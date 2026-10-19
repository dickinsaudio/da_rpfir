/*******************************************************************
 * Copyright (C) 2026 DickinsAudio
 *
 * A2B I2C TUNNEL - byte level mailbox protocol (PROTOCOL.md)
 *
 * Driven by an I2C slave driver (ISR) or by the native test harness:
 *   tp_rx(byte, first)  each byte the master writes, first = first byte after the address
 *   tp_tx()             each byte the master reads
 *   tp_stop()           STOP seen (end of transaction)
 *   tp_abort()          transaction aborted (bus error, mode switch)
 *******************************************************************/

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TP_MAGIC        0xA2
#define TP_VERSION      0x01
#define TP_MAX_DATA     240

#define TP_OP_SUMMARY       0x01
#define TP_OP_SOCK_STATUS   0x02
#define TP_OP_CONNECT       0x10
#define TP_OP_DATA          0x11
#define TP_OP_READ          0x12
#define TP_OP_CLOSE         0x13
#define TP_OP_RESET_ALL     0x1F

void    tp_rx(uint8_t b, bool first);
uint8_t tp_tx(void);
void    tp_stop(void);
void    tp_abort(void);

uint8_t tp_crc8(const uint8_t *p, int len);

typedef struct
{
    uint32_t transactions;
    uint32_t crc_errors;
    uint32_t bad_ops;
    uint32_t overruns;
} tp_stats_t;
const tp_stats_t *tp_get_stats(void);

#ifdef __cplusplus
}
#endif
