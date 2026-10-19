/*******************************************************************
 * Copyright (C) 2026 DickinsAudio
 *
 * A2B I2C TUNNEL - byte level mailbox protocol, see tunnel_proto.h
 *
 * A write phase is collected into cmd[] and executed when it ends, which is
 * either the first read request (repeated start) or the STOP.  Read type ops
 * build their response into rsp[] at that point, and the read phase clocks it
 * out.  A read with nothing pending gets a fresh SUMMARY.
 *******************************************************************/

#include "tunnel_proto.h"
#include "vw5500.h"
#include <string.h>

static uint8_t  cmd[8 + TP_MAX_DATA];
static int      cmd_len;
static bool     cmd_overrun;
static bool     cmd_pending;                // Bytes collected and not yet executed

static uint8_t  rsp[8 + TP_MAX_DATA];
static int      rsp_len;
static int      rsp_idx;
static bool     rsp_valid;                  // A read op prepared a response

static tp_stats_t stats;

const tp_stats_t *tp_get_stats(void) { return &stats; }

uint8_t tp_crc8(const uint8_t *p, int len)
{
    uint8_t crc = 0;
    while (len--)
    {
        crc ^= *p++;
        for (int b=0; b<8; b++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint16_t get16(const uint8_t *p)   { return (uint16_t)(p[0] | (p[1] << 8)); }

static void build_summary(void)
{
    uint8_t l, a, t;
    vw_masks(&l, &a, &t);
    rsp[0] = TP_MAGIC; rsp[1] = TP_VERSION; rsp[2] = l; rsp[3] = a; rsp[4] = t;
    rsp[5] = tp_crc8(rsp, 5);
    rsp_len = 6;
}

static void build_status(uint8_t sn)
{
    vw_status_t st;
    vw_status(sn, &st);
    rsp[0] = sn;
    rsp[1] = st.sr;
    rsp[2] = st.flags;
    rsp[3] = st.cid;
    put16(&rsp[4],  st.port);
    put16(&rsp[6],  st.rx_next);
    put16(&rsp[8],  st.rx_free);
    put16(&rsp[10], st.tx_base);
    rsp[12] = st.tx_avail > 255 ? 255 : (uint8_t)st.tx_avail;
    rsp[13] = tp_crc8(rsp, 13);
    rsp_len = 14;
}

static void build_read(uint8_t sn, uint8_t cid, uint16_t ack, uint8_t maxlen)
{
    uint16_t off;
    uint8_t  cid_now;
    if (maxlen > TP_MAX_DATA) maxlen = TP_MAX_DATA;
    uint16_t n = vw_read(sn, cid, ack, &rsp[5], maxlen, &off, &cid_now);
    rsp[0] = sn;
    rsp[1] = cid_now;
    put16(&rsp[2], off);
    rsp[4] = (uint8_t)n;
    rsp[5+n] = tp_crc8(rsp, 5+n);
    rsp_len = 6+n;
}

// Execute the collected write phase
static void execute(void)
{
    cmd_pending = false;
    rsp_valid   = false;
    stats.transactions++;
    if (cmd_overrun) { stats.overruns++; return; }
    if (cmd_len < 2) { stats.bad_ops++; return; }
    if (tp_crc8(cmd, cmd_len-1) != cmd[cmd_len-1]) { stats.crc_errors++; return; }

    int      n  = cmd_len - 2;                  // Argument bytes between op and crc
    uint8_t *a  = &cmd[1];
    switch (cmd[0])
    {
        case TP_OP_SUMMARY:
            build_summary(); rsp_valid = true; break;

        case TP_OP_SOCK_STATUS:
            if (n != 1) goto bad;
            build_status(a[0]); rsp_valid = true; break;

        case TP_OP_CONNECT:
            if (n != 8) goto bad;
            vw_connect(a[0], a[1], &a[2], get16(&a[6])); break;

        case TP_OP_DATA:
            if (n < 4) goto bad;
            vw_data(a[0], a[1], get16(&a[2]), &a[4], (uint16_t)(n-4)); break;

        case TP_OP_READ:
            if (n != 5) goto bad;
            build_read(a[0], a[1], get16(&a[2]), a[4]); rsp_valid = true; break;

        case TP_OP_CLOSE:
            if (n != 2) goto bad;
            vw_close(a[0], a[1]); break;

        case TP_OP_RESET_ALL:
            if (n != 1 || a[0] != 0xA5) goto bad;
            vw_reset_all(); break;

        default:
        bad:
            stats.bad_ops++;
            break;
    }
    rsp_idx = 0;
}

void tp_rx(uint8_t b, bool first)
{
    if (first || !cmd_pending)
    {
        cmd_len = 0;
        cmd_overrun = false;
        cmd_pending = true;
    }
    if (cmd_len < (int)sizeof(cmd)) cmd[cmd_len++] = b;
    else cmd_overrun = true;
}

uint8_t tp_tx(void)
{
    if (cmd_pending) execute();                 // Repeated start after the write phase
    if (!rsp_valid)                             // Bare read - give the summary
    {
        build_summary();
        rsp_valid = true;
        rsp_idx = 0;
    }
    return rsp_idx < rsp_len ? rsp[rsp_idx++] : 0xFF;
}

void tp_stop(void)
{
    if (cmd_pending) execute();                 // Write only op, or write then STOP then read
    else rsp_valid = false;                     // A read phase has finished - response consumed
}

void tp_abort(void)
{
    cmd_pending = false;
    rsp_valid   = false;
    cmd_len     = 0;
}
