#pragma once

#include <cstddef>
#include <cstdint>

struct max98415_status_t
{
	bool online;
	bool enabled;
	bool internal_tone_enabled;
	uint8_t amplifier_enables;
	bool pvdd_valid;
	bool amp_a_temperature_valid;
	bool amp_b_temperature_valid;
	uint16_t pvdd_raw;
	uint16_t amp_a_temperature_raw;
	uint16_t amp_b_temperature_raw;
	float pvdd_voltage;
	float amp_a_temperature;
	float amp_b_temperature;
	uint8_t supply_raw;
	uint8_t power_raw;
	uint8_t clock_raw;
	uint8_t amplifier_fault_raw;
	uint8_t amp_a_thermal_raw;
	uint8_t amp_b_thermal_raw;
	uint8_t supply_state;
	uint8_t power_state;
	uint8_t clock_state;
	uint8_t amplifier_fault_state;
	uint8_t amp_a_thermal_state;
	uint8_t amp_b_thermal_state;
	uint16_t enable_failure_register;
	uint8_t enable_failure_expected;
	uint8_t enable_failure_actual;
	bool enable_failure_write_ok;
};

bool amp_enable();
bool amp_disable();
bool max98415_set_amplifier_channels(bool amp_a, bool amp_b);
bool max98415_internal_tone_enable(bool enable);
bool max98415_read_status(max98415_status_t *status);
size_t max98415_register_count();
int max98415_format_register(size_t index, char *buffer, size_t length, bool *read_ok);
int max98415_format_register_value(size_t index, char *buffer, size_t length, bool *read_ok);