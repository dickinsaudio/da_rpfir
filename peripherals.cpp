#include <stdio.h>
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PICO_I2C_SCL_PIN 11
#define PICO_I2C_SDA_PIN 10
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
    if (!i2c_initialized) i2c_initialize();
    uint8_t ret=0;
    absolute_time_t t = make_timeout_time_ms(10);
    i2c_write_blocking_until(I2C_ID, addr, &reg, 1, true,  t);
    t = make_timeout_time_ms(10);
    i2c_read_blocking_until(I2C_ID,  addr, &ret, 1, false, t);
    return ret;
}

void i2c_write(uint8_t addr, uint8_t reg, uint8_t data)
{
    if (!i2c_initialized) i2c_initialize();
    uint8_t msg[2] = {reg, data};
    absolute_time_t t = make_timeout_time_ms(10);
    i2c_write_blocking_until(I2C_ID, addr, msg, 2, false, t);
}

void i2c_scan(uint8_t status)
{
    printf("I2C Bus Scan\n");
    printf("   00  01  02  03  04  05  06  07  08  09  0A  0B  0C  0D  0E  0F\n");

    for (int addr = 0; addr < 128; ++addr)
    {
        if (addr % 16 == 0)
        {
            printf("%02x ", addr);
        }
        uint8_t val = i2c_read(addr, status);
        printf("%02x",val);
        printf(addr % 16 == 15 ? "\n" : "  ");
    }
    printf("\n");
}

#ifdef __cplusplus
}
#endif
