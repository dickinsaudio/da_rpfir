#pragma once

void a2b_amp_init();

float a2b_bus_voltage();
float a2b_amp_voltage();
float a2b_amp_current();

void a2b_22v_enable_set(bool enabled);
bool a2b_22v_enable_get();

enum
{
	A2B_ICTRL_DISABLED = 0,
	A2B_ICTRL_LOW = 1,
	A2B_ICTRL_HIGH = 2
};

void a2b_ictrl_set(int mode);
int a2b_ictrl_get();