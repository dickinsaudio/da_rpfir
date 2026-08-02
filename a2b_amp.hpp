#pragma once

void a2b_amp_init();

float a2b_bus_voltage();
float a2b_amp_voltage();
float a2b_amp_current();

void a2b_22v_enable_set(bool enabled);
bool a2b_22v_enable_get();