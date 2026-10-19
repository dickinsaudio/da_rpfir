/*******************************************************************
 * Copyright (C) 2026 DickinsAudio
 *
 * VIRTUAL W5500
 *
 * Sits behind the WIZnet ioLibrary SPI/CS callbacks and pretends to be
 * (some or all of) a W5500.  Each SPI frame is decoded from its 3 byte
 * header and routed either to the real chip or to a virtual socket whose
 * buffers are filled and drained by the A2B I2C tunnel.  The web server
 * and the rest of the ioLibrary are unchanged - they still think they are
 * talking to a W5500.
 *
 * Portable C (no Pico SDK) so it also builds natively for testing.
 *******************************************************************/

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VW_SOCKETS      8                   // W5500 socket count
#define VW_BUF_SIZE     2048                // Per socket RX and TX buffer (power of 2)
#define VW_MAX_BUFFERED 4                   // Sockets that can carry tunnel TCP traffic

// Install the virtual chip in place of the currently registered WIZCHIP callbacks.
//  virtual_mask : bit n = socket n is routed to the tunnel (at most VW_MAX_BUFFERED of them get buffers)
//  real_present : false = no W5500 fitted, common registers and all sockets are virtual
// Call after the real callbacks are registered (i.e. after wizchip_initialize()) if real_present.
void    vw_install(uint8_t virtual_mask, bool real_present);
bool    vw_installed(void);
bool    vw_real_present(void);
uint8_t vw_virtual_mask(void);

// Tunnel side operations (see PROTOCOL.md).  Safe to call from an ISR.
#define VW_FLAG_CONN_ACTIVE   0x01
#define VW_FLAG_LOCAL_FIN     0x02
#define VW_FLAG_PEER_FIN      0x04
#define VW_FLAG_LISTEN_READY  0x08
#define VW_FLAG_VIRTUAL       0x80

typedef struct
{
    uint8_t  sr;
    uint8_t  flags;
    uint8_t  cid;
    uint16_t port;
    uint16_t rx_next;
    uint16_t rx_free;
    uint16_t tx_base;
    uint16_t tx_avail;
} vw_status_t;

void     vw_status(uint8_t sn, vw_status_t *st);
void     vw_masks(uint8_t *listen_mask, uint8_t *active_mask, uint8_t *attn_mask);
void     vw_connect(uint8_t sn, uint8_t cid, const uint8_t ip[4], uint16_t port);
uint16_t vw_data(uint8_t sn, uint8_t cid, uint16_t off, const uint8_t *data, uint16_t len);   // Returns bytes accepted
uint16_t vw_read(uint8_t sn, uint8_t cid, uint16_t ack, uint8_t *out, uint16_t maxlen, uint16_t *off, uint8_t *cid_out);
void     vw_close(uint8_t sn, uint8_t cid);
void     vw_reset_all(void);

// Statistics for a debug page
typedef struct
{
    uint32_t frames_real;
    uint32_t frames_virtual;
    uint32_t connects;
    uint32_t rx_bytes;              // PC -> device
    uint32_t tx_bytes;              // device -> PC
} vw_stats_t;
const vw_stats_t *vw_get_stats(void);

// Provided by the platform: a short critical section safe against the tunnel ISR (not nested)
void vw_platform_lock(void);
void vw_platform_unlock(void);

#ifdef __cplusplus
}
#endif
