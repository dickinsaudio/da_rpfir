#include <stdio.h>
#include "peripherals.hpp"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "peripherals.pio.h"


#ifdef __cplusplus
extern "C" {
#endif



#ifdef PICO_RGB_PIN
#define RGB_PIO pio1
#define RGB_SM 1
#define RGB_SPEED 800000

static bool led_initialized;

void send_led(uint32_t color)
{
    if (!led_initialized)
    {
        led_initialized = true;
        int offset = pio_add_program(RGB_PIO, &ws2812_program);
        ws2812_program_init(RGB_PIO, RGB_SM, offset, PICO_RGB_PIN, RGB_SPEED, false);
    }
    pio_sm_put_blocking(RGB_PIO, RGB_SM, color<<8);
}

static uint8_t led_scale[16] = {  0, 1, 2, 3, 4, 6, 9, 13, 19, 28, 40, 58, 84, 122, 176, 255 };

void send_rgb_raw(uint8_t r, uint8_t g, uint8_t b)
{
    send_led((g<<16) | (r<<8) | b);
}

void send_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    send_rgb_raw(led_scale[r&0x0F], led_scale[g&0x0F], led_scale[b&0x0F]);
}
#else
void send_rgb(uint8_t r, uint8_t g, uint8_t b) {};
#endif



//#define PICO_I2C_SCL_PIN 23
//#define PICO_I2C_SDA_PIN 22
#define I2C_ID i2c1
#define I2C_SPEED 100000 //100KHz

static bool i2c_initialized; 

void i2c_initialize()
{
    i2c_initialized = true;
    /* uint32_t baud = */i2c_init(I2C_ID, I2C_SPEED);
    gpio_set_function(PICO_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PICO_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(PICO_I2C_SCL_PIN);
    gpio_pull_up(PICO_I2C_SDA_PIN);
}


uint8_t i2c_read(uint8_t addr, uint8_t reg)
{
    uint8_t ret=0;
    i2c_read_register(addr, reg, &ret);
    return ret;
}

bool i2c_read_register(uint8_t addr, uint8_t reg, uint8_t *data)
{
    if (!data) return false;
    if (!i2c_initialized) i2c_initialize();

    absolute_time_t t = make_timeout_time_ms(10);
    if (i2c_write_blocking_until(I2C_ID, addr, &reg, 1, true, t) != 1)
    {
        return false;
    }
    t = make_timeout_time_ms(10);
    return i2c_read_blocking_until(I2C_ID, addr, data, 1, false, t) == 1;
}

bool i2c_read_register16(uint8_t addr, uint16_t reg, uint8_t *data)
{
    if (!data) return false;
    if (!i2c_initialized) i2c_initialize();

    uint8_t address[2] = {(uint8_t)(reg >> 8), (uint8_t)reg};
    absolute_time_t timeout = make_timeout_time_ms(10);
    if (i2c_write_blocking_until(I2C_ID, addr, address, sizeof(address), true, timeout) != sizeof(address))
    {
        return false;
    }
    timeout = make_timeout_time_ms(10);
    return i2c_read_blocking_until(I2C_ID, addr, data, 1, false, timeout) == 1;
}

void i2c_write(uint8_t addr, uint8_t reg, uint8_t data)
{
    if (!i2c_initialized) i2c_initialize();
    uint8_t msg[2] = {reg, data};
    absolute_time_t t = make_timeout_time_ms(10);
    i2c_write_blocking_until(I2C_ID, addr, msg, 2, false, t);
}

int i2c_scan(uint8_t *addresses, int max_addresses)
{
    if (!addresses || max_addresses <= 0) return 0;
    if (!i2c_initialized) i2c_initialize();

    int count = 0;
    for (uint8_t addr = 0x08; addr <= 0x77 && count < max_addresses; ++addr)
    {
        uint8_t value;
        absolute_time_t timeout = make_timeout_time_ms(5);
        if (i2c_read_blocking_until(I2C_ID, addr, &value, 1, false, timeout) >= 0)
        {
            addresses[count++] = addr;
        }
    }
    return count;
}

#ifdef __cplusplus
}
#endif
