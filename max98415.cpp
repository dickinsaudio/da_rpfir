#include "max98415.hpp"

#include "peripherals.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#define MAX98415_I2C_ADDRESS 0x39

struct max98415_register_descriptor_t
{
    uint16_t address;
    const char *name;
    const char *fields;
};

#include "max98415_register_map.inc"

static_assert(sizeof(register_map) / sizeof(register_map[0]) == MAX98415_REGISTER_COUNT);

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