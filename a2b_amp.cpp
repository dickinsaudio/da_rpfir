
#include "a2b_amp.hpp"

#include "hardware/adc.h"

#define A2B_BUS_ADC_GPIO 26
#define A2B_AMP_ADC_GPIO 27
#define A2B_CURRENT_ADC_GPIO 28

#define A2B_BUS_ADC_SCALE ((200.0F + 10.0F)/10.0F)
#define A2B_AMP_ADC_SCALE ((200.0F + 10.0F)/10.0F)
#define A2B_CURRENT_ADC_SCALE 1000.0f

#define A2B_ADC_VREF 3.3f
#define A2B_ADC_MAX 4095.0f

static bool s_a2b_adc_initialized = false;

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
    s_a2b_adc_initialized = true;
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