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

enum
{
	A2B_ICTRL_DISABLED = 0,
	A2B_ICTRL_LOW = 1,
	A2B_ICTRL_HIGH = 2,
	A2B_ICTRL_PWM_9 = 3
};

void a2b_ictrl_set(int mode);
int a2b_ictrl_get();