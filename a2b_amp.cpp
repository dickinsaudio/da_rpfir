
#include "a2b_amp.hpp"

#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "histogram.hpp"
#include "board_list.h"
#include "pico/stdlib.h"
#include "pwm.h"
#include "log.hpp"
#include <cmath>

using DAES67::Histogram;

#define A2B_BUS_ADC_GPIO 27
#define A2B_AMP_ADC_GPIO 26
#define A2B_CURRENT_ADC_GPIO 28
#define A2B_22V_EN_GPIO 11
#define A2B_ICTRL_PWM_GPIO 10  
#define A2B_ICTRL_PWM_WRAP 999
#define A2B_ICTRL_PWM_FREQ_KHZ 288

static constexpr float s_ictrl_rf = 1000.0F;
static constexpr float s_ictrl_ra = 4700.0F;
static constexpr float s_ictrl_rb = 5000.0F;
static constexpr float s_ictrl_rc = 1000000.0F;
static constexpr float s_ictrl_bias_voltage = 24.0F;
static constexpr float s_ictrl_pwm_voltage = 3.3F;
static constexpr float s_ictrl_rsens = 0.025F;
static constexpr float s_ictrl_gain = 20.0F;
// At DC the filter capacitor is open, so Rf and Ra are in series.
static constexpr float s_ictrl_series = s_ictrl_rf + s_ictrl_ra;
static constexpr float s_ictrl_conductance =
    1.0F / s_ictrl_series + 1.0F / s_ictrl_rb + 1.0F / s_ictrl_rc;
static constexpr float s_ictrl_ma_per_volt = 1000.0F / (s_ictrl_rsens * s_ictrl_gain);

#define A2B_BUS_ADC_SCALE ((200.0F + 22.1F)/22.1F)
#define A2B_AMP_ADC_SCALE ((200.0F + 22.1F)/22.1F)
#define A2B_CURRENT_ADC_SCALE 2000.0f
#define A2B_CURRENT_FLOOR_ON_MA   40.0f // Lowest real input current with 22V on - below this is sense offset
#define A2B_CURRENT_FLOOR_OFF_MA  20.0f // Input current with 22V off (fixed - the sense output is not valid then)

#define A2B_ADC_VREF 3.3f
#define A2B_ADC_MAX 4095.0f

#define A2B_ADC_RATE        8000        // Rate each channel is sampled by the round robin DMA
#define A2B_ADC_LOG_RATE    1000        // Rate the smoothed values are added to the histograms
#define A2B_ADC_FILTER      4           // Binary low pass filter a = [ 1 -(2^FILTER-1)/2^FILTER ]
#define A2B_ADC_START_CHAN  0
#define A2B_ADC_SLOT(gpio)  ( ( ( (gpio) - 26 ) + 3 - A2B_ADC_START_CHAN ) % 3 )
#define A2B_BUS_SLOT        A2B_ADC_SLOT(A2B_BUS_ADC_GPIO)
#define A2B_AMP_SLOT        A2B_ADC_SLOT(A2B_AMP_ADC_GPIO)
#define A2B_CURRENT_SLOT    A2B_ADC_SLOT(A2B_CURRENT_ADC_GPIO)

static Histogram s_bus_hist    ("A2B Input (V)",  0, 30);
static Histogram s_current_hist("A2B Input (mA)", 0, 1000);
static Histogram s_amp_hist    ("A2B Amp (V)",    0, 30);
static Histogram s_adc_time    ("A2B ADC IRQ (s)",0, .002);

static bool s_a2b_adc_initialized = false;
static float s_a2b_current_limit_ma = 0.0F;
static volatile float s_current_avg_ma;             // First order, ~1 s time constant, updated with the histograms

static uint32_t __aligned(8) s_adc_trigger;         // Address reloaded into DMA1 write address by DMA2
static int16_t  __aligned(8) s_adc_samples[4];      // Raw samples written by DMA1
static int32_t               s_adc_smoothed[3];     // Smoothed ADC values with 8 extra fractional bits
static int s_adc_dma1 = -1;                         // DMA to read 3 ADC values to memory
static int s_adc_dma2 = -1;                         // DMA to retrigger DMA1 and raise the IRQ

static inline float adc_smoothed_scaled(int slot, float scale)
{
    return (float)s_adc_smoothed[slot] * (A2B_ADC_VREF / A2B_ADC_MAX / (1 << 8) * scale);
}

static void __not_in_flash_func(a2b_adc_isr)()
{
    static uint32_t count = 0;
    dma_hw->ints1 = 1u << s_adc_dma2;
    for (int n = 0; n < 3; ++n)
        s_adc_smoothed[n] = (s_adc_smoothed[n] * ((1 << A2B_ADC_FILTER) - 1) + (s_adc_samples[n] << 8)) >> A2B_ADC_FILTER;

    if (count++ % (A2B_ADC_RATE / A2B_ADC_LOG_RATE)) return;

    s_adc_time.time();
    s_bus_hist.add    (adc_smoothed_scaled(A2B_BUS_SLOT,     A2B_BUS_ADC_SCALE));
    // With 22V off the sense output is meaningless - use the fixed off current. With it on, the
    // reading never goes below the board's real minimum. (The average just settles across a switch.)
    float current_ma = A2B_CURRENT_FLOOR_OFF_MA;
    if (gpio_get_out_level(A2B_22V_EN_GPIO))
    {
        current_ma = adc_smoothed_scaled(A2B_CURRENT_SLOT, A2B_CURRENT_ADC_SCALE);
        if (current_ma < A2B_CURRENT_FLOOR_ON_MA) current_ma = A2B_CURRENT_FLOOR_ON_MA;
    }
    s_current_hist.add(current_ma);
    s_current_avg_ma += (current_ma - s_current_avg_ma) * (1.0F / A2B_ADC_LOG_RATE);   // tau = 1 s
    s_amp_hist.add    (adc_smoothed_scaled(A2B_AMP_SLOT,     A2B_AMP_ADC_SCALE));
}

static void a2b_adc_setup()
{
    adc_run(false);
    adc_init();
    adc_gpio_init(A2B_BUS_ADC_GPIO);
    adc_gpio_init(A2B_AMP_ADC_GPIO);
    adc_gpio_init(A2B_CURRENT_ADC_GPIO);
    adc_set_round_robin(0x07);                                  // ADC0 ADC1 ADC2
    adc_fifo_setup(true, true, 1, false, false);                // FIFO with DMA request, full 16 bit samples
    adc_set_clkdiv((48000000LL / A2B_ADC_RATE / 3) - 1);
    adc_select_input(A2B_ADC_START_CHAN);

    // Pre-load the filters so the histograms don't ramp up from zero
    for (int n = 0; n < 3; ++n)
    {
        adc_select_input((A2B_ADC_START_CHAN + n) % 3);
        s_adc_smoothed[n] = adc_read() << 8;
    }
    adc_select_input(A2B_ADC_START_CHAN);
    adc_fifo_drain();

    if (s_adc_dma1 < 0) s_adc_dma1 = dma_claim_unused_channel(true);
    if (s_adc_dma2 < 0) s_adc_dma2 = dma_claim_unused_channel(true);
    dma_channel_config conf1 = dma_channel_get_default_config(s_adc_dma1);
    dma_channel_config conf2 = dma_channel_get_default_config(s_adc_dma2);

    channel_config_set_transfer_data_size(&conf1, DMA_SIZE_16);
    channel_config_set_read_increment    (&conf1, false);
    channel_config_set_write_increment   (&conf1, true);
    channel_config_set_dreq              (&conf1, DREQ_ADC);
    channel_config_set_chain_to          (&conf1, s_adc_dma2);
    dma_channel_configure(s_adc_dma1, &conf1, s_adc_samples, &adc_hw->fifo, 3, false);

    s_adc_trigger = (uint32_t)s_adc_samples;
    channel_config_set_transfer_data_size(&conf2, DMA_SIZE_32);
    channel_config_set_read_increment    (&conf2, false);
    channel_config_set_write_increment   (&conf2, false);
    dma_channel_configure(s_adc_dma2, &conf2, &dma_hw->ch[s_adc_dma1].al2_write_addr_trig, &s_adc_trigger, 1, false);

    dma_channel_set_irq1_enabled(s_adc_dma2, true);
    irq_set_exclusive_handler(DMA_IRQ_1, a2b_adc_isr);
    irq_set_priority(DMA_IRQ_1, 0x40);                          // Below the I2S audio DMA (0x20)
    irq_set_enabled(DMA_IRQ_1, true);

    dma_start_channel_mask(1u << s_adc_dma2);
    adc_run(true);
}

void a2b_amp_init()
{
    if (s_a2b_adc_initialized)
    {
        return;
    }

    a2b_adc_setup();

    gpio_init(A2B_22V_EN_GPIO);
    gpio_set_dir(A2B_22V_EN_GPIO, GPIO_OUT);
    gpio_put(A2B_22V_EN_GPIO, 0);

    gpio_init(A2B_ICTRL_PWM_GPIO);
    gpio_disable_pulls(A2B_ICTRL_PWM_GPIO);
    pwmInit(A2B_ICTRL_PWM_GPIO, A2B_ICTRL_PWM_WRAP, A2B_ICTRL_PWM_FREQ_KHZ, 0);

    s_a2b_adc_initialized = true;
    a2b_current_limit_set(A2B_CURRENT_LIMIT_DEFAULT_MA);
}

void a2b_22v_enable_set(bool enabled)
{
    a2b_amp_init();
    gpio_put(A2B_22V_EN_GPIO, enabled ? 1 : 0);
}

bool a2b_22v_enable_get()
{
    a2b_amp_init();
    return gpio_get(A2B_22V_EN_GPIO) != 0;
}

float a2b_current_limit_min()
{
    return (s_ictrl_bias_voltage / s_ictrl_rc) / s_ictrl_conductance * s_ictrl_ma_per_volt;
}

float a2b_current_limit_max()
{
    return (s_ictrl_pwm_voltage / s_ictrl_series + s_ictrl_bias_voltage / s_ictrl_rc)
           / s_ictrl_conductance * s_ictrl_ma_per_volt;
}

bool a2b_current_limit_set(float current_ma)
{
    if (!std::isfinite(current_ma)) return false;
    if (current_ma < a2b_current_limit_min() || current_ma > a2b_current_limit_max())
    {
        Warning("Current limit %.1f mA outside achievable range %.1f..%.1f mA - clamped",
                current_ma, a2b_current_limit_min(), a2b_current_limit_max());
        current_ma = current_ma < a2b_current_limit_min() ? a2b_current_limit_min() : a2b_current_limit_max();
    }
    a2b_amp_init();
    const float ictl_voltage = current_ma / s_ictrl_ma_per_volt;
    const float pwm_voltage = s_ictrl_series *
        (ictl_voltage * s_ictrl_conductance - s_ictrl_bias_voltage / s_ictrl_rc);
    const uint level = (uint)std::lround(pwm_voltage / s_ictrl_pwm_voltage * (A2B_ICTRL_PWM_WRAP + 1));
    pwm_set_gpio_level(A2B_ICTRL_PWM_GPIO, level);
    const float actual_pwm_voltage = s_ictrl_pwm_voltage * level / (A2B_ICTRL_PWM_WRAP + 1);
    s_a2b_current_limit_ma = (actual_pwm_voltage / s_ictrl_series + s_ictrl_bias_voltage / s_ictrl_rc)
                            / s_ictrl_conductance * s_ictrl_ma_per_volt;
    return true;
}

float a2b_current_limit_get()
{
    a2b_amp_init();
    return s_a2b_current_limit_ma;
}

float a2b_bus_voltage()
{
    return s_bus_hist.latest();
}

float a2b_amp_voltage()
{
    return s_amp_hist.latest();
}

float a2b_input_current()
{
    return s_current_hist.latest();
}

float a2b_input_current_avg()
{
    return s_current_avg_ma;
}

void a2b_amp_stats_clear()
{
    s_bus_hist.clear();
    s_current_hist.clear();
    s_amp_hist.clear();
    s_adc_time.clear();
}

int a2b_amp_stats_text(int step, int len, char *buf)
{
    static const Histogram *const hists[] = { &s_bus_hist, &s_current_hist, &s_amp_hist, &s_adc_time };
    if (step < 0 || step >= (int)(sizeof(hists) / sizeof(hists[0])) || len < 2 || !buf) return 0;
    int n = hists[step]->sntext(len - 1, 12, buf);
    buf[n++] = '\n';
    return n;
}
