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
bool max98415_internal_tone_toggle();

// Preset dynamics (brown-out / limiting). Kept across amp_enable() (which resets the chip).
enum max98415_dynamics_t
{
	MAX98415_DYNAMICS_OFF,          // DHT and BPE off
	MAX98415_DYNAMICS_BROWNOUT,     // DHT 5% headroom + BPE stepped gain reduction as PVDD falls
	MAX98415_DYNAMICS_COUNT
};
bool max98415_set_dynamics(max98415_dynamics_t dynamics);
max98415_dynamics_t max98415_get_dynamics();
const char *max98415_dynamics_name(max98415_dynamics_t dynamics);
// Summary for the amp page (state, tone, channels, PVDD, die temperatures; supply/clock raw for faults).
// Reads the amp now, in one I2C master window - call only when the page is loaded.
bool max98415_read_status(max98415_status_t *status);

size_t max98415_register_count();
int max98415_format_register(size_t index, char *buffer, size_t length, bool *read_ok);
int max98415_format_register_value(size_t index, char *buffer, size_t length, bool *read_ok);