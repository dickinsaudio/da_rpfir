/*******************************************************************
 * Copyright (C) 2023 DickinsAudio
 * 
 * This source code is the proprietary information of DickinsAudio.
 * All rights reserved.
 * 
 * This code is provided under specific license agreements and is 
 * intended for evaluation and consideration for licensed use. 
 * For discussions on licensing terms and pricing, please contact 
 * info@dickins.com
 * 
 * Licensed users are permitted full use of this code for the 
 * development and building of applications and systems, including 
 * modification, extension and use and transfer within
 * alternate representations, repositories and licensing frameworks
 * as allowed by the licensing arrangements in place with 
 * DickinsAudio.
 * 
 * Any use of this code outside of evaluation, consideration for 
 * licensed use, or as aggreed by license by licensed users is 
 * strictly prohibited.
 * 
 * DickinsAudio assumes no liability, either directly or indirectly, 
 * for the use of this software in relation to the use of the software 
 * and its relationship to any third-party intellectual property.
 *******************************************************************/

// Power management — PWM rail control with ADC voltage feedback
// See power.hpp for configuration defines and API documentation.

#include "power.hpp"
#include "da_rpfir.hpp"
#include "pwm.h"
#include "hardware/adc.h"

//---------------------------------------------------------------------------
// ADC SETUP
//---------------------------------------------------------------------------
#define POWER_ADC_WINDOW   64      // Number of samples to average for ADC readings.  Adjust to taste: larger = smoother but slower response.
#define POWER_ADC_RATE     8000    // ADC sampling rate

//---------------------------------------------------------------------------
// ADC scaling — convert raw ADC value to voltage at the amplifier rail
//---------------------------------------------------------------------------
#define POWER_R_HIGH_OHM        47000.0f    // High-side resistor (ohms)
#define POWER_R_LOW_OHM          3300.0f    // Low-side resistor (ohms)
#define POWER_ADC_VREF             3.295f   // ADC reference voltage (volts)
#define POWER_ADC_OFFSET              10    // ADC counts to offset for 0V 
#define POWER_ADC_MAX               4092    // ADC maximum count (12-bit)

// ---------------------------------------------------------------------------
// GPIO pin assignments
// ---------------------------------------------------------------------------
#define POWER_VAMP_EN_PIN       24      // Amplifier PMIC enable (active high)
#define POWER_PWM0_PIN          22      // PWM output — rail 0
#define POWER_PWM1_PIN          23      // PWM output — rail 1
#define POWER_VSEN0_PIN         28      // ADC input — rail 0 sense (ADC channel 2)
#define POWER_VSEN1_PIN         29      // ADC input — rail 1 sense (ADC channel 3)

// ---------------------------------------------------------------------------
// PWM parameters
// ---------------------------------------------------------------------------
#define POWER_PWM_MAX           1023    // PWM wrap / top value (10-bit)
#define POWER_PWM_FREQ_KHZ      100     // PWM switching frequency (kHz)

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------
static float s_target_volts  = 0.0f;
static bool power_adc_initialized = false;
static int power_adc_dma1 = -1;
static int power_adc_dma2 = -1;
uint16_t power_adc_samples[2*POWER_ADC_WINDOW] = {};   // DMA buffer for ADC samples
static float  power_voltage[2] = {};                         // Smoothed voltage readings in volts
static uint32_t power_adc_trigger;


//---------------------------------------------------------------------------
// Setup
//---------------------------------------------------------------------------


void power_adc_setup();
void power_init()
{
    gpio_init(POWER_VAMP_EN_PIN);
    gpio_set_dir(POWER_VAMP_EN_PIN, GPIO_OUT);
    gpio_put(POWER_VAMP_EN_PIN, 0);

    sleep_ms(10000);  
    printf("Power management init\n");
    // VAMP_EN — disabled until power_enable(true)

    power_adc_setup();

    // PWM outputs — start at POWER_PWM_NOMINAL (~43V), closed loop trims from there
    gpio_init(POWER_PWM0_PIN);
    pwmInit(POWER_PWM0_PIN, POWER_PWM_MAX, POWER_PWM_FREQ_KHZ, POWER_PWM_NOMINAL);
    setPemLvl(POWER_PWM0_PIN, POWER_PWM_NOMINAL);

    gpio_init(POWER_PWM1_PIN);
    pwmInit(POWER_PWM1_PIN, POWER_PWM_MAX, POWER_PWM_FREQ_KHZ, POWER_PWM_NOMINAL);
    setPemLvl(POWER_PWM1_PIN, POWER_PWM_NOMINAL);

}

// ---------------------------------------------------------------------------
// power_enable
// ---------------------------------------------------------------------------
void power_enable(bool on)
{
    gpio_put(POWER_VAMP_EN_PIN, on ? 1 : 0);
}

// ---------------------------------------------------------------------------
// power_set_voltage
// ---------------------------------------------------------------------------
void power_set_voltage(float volts)
{
    s_target_volts = volts;
}

void power_update()
{

}

float power_adc_to_voltage(float adc_value) 
{
    return (adc_value-POWER_ADC_OFFSET) * (float)POWER_ADC_VREF / POWER_ADC_MAX * (POWER_R_HIGH_OHM + POWER_R_LOW_OHM) / POWER_R_LOW_OHM;
}

int32_t power_adc_isr_count = 0;
__not_in_flash() void power_adc_isr() 
{
    dma_hw->ints1 = 1u << power_adc_dma2;
    power_adc_isr_count++;
    uint32_t sum[2] = {};
    uint16_t *samples = power_adc_samples;
    for (int i = 0; i < POWER_ADC_WINDOW; i++) 
    {
        sum[0] += *samples++;
        sum[1] += *samples++;
    }
    power_voltage[0] = power_adc_to_voltage((float)sum[0] / POWER_ADC_WINDOW);
    power_voltage[1] = power_adc_to_voltage((float)sum[1] / POWER_ADC_WINDOW);
}

void power_adc_setup() 
{
    if (!power_adc_initialized)
    {
        adc_init();                         // Must come first — brings ADC out of reset
        adc_gpio_init(POWER_VSEN0_PIN);
        adc_gpio_init(POWER_VSEN1_PIN);
        adc_run(false);
        adc_set_round_robin(0x0C);                              // ADC2 ADC3
        adc_fifo_setup(true, true, 1, false,  false);           // Enable with DMA, dreq on 1 sample, 12-bit (no shift)
        adc_set_clkdiv((48000000LL/8000/2) - 1);                // Set the rate of sampling

        adc_select_input(2);                                    // Shift start to get the I value last (lowest latency control)
        if (power_adc_dma1 < 0) power_adc_dma1 = dma_claim_unused_channel(true);    // Claim a DMA channel
        if (power_adc_dma2 < 0) power_adc_dma2 = dma_claim_unused_channel(true);    // Claim a DMA channel
        dma_channel_config conf1 = dma_channel_get_default_config(power_adc_dma1);
        dma_channel_config conf2 = dma_channel_get_default_config(power_adc_dma2);

        channel_config_set_transfer_data_size(&conf1, DMA_SIZE_16);
        channel_config_set_read_increment    (&conf1, false);
        channel_config_set_write_increment   (&conf1, true);
        channel_config_set_dreq              (&conf1, DREQ_ADC);
        channel_config_set_chain_to          (&conf1, power_adc_dma2);
        channel_config_set_high_priority     (&conf1, false);
        dma_channel_configure          (power_adc_dma1, &conf1, power_adc_samples, &adc_hw->fifo, 2*POWER_ADC_WINDOW, false);
        
        power_adc_trigger = (uint32_t)power_adc_samples;
        channel_config_set_read_increment    (&conf2, false);            
        channel_config_set_write_increment   (&conf2, false);            
        channel_config_set_transfer_data_size(&conf2, DMA_SIZE_32);         
        channel_config_set_high_priority     (&conf2, false);
        dma_channel_configure          (power_adc_dma2, &conf2, &dma_hw->ch[power_adc_dma1].al2_write_addr_trig, &power_adc_trigger, 1, false);

        dma_channel_set_irq1_enabled   (power_adc_dma2, true);
        irq_set_exclusive_handler(DMA_IRQ_1,power_adc_isr);

        dma_start_channel_mask(1u << power_adc_dma1);    // Start data channel first so first ISR fires after a full buffer
        adc_run(true);

        power_adc_initialized = true;
    }
    irq_set_enabled(DMA_IRQ_1, true);
    irq_set_priority(DMA_IRQ_1, 0x00);
}

float power_read_voltage(int ch)
{
    if (ch < 0 || ch > 1) return 0.0f;
    return power_voltage[ch];
}
