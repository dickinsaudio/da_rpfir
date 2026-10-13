#pragma once

void a2b_amp_init();

float a2b_bus_voltage();
float a2b_amp_voltage();
float a2b_input_current();

// ADC is sampled at 8kHz via DMA, low pass filtered and logged to histograms at 1kHz
void a2b_amp_stats_clear();
int  a2b_amp_stats_text(int step, int len, char *buf);     // Text for histogram 'step', returns 0 when done

void a2b_22v_enable_set(bool enabled);
bool a2b_22v_enable_get();

constexpr float A2B_CURRENT_LIMIT_DEFAULT_MA = 400.0F;
// Always PWM on GPIO10. Invalid/unachievable requests are logged and leave the setting unchanged.
bool a2b_current_limit_set(float current_ma);
// Nominal sense-current limit after PWM quantization, not measured supply current.
float a2b_current_limit_get();
float a2b_current_limit_min();
float a2b_current_limit_max();