
#include "a2b_amp.hpp"

#include "hardware/adc.h"
#include "board_list.h"

#define A2B_BUS_ADC_GPIO 26
#define A2B_AMP_ADC_GPIO 27
#define A2B_CURRENT_ADC_GPIO 28
#define A2B_22V_EN_GPIO 24
#define A2B_ICTRL_PWM_GPIO 25

#if (DEVICE_BOARD_NAME == W55RP20_EVB_PICO)
#define A2B_ICTRL_CONFLICT 1
#else
#define A2B_ICTRL_CONFLICT 0
#endif

#define A2B_BUS_ADC_SCALE ((200.0F + 10.0F)/10.0F)
#define A2B_AMP_ADC_SCALE ((200.0F + 10.0F)/10.0F)
#define A2B_CURRENT_ADC_SCALE 2000.0f

#define A2B_ADC_VREF 3.3f
#define A2B_ADC_MAX 4095.0f

static bool s_a2b_adc_initialized = false;
static int s_a2b_ictrl_mode = A2B_ICTRL_DISABLED;
static bool s_a2b_ictrl_available = false;

static inline uint adc_gpio_to_input(uint gpio)
{
    return gpio - 26u;
}

static float read_adc_scaled(uint gpio, float scale)
{
    adc_select_input(adc_gpio_to_input(gpio));
    return adc_read() * A2B_ADC_VREF / A2B_ADC_MAX * scale;
}

void a2b_amp_init()
{
    if (s_a2b_adc_initialized)
    {
        return;
    }

    adc_init();
    adc_gpio_init(A2B_BUS_ADC_GPIO);
    adc_gpio_init(A2B_AMP_ADC_GPIO);
    adc_gpio_init(A2B_CURRENT_ADC_GPIO);

    gpio_init(A2B_22V_EN_GPIO);
    gpio_set_dir(A2B_22V_EN_GPIO, GPIO_OUT);
    gpio_put(A2B_22V_EN_GPIO, 0);

#if A2B_ICTRL_CONFLICT
    s_a2b_ictrl_available = false;
    s_a2b_ictrl_mode = A2B_ICTRL_DISABLED;
#else
    gpio_init(A2B_ICTRL_PWM_GPIO);
    gpio_set_dir(A2B_ICTRL_PWM_GPIO, GPIO_IN);
    gpio_disable_pulls(A2B_ICTRL_PWM_GPIO);
    s_a2b_ictrl_available = true;
    s_a2b_ictrl_mode = A2B_ICTRL_DISABLED;
#endif

    s_a2b_adc_initialized = true;
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

void a2b_ictrl_set(int mode)
{
    a2b_amp_init();

    if (!s_a2b_ictrl_available)
    {
        s_a2b_ictrl_mode = A2B_ICTRL_DISABLED;
        return;
    }

    switch (mode)
    {
        case A2B_ICTRL_LOW:
            gpio_set_dir(A2B_ICTRL_PWM_GPIO, GPIO_OUT);
            gpio_put(A2B_ICTRL_PWM_GPIO, 0);
            s_a2b_ictrl_mode = A2B_ICTRL_LOW;
            break;
        case A2B_ICTRL_HIGH:
            gpio_set_dir(A2B_ICTRL_PWM_GPIO, GPIO_OUT);
            gpio_put(A2B_ICTRL_PWM_GPIO, 1);
            s_a2b_ictrl_mode = A2B_ICTRL_HIGH;
            break;
        default:
            gpio_set_dir(A2B_ICTRL_PWM_GPIO, GPIO_IN);
            gpio_disable_pulls(A2B_ICTRL_PWM_GPIO);
            s_a2b_ictrl_mode = A2B_ICTRL_DISABLED;
            break;
    }
}

int a2b_ictrl_get()
{
    a2b_amp_init();

    if (!s_a2b_ictrl_available)
    {
        return A2B_ICTRL_DISABLED;
    }

    return s_a2b_ictrl_mode;
}

float a2b_bus_voltage()
{
    a2b_amp_init();
    return read_adc_scaled(A2B_BUS_ADC_GPIO, A2B_BUS_ADC_SCALE);
}

float a2b_amp_voltage()
{
    a2b_amp_init();
    return read_adc_scaled(A2B_AMP_ADC_GPIO, A2B_AMP_ADC_SCALE);
}

float a2b_amp_current()
{
    a2b_amp_init();
    return read_adc_scaled(A2B_CURRENT_ADC_GPIO, A2B_CURRENT_ADC_SCALE);
}