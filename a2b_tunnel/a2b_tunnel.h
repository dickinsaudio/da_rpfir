/*******************************************************************
 * Copyright (C) 2026 DickinsAudio
 *
 * A2B I2C WEB TUNNEL - project facing API
 *
 * Usage (w5500 startup):
 *     wizchip_spi_initialize(); wizchip_reset();
 *     if (!a2b_tunnel_w5500_present()) { a2b_tunnel_start(false); return; }   // Tunnel only
 *     ... normal W5500 bring up ...
 *     a2b_tunnel_start(true);                                                 // Both
 *
 * The RP2350 I2C0 (shared with the amplifier) becomes a slave at
 * A2B_TUNNEL_I2C_ADDR and only turns master for the duration of each
 * amplifier transaction (see i2c_master_acquire / i2c_master_release).
 *******************************************************************/

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifndef A2B_TUNNEL_I2C_ADDR
#define A2B_TUNNEL_I2C_ADDR         0x2A
#endif

// Sockets carried by the tunnel when a real W5500 is fitted. The web server
// listens on sockets 1..3, so socket 3 serves the tunnel and 1..2 Ethernet.
#ifndef A2B_TUNNEL_SOCKETS_WITH_W5500
#define A2B_TUNNEL_SOCKETS_WITH_W5500   (1u<<3)
#endif
// With no W5500 every web server socket goes over the tunnel
#ifndef A2B_TUNNEL_SOCKETS_ONLY
#define A2B_TUNNEL_SOCKETS_ONLY         ((1u<<1) | (1u<<2) | (1u<<3))
#endif

#ifdef __cplusplus
extern "C" {
#endif

bool a2b_tunnel_w5500_present(void);        // Probe VERSIONR on the real SPI bus (after wizchip_spi_initialize/reset)
void a2b_tunnel_start(bool w5500_present);  // Install the virtual chip and start the I2C slave
bool a2b_tunnel_only(void);                 // True if running without a W5500 (skip DHCP etc.)
int  a2b_tunnel_stats_text(char *buf, int len);

// Hooks called by peripherals.cpp around every I2C master transaction (weak no-ops there)
void i2c_master_acquire(void);
void i2c_master_release(void);

#ifdef __cplusplus
}
#endif
