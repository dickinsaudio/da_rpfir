#pragma once

void a2b_amp_init();

float a2b_bus_voltage();
float a2b_amp_voltage();
float a2b_input_current();
float a2b_input_current_avg();      // ~1 s first order average of the input current (mA)

// ADC is sampled at 8kHz via DMA, low pass filtered and logged to histograms at 1kHz
void a2b_amp_stats_clear();
int  a2b_amp_stats_text(int step, int len, char *buf);     // Text for histogram 'step', returns 0 when done

void a2b_22v_enable_set(bool enabled);
bool a2b_22v_enable_get();

constexpr float A2B_CURRENT_LIMIT_DEFAULT_MA = 200.0F;
// Always PWM on GPIO10. Requests outside the achievable range are clamped to it (and logged).
bool a2b_current_limit_set(float current_ma);
// Nominal sense-current limit after PWM quantization, not measured supply current.
float a2b_current_limit_get();
float a2b_current_limit_min();
float a2b_current_limit_max();