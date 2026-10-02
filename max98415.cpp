#include "max98415.hpp"

#include "peripherals.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"

#define MAX98415_I2C_ADDRESS 0x39
#define MAX98415_RESET_PIN 16
#define MAX98415_MUTE_PIN 17
#define MAX98415_REGISTER_COUNT 217

#define MAX98415_REG_SUPPLY_INTERRUPT_RAW 0x2000
#define MAX98415_REG_POWER_INTERRUPT_RAW 0x2001
#define MAX98415_REG_CLOCK_INTERRUPT_RAW 0x2003
#define MAX98415_REG_AMPLIFIER_FAULT_RAW 0x2005
#define MAX98415_REG_AMP_A_THERMAL_RAW 0x2006
#define MAX98415_REG_AMP_B_THERMAL_RAW 0x2007
#define MAX98415_REG_SUPPLY_INTERRUPT_STATE 0x2010
#define MAX98415_REG_POWER_INTERRUPT_STATE 0x2011
#define MAX98415_REG_CLOCK_INTERRUPT_STATE 0x2013
#define MAX98415_REG_AMPLIFIER_FAULT_STATE 0x2015
#define MAX98415_REG_AMP_A_THERMAL_STATE 0x2016
#define MAX98415_REG_AMP_B_THERMAL_STATE 0x2017
#define MAX98415_REG_CLOCK_MONITOR_CONTROL 0x21F0
#define MAX98415_REG_PCM_MODE_CONFIG 0x2201
#define MAX98415_REG_PCM_CLOCK_SETUP 0x2202
#define MAX98415_REG_PCM_SAMPLE_RATE 0x2203
#define MAX98415_REG_PCM_RX_SOURCE_1 0x2229
#define MAX98415_REG_PCM_RX_SOURCE_2 0x222A
#define MAX98415_REG_PCM_RX_ENABLES 0x222E
#define MAX98415_REG_TONE_CONFIG 0x2283
#define MAX98415_REG_TONE_ENABLE 0x228F
#define MAX98415_REG_AMP_A_VOLUME 0x22F0
#define MAX98415_REG_AMP_B_VOLUME 0x22F1
#define MAX98415_REG_VOLUME_UPDATE 0x22FF
#define MAX98415_REG_NOISE_GATE_ENABLES 0x22E3
#define MAX98415_REG_AMP_A_PATH_GAIN 0x2302
#define MAX98415_REG_AMP_A_DSP_CONFIG 0x2303
#define MAX98415_REG_AMP_B_PATH_GAIN 0x2322
#define MAX98415_REG_AMP_B_DSP_CONFIG 0x2323
#define MAX98415_REG_AMP_ENABLES 0x23FF
#define MAX98415_REG_BPE_ENABLE 0x283F
#define MAX98415_REG_DHT_ENABLE 0x284F
#define MAX98415_REG_RMS_LIMITER_ENABLE 0x28AF
#define MAX98415_REG_MEASUREMENT_ADC_READBACK_CONTROL 0x2704
#define MAX98415_REG_PVDD_MSB 0x2720
#define MAX98415_REG_PVDD_LSB 0x2721
#define MAX98415_REG_AMP_A_TEMPERATURE_MSB 0x2724
#define MAX98415_REG_AMP_A_TEMPERATURE_LSB 0x2725
#define MAX98415_REG_AMP_B_TEMPERATURE_MSB 0x2726
#define MAX98415_REG_AMP_B_TEMPERATURE_LSB 0x2727
#define MAX98415_REG_MEASUREMENT_ADC_CONFIG 0x273F
#define MAX98415_REG_GROUP_WRITE_ADDRESS 0x29F1
#define MAX98415_REG_AUTO_RESTART_BEHAVIOR 0x29FE
#define MAX98415_REG_GLOBAL_ENABLE 0x29FF

#define MAX98415_PCM_CHANNEL_SIZE_32_BIT (0x03 << 6)
#define MAX98415_PCM_FORMAT_I2S (0x00 << 3)
#define MAX98415_PCM_BCLK_RISING_EDGE (0x00 << 4)
#define MAX98415_PCM_BCLKS_PER_FRAME_64 0x04
#define MAX98415_SAMPLE_RATE_48_KHZ 0x08
#define MAX98415_PCM_STEREO_A_CH0_B_CH1 0x00
#define MAX98415_PCM_CHANNEL_0_AND_1_SOURCES 0x10
#define MAX98415_PCM_RECEIVE_ENABLE 0x01
#define MAX98415_TONE_1_KHZ_MINUS_6_DBFS 0x04
#define MAX98415_TONE_ENABLE 0x01
#define MAX98415_TONE_DISABLE 0x00
#define MAX98415_VOLUME_0_DB 0x00
#define MAX98415_SPK_DSP_CONFIG_NORMAL 0x08
#define MAX98415_UPDATE_BOTH_VOLUMES 0x03
#define MAX98415_MAX_OUTPUT_23_71_V_PEAK 0x12
#define MAX98415_ENABLE_BOTH_AMPLIFIERS 0x03
#define MAX98415_MEASUREMENT_ADC_CONTINUOUS_READBACK 0x00
#define MAX98415_FORCE_PVDD_ADC_ENABLE 0x01
#define MAX98415_CLOCK_AUTO_RESTART_ENABLE 0x01
#define MAX98415_AUTO_RESTART_DISABLE 0x00
#define MAX98415_DEVICE_ENABLE 0x01
#define MAX98415_DEVICE_DISABLE 0x00
#define MAX98415_SUPPLY_FAULT_MASK 0x37
#define MAX98415_CLOCK_ERROR_MASK 0x03
#define MAX98415_MINIMUM_PVDD_CODE 57
#define MAX98415_MINIMUM_TEMPERATURE_CODE 223
#define MAX98415_MAXIMUM_TEMPERATURE_CODE 402

struct max98415_register_descriptor_t
{
    uint16_t address;
    const char *name;
    const char *fields;
};

#include "max98415_register_map.inc"

static_assert(sizeof(register_map) / sizeof(register_map[0]) == MAX98415_REGISTER_COUNT);

struct register_value_t
{
    uint16_t address;
    uint8_t value;
    bool verify = true;
};

static uint16_t enable_failure_register;
static uint8_t enable_failure_expected;
static uint8_t enable_failure_actual;
static bool enable_failure_write_ok;

static bool write_and_verify(uint16_t address, uint8_t value, uint8_t *readback, bool *write_ok)
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        *write_ok = i2c_write_register16(MAX98415_I2C_ADDRESS, address, value);
        if (*write_ok)
        {
            sleep_ms(1);
            if (i2c_read_register16(MAX98415_I2C_ADDRESS, address, readback) && *readback == value) return true;
        }
    }
    return false;
}

bool amp_enable()
{
    enable_failure_register = 0;
    enable_failure_expected = 0;
    enable_failure_actual = 0;
    enable_failure_write_ok = false;

    gpio_init(MAX98415_RESET_PIN);
    gpio_set_dir(MAX98415_RESET_PIN, GPIO_OUT);
    gpio_init(MAX98415_MUTE_PIN);
    gpio_set_dir(MAX98415_MUTE_PIN, GPIO_OUT);
    gpio_put(MAX98415_MUTE_PIN, 1);
    gpio_put(MAX98415_RESET_PIN, 0);
    sleep_us(2);
    gpio_put(MAX98415_RESET_PIN, 1);
    sleep_ms(10);

    if (!gpio_get(MAX98415_RESET_PIN))
    {
        enable_failure_register = 0xFFFF;
        enable_failure_expected = 1;
        enable_failure_actual = 0;
        enable_failure_write_ok = false;
        return false;
    }

    uint8_t probe_readback = 0;
    bool probe_write_ok = false;
    if (!write_and_verify(MAX98415_REG_GROUP_WRITE_ADDRESS, 0x12, &probe_readback, &probe_write_ok) ||
        !write_and_verify(MAX98415_REG_GROUP_WRITE_ADDRESS, 0x10, &probe_readback, &probe_write_ok))
    {
        enable_failure_register = MAX98415_REG_GROUP_WRITE_ADDRESS;
        enable_failure_expected = probe_readback == 0x12 ? 0x10 : 0x12;
        enable_failure_actual = probe_readback;
        enable_failure_write_ok = probe_write_ok;
        return false;
    }

    static constexpr register_value_t configuration[] = {
        {MAX98415_REG_GLOBAL_ENABLE, MAX98415_DEVICE_DISABLE},
        {MAX98415_REG_CLOCK_MONITOR_CONTROL, MAX98415_CLOCK_AUTO_RESTART_ENABLE},
        {MAX98415_REG_PCM_MODE_CONFIG, MAX98415_PCM_CHANNEL_SIZE_32_BIT | MAX98415_PCM_FORMAT_I2S},
        {MAX98415_REG_PCM_CLOCK_SETUP, MAX98415_PCM_BCLK_RISING_EDGE | MAX98415_PCM_BCLKS_PER_FRAME_64},
        {MAX98415_REG_PCM_SAMPLE_RATE, (uint8_t)((MAX98415_SAMPLE_RATE_48_KHZ << 4) | MAX98415_SAMPLE_RATE_48_KHZ)},
        {MAX98415_REG_PCM_RX_SOURCE_1, MAX98415_PCM_STEREO_A_CH0_B_CH1},
        {MAX98415_REG_PCM_RX_SOURCE_2, MAX98415_PCM_CHANNEL_0_AND_1_SOURCES},
        {MAX98415_REG_TONE_ENABLE, MAX98415_TONE_DISABLE},
        {MAX98415_REG_AMP_A_VOLUME, MAX98415_VOLUME_0_DB},
        {MAX98415_REG_AMP_B_VOLUME, MAX98415_VOLUME_0_DB},
        {MAX98415_REG_VOLUME_UPDATE, MAX98415_UPDATE_BOTH_VOLUMES, false},
        {MAX98415_REG_NOISE_GATE_ENABLES, 0x00},
        {MAX98415_REG_AMP_A_PATH_GAIN, MAX98415_MAX_OUTPUT_23_71_V_PEAK},
        {MAX98415_REG_AMP_A_DSP_CONFIG, MAX98415_SPK_DSP_CONFIG_NORMAL},
        {MAX98415_REG_AMP_B_PATH_GAIN, MAX98415_MAX_OUTPUT_23_71_V_PEAK},
        {MAX98415_REG_AMP_B_DSP_CONFIG, MAX98415_SPK_DSP_CONFIG_NORMAL},
        {MAX98415_REG_BPE_ENABLE, 0x00},
        {MAX98415_REG_DHT_ENABLE, 0x00},
        {MAX98415_REG_RMS_LIMITER_ENABLE, 0x00},
        {MAX98415_REG_MEASUREMENT_ADC_READBACK_CONTROL, MAX98415_MEASUREMENT_ADC_CONTINUOUS_READBACK},
        {MAX98415_REG_MEASUREMENT_ADC_CONFIG, MAX98415_FORCE_PVDD_ADC_ENABLE},
        {MAX98415_REG_AUTO_RESTART_BEHAVIOR, MAX98415_AUTO_RESTART_DISABLE},
        {MAX98415_REG_AMP_ENABLES, MAX98415_ENABLE_BOTH_AMPLIFIERS, false},
        {MAX98415_REG_PCM_RX_ENABLES, MAX98415_PCM_RECEIVE_ENABLE, false},
        {MAX98415_REG_GLOBAL_ENABLE, MAX98415_DEVICE_ENABLE},
    };

    for (const register_value_t &entry : configuration)
    {
        bool write_ok = false;
        uint8_t readback = 0;
        bool ok = entry.verify
                    ? write_and_verify(entry.address, entry.value, &readback, &write_ok)
                    : (write_ok = i2c_write_register16(MAX98415_I2C_ADDRESS, entry.address, entry.value));
        if (!ok)
        {
            enable_failure_register = entry.address;
            enable_failure_expected = entry.value;
            enable_failure_actual = readback;
            enable_failure_write_ok = write_ok;
            return false;
        }
    }

    sleep_ms(10);

    uint8_t global_enable = 0;
    uint8_t supply_raw = 0;
    uint8_t clock_raw = 0;
    if (!i2c_read_register16(MAX98415_I2C_ADDRESS, MAX98415_REG_GLOBAL_ENABLE, &global_enable) ||
        global_enable != MAX98415_DEVICE_ENABLE)
    {
        enable_failure_register = MAX98415_REG_GLOBAL_ENABLE;
        enable_failure_expected = MAX98415_DEVICE_ENABLE;
        enable_failure_actual = global_enable;
        enable_failure_write_ok = true;
        return false;
    }
    if (!i2c_read_register16(MAX98415_I2C_ADDRESS, MAX98415_REG_SUPPLY_INTERRUPT_RAW, &supply_raw) ||
        (supply_raw & MAX98415_SUPPLY_FAULT_MASK) != 0)
    {
        enable_failure_register = MAX98415_REG_SUPPLY_INTERRUPT_RAW;
        enable_failure_expected = 0;
        enable_failure_actual = supply_raw;
        enable_failure_write_ok = true;
        return false;
    }
    if (!i2c_read_register16(MAX98415_I2C_ADDRESS, MAX98415_REG_CLOCK_INTERRUPT_RAW, &clock_raw) ||
        (clock_raw & 0x03) != 0)
    {
        enable_failure_register = MAX98415_REG_CLOCK_INTERRUPT_RAW;
        enable_failure_expected = 0;
        enable_failure_actual = clock_raw;
        enable_failure_write_ok = true;
        return false;
    }

    static constexpr register_value_t enabled_state[] = {
        {MAX98415_REG_AMP_ENABLES, MAX98415_ENABLE_BOTH_AMPLIFIERS},
        {MAX98415_REG_PCM_RX_ENABLES, MAX98415_PCM_RECEIVE_ENABLE},
    };
    for (const register_value_t &entry : enabled_state)
    {
        uint8_t readback = 0;
        bool write_ok = true;
        if (!i2c_read_register16(MAX98415_I2C_ADDRESS, entry.address, &readback) || readback != entry.value)
        {
            enable_failure_register = entry.address;
            enable_failure_expected = entry.value;
            enable_failure_actual = readback;
            enable_failure_write_ok = write_ok;
            return false;
        }
    }
    return true;
}

bool amp_disable()
{
    gpio_put(MAX98415_MUTE_PIN, 1);
    uint8_t readback = 0;
    bool write_ok = false;
    return write_and_verify(MAX98415_REG_GLOBAL_ENABLE,
                            MAX98415_DEVICE_DISABLE,
                            &readback,
                            &write_ok);
}

bool max98415_set_amplifier_channels(bool amp_a, bool amp_b)
{
    uint8_t readback = 0;
    bool write_ok = false;
    uint8_t enables = (amp_a ? 0x01 : 0x00) | (amp_b ? 0x02 : 0x00);
    return write_and_verify(MAX98415_REG_AMP_ENABLES, enables, &readback, &write_ok);
}

bool max98415_internal_tone_enable(bool enable)
{
    uint8_t readback = 0;
    bool write_ok = false;
    uint8_t global_enable = 0;
    if (!i2c_read_register16(MAX98415_I2C_ADDRESS, MAX98415_REG_GLOBAL_ENABLE, &global_enable))
    {
        return false;
    }
    bool restore_enable = (global_enable & MAX98415_DEVICE_ENABLE) != 0;
    if (restore_enable && !write_and_verify(MAX98415_REG_GLOBAL_ENABLE,
                                            MAX98415_DEVICE_DISABLE,
                                            &readback,
                                            &write_ok))
    {
        return false;
    }
    if (enable && !write_and_verify(MAX98415_REG_TONE_CONFIG,
                                    MAX98415_TONE_1_KHZ_MINUS_6_DBFS,
                                    &readback,
                                    &write_ok))
    {
        return false;
    }
    if (!write_and_verify(MAX98415_REG_TONE_ENABLE,
                          enable ? MAX98415_TONE_ENABLE : MAX98415_TONE_DISABLE,
                          &readback,
                          &write_ok))
    {
        return false;
    }
    if (restore_enable)
    {
        if (!write_and_verify(MAX98415_REG_GLOBAL_ENABLE,
                              MAX98415_DEVICE_ENABLE,
                              &readback,
                              &write_ok))
        {
            return false;
        }
        sleep_ms(10);
    }
    return true;
}

static bool read_9_bit(uint16_t msb_address, uint16_t lsb_address, uint16_t *value)
{
    if (!value) return false;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        uint8_t first_msb = 0;
        uint8_t lsb = 0;
        uint8_t second_msb = 0;
        if (!i2c_read_register16(MAX98415_I2C_ADDRESS, msb_address, &first_msb) ||
            !i2c_read_register16(MAX98415_I2C_ADDRESS, lsb_address, &lsb) ||
            !i2c_read_register16(MAX98415_I2C_ADDRESS, msb_address, &second_msb))
        {
            return false;
        }
        if (first_msb == second_msb)
        {
            *value = ((uint16_t)first_msb << 1) | (lsb & 1u);
            return true;
        }
    }
    return false;
}

bool max98415_read_status(max98415_status_t *status)
{
    if (!status) return false;
    *status = {};

    uint16_t pvdd = 0;
    uint16_t amp_a_temperature = 0;
    uint16_t amp_b_temperature = 0;
    uint8_t global_enable = 0;
    uint8_t tone_enable = 0;
    uint8_t amplifier_enables = 0;
    bool ok = read_9_bit(MAX98415_REG_PVDD_MSB, MAX98415_REG_PVDD_LSB, &pvdd) &&
              read_9_bit(MAX98415_REG_AMP_A_TEMPERATURE_MSB, MAX98415_REG_AMP_A_TEMPERATURE_LSB, &amp_a_temperature) &&
              read_9_bit(MAX98415_REG_AMP_B_TEMPERATURE_MSB, MAX98415_REG_AMP_B_TEMPERATURE_LSB, &amp_b_temperature) &&
              i2c_read_register16(MAX98415_I2C_ADDRESS, MAX98415_REG_SUPPLY_INTERRUPT_RAW, &status->supply_raw) &&
              i2c_read_register16(MAX98415_I2C_ADDRESS, MAX98415_REG_POWER_INTERRUPT_RAW, &status->power_raw) &&
              i2c_read_register16(MAX98415_I2C_ADDRESS, MAX98415_REG_CLOCK_INTERRUPT_RAW, &status->clock_raw) &&
              i2c_read_register16(MAX98415_I2C_ADDRESS, MAX98415_REG_AMPLIFIER_FAULT_RAW, &status->amplifier_fault_raw) &&
              i2c_read_register16(MAX98415_I2C_ADDRESS, MAX98415_REG_AMP_A_THERMAL_RAW, &status->amp_a_thermal_raw) &&
              i2c_read_register16(MAX98415_I2C_ADDRESS, MAX98415_REG_AMP_B_THERMAL_RAW, &status->amp_b_thermal_raw) &&
              i2c_read_register16(MAX98415_I2C_ADDRESS, MAX98415_REG_SUPPLY_INTERRUPT_STATE, &status->supply_state) &&
              i2c_read_register16(MAX98415_I2C_ADDRESS, MAX98415_REG_POWER_INTERRUPT_STATE, &status->power_state) &&
              i2c_read_register16(MAX98415_I2C_ADDRESS, MAX98415_REG_CLOCK_INTERRUPT_STATE, &status->clock_state) &&
              i2c_read_register16(MAX98415_I2C_ADDRESS, MAX98415_REG_AMPLIFIER_FAULT_STATE, &status->amplifier_fault_state) &&
              i2c_read_register16(MAX98415_I2C_ADDRESS, MAX98415_REG_AMP_A_THERMAL_STATE, &status->amp_a_thermal_state) &&
              i2c_read_register16(MAX98415_I2C_ADDRESS, MAX98415_REG_AMP_B_THERMAL_STATE, &status->amp_b_thermal_state) &&
              i2c_read_register16(MAX98415_I2C_ADDRESS, MAX98415_REG_TONE_ENABLE, &tone_enable) &&
              i2c_read_register16(MAX98415_I2C_ADDRESS, MAX98415_REG_AMP_ENABLES, &amplifier_enables) &&
              i2c_read_register16(MAX98415_I2C_ADDRESS, MAX98415_REG_GLOBAL_ENABLE, &global_enable);
    if (!ok) return false;

    status->online = true;
    status->enabled = (global_enable & MAX98415_DEVICE_ENABLE) != 0;
    status->internal_tone_enabled = (tone_enable & MAX98415_TONE_ENABLE) != 0;
    status->amplifier_enables = amplifier_enables & MAX98415_ENABLE_BOTH_AMPLIFIERS;
    bool adc_active = status->enabled && (status->clock_raw & MAX98415_CLOCK_ERROR_MASK) == 0;
    status->pvdd_valid = adc_active && pvdd >= MAX98415_MINIMUM_PVDD_CODE;
    status->amp_a_temperature_valid = adc_active &&
                                      amp_a_temperature >= MAX98415_MINIMUM_TEMPERATURE_CODE &&
                                      amp_a_temperature <= MAX98415_MAXIMUM_TEMPERATURE_CODE;
    status->amp_b_temperature_valid = adc_active &&
                                      amp_b_temperature >= MAX98415_MINIMUM_TEMPERATURE_CODE &&
                                      amp_b_temperature <= MAX98415_MAXIMUM_TEMPERATURE_CODE;
    status->pvdd_raw = pvdd;
    status->amp_a_temperature_raw = amp_a_temperature;
    status->amp_b_temperature_raw = amp_b_temperature;
    status->pvdd_voltage = pvdd * 0.04399F;
    status->amp_a_temperature = (float)amp_a_temperature - 252.0F;
    status->amp_b_temperature = (float)amp_b_temperature - 252.0F;
    status->enable_failure_register = enable_failure_register;
    status->enable_failure_expected = enable_failure_expected;
    status->enable_failure_actual = enable_failure_actual;
    status->enable_failure_write_ok = enable_failure_write_ok;
    return true;
}

static void append(char *&output, size_t &remaining, const char *format, ...)
{
    if (remaining == 0) return;

    va_list args;
    va_start(args, format);
    int length = vsnprintf(output, remaining, format, args);
    va_end(args);

    if (length < 0) return;
    size_t used = (size_t)length < remaining ? (size_t)length : remaining - 1;
    output += used;
    remaining -= used;
}

static void append_reserved_fields(char *&output, size_t &remaining, uint8_t value, uint8_t named_mask)
{
    int bit = 7;
    while (bit >= 0)
    {
        if (named_mask & (1u << bit))
        {
            --bit;
            continue;
        }

        int msb = bit;
        while (bit >= 0 && !(named_mask & (1u << bit))) --bit;
        int lsb = bit + 1;
        uint8_t width = (uint8_t)(msb - lsb + 1);
        uint8_t field_value = (uint8_t)((value >> lsb) & ((1u << width) - 1u));
        if (msb == lsb) append(output, remaining, "  [%d]   %-42s %u\n", msb, "Reserved", field_value);
        else append(output, remaining, "  [%d:%d] %-42s 0x%X\n", msb, lsb, "Reserved", field_value);
    }
}

size_t max98415_register_count()
{
    return MAX98415_REGISTER_COUNT;
}

int max98415_format_register_value(size_t index, char *buffer, size_t length, bool *read_ok)
{
    if (!buffer || length == 0 || index >= MAX98415_REGISTER_COUNT) return 0;

    const max98415_register_descriptor_t &reg = register_map[index];
    uint8_t value = 0;
    bool ok = i2c_read_register16(MAX98415_I2C_ADDRESS, reg.address, &value);
    char separator = (index % 8) == 7 || index + 1 == MAX98415_REGISTER_COUNT ? '\n' : ' ';
    if (read_ok) *read_ok = ok;
    if (!ok) return snprintf(buffer, length, "%04X:??%c", reg.address, separator);
    return snprintf(buffer, length, "%04X:%02X%c", reg.address, value, separator);
}

int max98415_format_register(size_t index, char *buffer, size_t length, bool *read_ok)
{
    if (!buffer || length == 0 || index >= MAX98415_REGISTER_COUNT) return 0;

    const max98415_register_descriptor_t &reg = register_map[index];
    uint8_t value = 0;
    bool ok = i2c_read_register16(MAX98415_I2C_ADDRESS, reg.address, &value);
    if (read_ok) *read_ok = ok;

    char *output = buffer;
    size_t remaining = length;
    if (!ok)
    {
        append(output, remaining, "I2C read failed at register 0x%04X\n", reg.address);
        return (int)(output - buffer);
    }

    append(output, remaining, "0x%04X  0x%02X  %s\n", reg.address, value, reg.name);
    append(output, remaining, "  BIT   FIELD                                      VALUE\n");

    uint8_t named_mask = 0;
    const char *field = reg.fields;
    while (*field)
    {
        int msb = *field++ - '0';
        ++field;
        int lsb = *field++ - '0';
        ++field;
        const char *name = field;
        const char *end = strchr(field, ';');
        size_t name_length = end ? (size_t)(end - name) : strlen(name);
        uint8_t width = (uint8_t)(msb - lsb + 1);
        uint8_t mask = (uint8_t)(((1u << width) - 1u) << lsb);
        uint8_t field_value = (value & mask) >> lsb;
        named_mask |= mask;

        if (msb == lsb)
        {
            append(output, remaining, "  [%d]   %-42.*s %u\n", msb, (int)name_length, name, field_value);
        }
        else
        {
            append(output, remaining, "  [%d:%d] %-42.*s 0x%X\n", msb, lsb, (int)name_length, name, field_value);
        }
        field = end ? end + 1 : name + name_length;
    }

    append_reserved_fields(output, remaining, value, named_mask);
    append(output, remaining, "\n");
    return (int)(output - buffer);
}