/*******************************************************************
 * Copyright (C) 2026 DickinsAudio
 *
 * VIRTUAL W5500 - see vw5500.h and PROTOCOL.md
 *
 * W5500 SPI frame:  [addr_hi][addr_lo][ctrl] [data...]
 *   ctrl = BSB[7:3] RWB[2] OM[1:0]
 *   BSB 0 = common registers, socket n: 4n+1 registers, 4n+2 TX buffer, 4n+3 RX buffer
 *
 * The ioLibrary always frames an access as CS select, 3 header bytes (byte or burst),
 * data (byte or burst, possibly in the same burst as the header), CS deselect.  We hold
 * back the real CS until the header is complete, then either replay it to the real chip
 * or service the frame here.
 *
 * Virtual socket model (TCP only carries data, UDP/MACRAW just pretend to work):
 *   TX: web server writes at TX_WR, SEND commits it (tx_commit). The PC reads from tx_rd
 *       and acknowledges, which frees space (TX_FSR).  Stream offset == pointer value.
 *   RX: the PC writes at rx_wr, the web server reads at RX_RD and RECV commits (rx_rd).
 *   A connection that the web server closes keeps its TX data "lingering" until the PC
 *   has drained it and sent CLOSE, while the socket itself reopens and listens.
 *******************************************************************/

#include "vw5500.h"
#include <string.h>
#include "wizchip_conf.h"

// Socket register offsets
#define SN_MR       0x00
#define SN_CR       0x01
#define SN_IR       0x02
#define SN_SR       0x03
#define SN_PORT     0x04
#define SN_DIPR     0x0C
#define SN_DPORT    0x10
#define SN_RXBUF    0x1E
#define SN_TXBUF    0x1F
#define SN_TX_FSR   0x20
#define SN_TX_RD    0x22
#define SN_TX_WR    0x24
#define SN_RX_RSR   0x26
#define SN_RX_RD    0x28
#define SN_RX_WR    0x2A
#define SN_REGS     0x30

// Commands, states and interrupt bits (names match the datasheet)
#define CMD_OPEN    0x01
#define CMD_LISTEN  0x02
#define CMD_CONNECT 0x04
#define CMD_DISCON  0x08
#define CMD_CLOSE   0x10
#define CMD_SEND    0x20
#define CMD_RECV    0x40

#define ST_CLOSED       0x00
#define ST_INIT         0x13
#define ST_LISTEN       0x14
#define ST_ESTABLISHED  0x17
#define ST_FIN_WAIT     0x18
#define ST_CLOSE_WAIT   0x1C
#define ST_UDP          0x22
#define ST_MACRAW       0x42

#define IR_CON      0x01
#define IR_DISCON   0x02
#define IR_RECV     0x04
#define IR_TIMEOUT  0x08
#define IR_SENDOK   0x10

// Common registers
#define C_MR        0x00
#define C_SIR       0x17
#define C_PHYCFGR   0x2E
#define C_VERSIONR  0x39
#define C_REGS      0x40

#define MASK        (VW_BUF_SIZE-1)

typedef struct
{
    uint8_t  reg[SN_REGS];          // Shadow of plain registers (MR, PORT, DIPR, TX_WR, RX_RD ...)
    uint8_t  sr;
    uint8_t  ir;
    uint16_t tx_rd;                 // Start of data not yet acknowledged by the PC
    uint16_t tx_commit;             // End of data committed by SEND
    uint16_t rx_wr;                 // End of data written by the PC
    uint16_t rx_rd;                 // Start of data not yet consumed (committed by RECV)
    uint8_t  cid;
    uint8_t  flags;                 // VW_FLAG_CONN_ACTIVE / LOCAL_FIN / PEER_FIN
    uint8_t *txbuf;                 // NULL if this socket carries no tunnel data
    uint8_t *rxbuf;
} vsock_t;

typedef struct
{
    void    (*cs_sel)(void);
    void    (*cs_desel)(void);
    uint8_t (*rd_byte)(void);
    void    (*wr_byte)(uint8_t);
    void    (*rd_burst)(uint8_t *, uint16_t);
    void    (*wr_burst)(uint8_t *, uint16_t);
} real_if_t;

enum { ROUTE_NONE, ROUTE_REAL, ROUTE_VIRTUAL };

static vsock_t    vs[VW_SOCKETS];
static uint8_t    creg[C_REGS];
static uint8_t    bufmem[VW_MAX_BUFFERED][2][VW_BUF_SIZE];
static real_if_t  real;
static bool       installed, real_present;
static uint8_t    virtual_mask;
static vw_stats_t stats;

static struct
{
    uint8_t  hdr[3];
    uint8_t  n;
    uint8_t  route;
    uint8_t  bsb;
    uint16_t addr;
} fr;

static inline uint16_t rd16(const uint8_t *p)        { return (uint16_t)((p[0]<<8) | p[1]); }
static inline void     wr16(uint8_t *p, uint16_t v)  { p[0] = (uint8_t)(v>>8); p[1] = (uint8_t)v; }

static inline bool is_tcp_live(const vsock_t *s)     { return s->sr == ST_ESTABLISHED || s->sr == ST_CLOSE_WAIT; }


////////////////////////////////////////////////////////////////////////////////
// SOCKET ENGINE (called with the lock held)

static void reset_tx(vsock_t *s)
{
    s->tx_rd = s->tx_commit = 0;
    wr16(&s->reg[SN_TX_WR], 0);
}

static void reset_rx(vsock_t *s)
{
    s->rx_rd = s->rx_wr = 0;
    wr16(&s->reg[SN_RX_RD], 0);
}

static void release(vsock_t *s)                         // Connection is finished on both sides
{
    s->flags = 0;
    if (s->sr == ST_FIN_WAIT) s->sr = ST_CLOSED;
    if (!is_tcp_live(s)) reset_tx(s);
}

static void command(vsock_t *s, uint8_t cmd)
{
    switch (cmd)
    {
        case CMD_OPEN:
            switch (s->reg[SN_MR] & 0x0F)
            {
                case 0x01: s->sr = ST_INIT;   break;
                case 0x02: s->sr = ST_UDP;    break;
                case 0x04: s->sr = ST_MACRAW; break;
                default:   s->sr = ST_CLOSED; break;
            }
            s->ir = 0;
            if (s->flags & VW_FLAG_CONN_ACTIVE)             // Lingering: keep the stream positions the PC
            {                                               // sees, drop unread RX, keep TX for draining
                s->rx_rd = s->rx_wr;
                wr16(&s->reg[SN_RX_RD], s->rx_wr);
            }
            else
            {
                reset_rx(s);
                reset_tx(s);
            }
            break;

        case CMD_LISTEN:
            if (s->sr == ST_INIT) s->sr = ST_LISTEN;
            break;

        case CMD_CONNECT:                                   // Outbound TCP is not supported over the tunnel
            if (s->sr == ST_INIT) { s->sr = ST_CLOSED; s->ir |= IR_TIMEOUT; }
            break;

        case CMD_DISCON:
            if (s->flags & VW_FLAG_CONN_ACTIVE)
            {
                s->flags |= VW_FLAG_LOCAL_FIN;
                if (s->flags & VW_FLAG_PEER_FIN) { s->sr = ST_CLOSED; release(s); }
                else                               s->sr = ST_FIN_WAIT;
            }
            else s->sr = ST_CLOSED;
            s->ir |= IR_DISCON;
            break;

        case CMD_CLOSE:
            s->sr = ST_CLOSED;
            if (s->flags & VW_FLAG_CONN_ACTIVE)
            {
                s->flags |= VW_FLAG_LOCAL_FIN;
                if (s->flags & VW_FLAG_PEER_FIN) release(s);      // Otherwise linger until the PC drains and closes
            }
            s->ir = 0;
            break;

        case CMD_SEND:
            if (s->txbuf && s->sr == ST_ESTABLISHED && !(s->flags & VW_FLAG_PEER_FIN))
                s->tx_commit = rd16(&s->reg[SN_TX_WR]);
            else
                s->tx_rd = s->tx_commit = rd16(&s->reg[SN_TX_WR]);    // Nowhere to go - pretend it was sent
            s->ir |= IR_SENDOK;
            break;

        case CMD_RECV:
            s->rx_rd = rd16(&s->reg[SN_RX_RD]);
            break;

        default:                                            // SEND_MAC, SEND_KEEP ...
            s->ir |= IR_SENDOK;
            break;
    }
}

static uint8_t sock_reg_read(vsock_t *s, uint16_t a)
{
    if (a >= SN_REGS) return 0;
    uint16_t v;
    switch (a)
    {
        case SN_CR:         return 0;                       // Commands complete instantly
        case SN_IR:         return s->ir;
        case SN_SR:         return s->sr;
        case SN_TX_FSR:
        case SN_TX_FSR+1:   v = (uint16_t)(VW_BUF_SIZE - (uint16_t)(s->tx_commit - s->tx_rd)); break;
        case SN_TX_RD:
        case SN_TX_RD+1:    v = s->tx_rd; break;
        case SN_RX_RSR:
        case SN_RX_RSR+1:   v = (uint16_t)(s->rx_wr - s->rx_rd); break;
        case SN_RX_WR:
        case SN_RX_WR+1:    v = s->rx_wr; break;
        default:            return s->reg[a];
    }
    return (a & 1) ? (uint8_t)v : (uint8_t)(v >> 8);       // All 16 bit registers start at even offsets
}

static void sock_reg_write(vsock_t *s, uint16_t a, uint8_t v)
{
    if (a >= SN_REGS) return;
    switch (a)
    {
        case SN_CR:         command(s, v);   break;
        case SN_IR:         s->ir &= ~v;     break;         // Write 1 to clear
        case SN_SR:
        case SN_TX_FSR: case SN_TX_FSR+1:
        case SN_TX_RD:  case SN_TX_RD+1:
        case SN_RX_RSR: case SN_RX_RSR+1:
        case SN_RX_WR:  case SN_RX_WR+1:     break;         // Read only
        default:            s->reg[a] = v;   break;
    }
}

static void common_reset(void)
{
    memset(creg, 0, sizeof(creg));
    creg[0x19] = 0x07; creg[0x1A] = 0xD0;                  // RTR 2000
    creg[0x1B] = 0x08;                                      // RCR
}

static uint8_t common_read(uint16_t a)
{
    if (a >= C_REGS) return 0;
    switch (a)
    {
        case C_MR:       return creg[C_MR] & 0x7F;          // RST self clears
        case C_VERSIONR: return 0x04;
        case C_PHYCFGR:  return 0xBF;                       // Link up, 100M full duplex
        case C_SIR:
        {
            uint8_t sir = 0;
            for (int n=0; n<VW_SOCKETS; n++) if (vs[n].ir) sir |= (uint8_t)(1u<<n);
            return sir;
        }
        default:         return creg[a];
    }
}

static void common_write(uint16_t a, uint8_t v)
{
    if (a >= C_REGS || a == C_VERSIONR || a == C_SIR) return;
    if (a == C_MR && (v & 0x80)) { common_reset(); return; }
    creg[a] = v;
}

// One byte of a virtual frame
static uint8_t access(uint8_t bsb, uint16_t addr, bool write, uint8_t v)
{
    if (bsb == 0)
    {
        if (write) common_write(addr, v);
        return write ? 0 : common_read(addr);
    }
    vsock_t *s = &vs[(bsb-1) >> 2];
    switch ((bsb-1) & 3)
    {
        case 0:
            if (write) { sock_reg_write(s, addr, v); return 0; }
            return sock_reg_read(s, addr);
        case 1:                                             // TX buffer
            if (!s->txbuf) return 0;
            if (write) { s->txbuf[addr & MASK] = v; return 0; }
            return s->txbuf[addr & MASK];
        case 2:                                             // RX buffer
            if (!s->rxbuf) return 0;
            if (write) { s->rxbuf[addr & MASK] = v; return 0; }
            return s->rxbuf[addr & MASK];
    }
    return 0;
}


////////////////////////////////////////////////////////////////////////////////
// SPI FRAME MUX - these replace the WIZCHIP callbacks

static void real_write(uint8_t *p, uint16_t len)
{
    if (real.wr_burst) real.wr_burst(p, len);
    else while (len--) real.wr_byte(*p++);
}

static void real_read(uint8_t *p, uint16_t len)
{
    if (real.rd_burst) real.rd_burst(p, len);
    else while (len--) *p++ = real.rd_byte();
}

static void mux_select(void)
{
    fr.n = 0;
    fr.route = ROUTE_NONE;
}

static void mux_deselect(void)
{
    if (fr.route == ROUTE_REAL) real.cs_desel();
    fr.route = ROUTE_NONE;
    fr.n = 0;
}

static void decide_route(void)
{
    fr.addr = (uint16_t)((fr.hdr[0] << 8) | fr.hdr[1]);
    fr.bsb  = fr.hdr[2] >> 3;
    bool virt = !real_present;
    if (!virt && fr.bsb >= 1 && fr.bsb <= 31)
    {
        int sn = (fr.bsb-1) >> 2;
        virt = (virtual_mask >> sn) & 1;
    }
    if (virt)
    {
        fr.route = ROUTE_VIRTUAL;
        stats.frames_virtual++;
    }
    else
    {
        fr.route = ROUTE_REAL;
        stats.frames_real++;
        real.cs_sel();
        real_write(fr.hdr, 3);
    }
}

static void virtual_data(uint8_t *p, uint16_t len, bool write)
{
    vw_platform_lock();
    while (len--)
    {
        uint8_t v = access(fr.bsb, fr.addr++, write, write ? *p : 0);
        if (!write) *p = v;
        p++;
    }
    vw_platform_unlock();
}

static void mux_write_burst(uint8_t *p, uint16_t len)
{
    while (len && fr.n < 3)                                  // Gather the header
    {
        fr.hdr[fr.n++] = *p++;
        len--;
        if (fr.n == 3) decide_route();
    }
    if (!len) return;
    if (fr.route == ROUTE_REAL)         real_write(p, len);
    else if (fr.route == ROUTE_VIRTUAL) virtual_data(p, len, true);
}

static void mux_read_burst(uint8_t *p, uint16_t len)
{
    if (fr.route == ROUTE_REAL)         real_read(p, len);
    else if (fr.route == ROUTE_VIRTUAL) virtual_data(p, len, false);
    else memset(p, 0, len);
}

static void    mux_write_byte(uint8_t b) { mux_write_burst(&b, 1); }
static uint8_t mux_read_byte(void)       { uint8_t b; mux_read_burst(&b, 1); return b; }


////////////////////////////////////////////////////////////////////////////////
// INSTALL

void vw_install(uint8_t mask, bool present)
{
    real.cs_sel   = WIZCHIP.CS._select;
    real.cs_desel = WIZCHIP.CS._deselect;
    real.rd_byte  = WIZCHIP.IF.SPI._read_byte;
    real.wr_byte  = WIZCHIP.IF.SPI._write_byte;
    real.rd_burst = WIZCHIP.IF.SPI._read_burst;
    real.wr_burst = WIZCHIP.IF.SPI._write_burst;

    real_present = present;
    virtual_mask = present ? mask : 0xFF;
    memset(vs, 0, sizeof(vs));
    common_reset();

    int slot = 0;
    for (int n=0; n<VW_SOCKETS; n++)
    {
        vs[n].reg[SN_RXBUF] = vs[n].reg[SN_TXBUF] = 2;
        vs[n].reg[0x12] = 0x05; vs[n].reg[0x13] = 0xB4;     // MSSR 1460
        vs[n].reg[0x16] = 0x80;                             // TTL
        if (((mask >> n) & 1) && slot < VW_MAX_BUFFERED)
        {
            vs[n].txbuf = bufmem[slot][0];
            vs[n].rxbuf = bufmem[slot][1];
            slot++;
        }
    }

    reg_wizchip_cs_cbfunc(mux_select, mux_deselect);
    reg_wizchip_spi_cbfunc(mux_read_byte, mux_write_byte);
    reg_wizchip_spiburst_cbfunc(mux_read_burst, mux_write_burst);
    installed = true;
}

bool    vw_installed(void)    { return installed; }
bool    vw_real_present(void) { return real_present; }
uint8_t vw_virtual_mask(void) { return virtual_mask; }
const vw_stats_t *vw_get_stats(void) { return &stats; }


////////////////////////////////////////////////////////////////////////////////
// TUNNEL OPERATIONS

static vsock_t *tunnel_sock(uint8_t sn)
{
    if (!installed || sn >= VW_SOCKETS || !vs[sn].txbuf) return 0;
    return &vs[sn];
}

static bool listen_ready(const vsock_t *s)
{
    return s->sr == ST_LISTEN && !(s->flags & VW_FLAG_CONN_ACTIVE);
}

static bool owns(const vsock_t *s, uint8_t cid)
{
    return (s->flags & VW_FLAG_CONN_ACTIVE) && s->cid == cid;
}

void vw_status(uint8_t sn, vw_status_t *st)
{
    memset(st, 0, sizeof(*st));
    vsock_t *s = tunnel_sock(sn);
    if (!s) return;
    vw_platform_lock();
    st->sr       = s->sr;
    st->flags    = (uint8_t)(s->flags | VW_FLAG_VIRTUAL | (listen_ready(s) ? VW_FLAG_LISTEN_READY : 0));
    st->cid      = s->cid;
    st->port     = rd16(&s->reg[SN_PORT]);
    st->rx_next  = s->rx_wr;
    st->rx_free  = (uint16_t)(VW_BUF_SIZE - (uint16_t)(s->rx_wr - s->rx_rd));
    st->tx_base  = s->tx_rd;
    st->tx_avail = (s->flags & VW_FLAG_CONN_ACTIVE) ? (uint16_t)(s->tx_commit - s->tx_rd) : 0;
    vw_platform_unlock();
}

void vw_masks(uint8_t *listen_mask, uint8_t *active_mask, uint8_t *attn_mask)
{
    uint8_t l = 0, a = 0, t = 0;
    vw_platform_lock();
    for (int n=0; n<VW_SOCKETS; n++)
    {
        vsock_t *s = &vs[n];
        if (!installed || !s->txbuf) continue;
        if (listen_ready(s)) l |= (uint8_t)(1u<<n);
        if (s->flags & VW_FLAG_CONN_ACTIVE)
        {
            a |= (uint8_t)(1u<<n);
            if (s->tx_commit != s->tx_rd || (s->flags & VW_FLAG_LOCAL_FIN)) t |= (uint8_t)(1u<<n);
        }
    }
    vw_platform_unlock();
    *listen_mask = l; *active_mask = a; *attn_mask = t;
}

void vw_connect(uint8_t sn, uint8_t cid, const uint8_t ip[4], uint16_t port)
{
    vsock_t *s = tunnel_sock(sn);
    if (!s || cid == 0) return;
    vw_platform_lock();
    if (listen_ready(s))
    {
        s->flags = VW_FLAG_CONN_ACTIVE;
        s->cid   = cid;
        s->sr    = ST_ESTABLISHED;
        s->ir   |= IR_CON;
        memcpy(&s->reg[SN_DIPR], ip, 4);
        wr16(&s->reg[SN_DPORT], port);
        reset_tx(s);
        reset_rx(s);
        stats.connects++;
    }
    vw_platform_unlock();
}

uint16_t vw_data(uint8_t sn, uint8_t cid, uint16_t off, const uint8_t *data, uint16_t len)
{
    vsock_t *s = tunnel_sock(sn);
    if (!s) return 0;
    uint16_t n = 0;
    vw_platform_lock();
    if (owns(s, cid) && s->sr == ST_ESTABLISHED && !(s->flags & VW_FLAG_PEER_FIN))
    {
        uint16_t skip = (uint16_t)(s->rx_wr - off);         // Bytes we already have
        if (skip <= len)
        {
            uint16_t space = (uint16_t)(VW_BUF_SIZE - (uint16_t)(s->rx_wr - s->rx_rd));
            n = (uint16_t)(len - skip);
            if (n > space) n = space;
            for (uint16_t i=0; i<n; i++) s->rxbuf[(uint16_t)(s->rx_wr + i) & MASK] = data[skip + i];
            s->rx_wr = (uint16_t)(s->rx_wr + n);
            if (n) s->ir |= IR_RECV;
            stats.rx_bytes += n;
        }
    }
    vw_platform_unlock();
    return n;
}

uint16_t vw_read(uint8_t sn, uint8_t cid, uint16_t ack, uint8_t *out, uint16_t maxlen, uint16_t *off, uint8_t *cid_out)
{
    vsock_t *s = tunnel_sock(sn);
    *off = 0; *cid_out = 0;
    if (!s) return 0;
    uint16_t n = 0;
    vw_platform_lock();
    *cid_out = (s->flags & VW_FLAG_CONN_ACTIVE) ? s->cid : 0;
    if (owns(s, cid))
    {
        uint16_t pending = (uint16_t)(s->tx_commit - s->tx_rd);
        uint16_t acked   = (uint16_t)(ack - s->tx_rd);
        if (acked <= pending) { s->tx_rd = ack; pending = (uint16_t)(pending - acked); stats.tx_bytes += acked; }
        n = pending < maxlen ? pending : maxlen;
        for (uint16_t i=0; i<n; i++) out[i] = s->txbuf[(uint16_t)(s->tx_rd + i) & MASK];
        *off = s->tx_rd;
    }
    vw_platform_unlock();
    return n;
}

static void peer_close(vsock_t *s)
{
    if (s->flags & VW_FLAG_LOCAL_FIN) { release(s); return; }
    s->flags |= VW_FLAG_PEER_FIN;
    s->tx_rd  = s->tx_commit;                               // Nobody left to read it
    if (s->sr == ST_ESTABLISHED) s->sr = ST_CLOSE_WAIT;
    s->ir |= IR_DISCON;
}

void vw_close(uint8_t sn, uint8_t cid)
{
    vsock_t *s = tunnel_sock(sn);
    if (!s) return;
    vw_platform_lock();
    if (owns(s, cid)) peer_close(s);
    vw_platform_unlock();
}

void vw_reset_all(void)
{
    vw_platform_lock();
    for (int n=0; n<VW_SOCKETS; n++)
        if (installed && vs[n].txbuf && (vs[n].flags & VW_FLAG_CONN_ACTIVE)) peer_close(&vs[n]);
    vw_platform_unlock();
}
