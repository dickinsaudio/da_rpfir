
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif


void send_rgb(uint8_t r, uint8_t g, uint8_t b); // Log scale intensity from 0 - 15


uint8_t i2c_read(uint8_t addr, uint8_t reg);
bool    i2c_read_register(uint8_t addr, uint8_t reg, uint8_t *data);
bool    i2c_read_register16(uint8_t addr, uint16_t reg, uint8_t *data);
bool    i2c_write_register16(uint8_t addr, uint16_t reg, uint8_t data);
void    i2c_write(uint8_t addr, uint8_t reg, uint8_t data);
int     i2c_scan(uint8_t *addresses, int max_addresses);

#ifdef __cplusplus
}
#endif