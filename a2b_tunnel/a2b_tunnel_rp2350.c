/*******************************************************************
 * Copyright (C) 2026 DickinsAudio
 *
 * A2B I2C WEB TUNNEL - RP2350 platform layer
 *
 *  - I2C0 slave driver feeding tunnel_proto (ISR on the core that starts it)
 *  - Dual role: slave by default, master only around amplifier transactions
 *  - W5500 probe and virtual chip install
 *******************************************************************/

#include "a2b_tunnel.h"
#include "tunnel_proto.h"
#include "vw5500.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

#include "wizchip_conf.h"
#include "wizchip_spi.h"
#include "hardware/spi.h"
#ifdef USE_PIO
#include "wizchip_qspi_pio.h"
#endif

#define TUNNEL_I2C      i2c0
#define TUNNEL_IRQ      I2C0_IRQ

#define SLAVE_INTR_MASK (I2C_IC_INTR_MASK_M_RX_FULL_BITS  | \
                         I2C_IC_INTR_MASK_M_RD_REQ_BITS   | \
                         I2C_IC_INTR_MASK_M_STOP_DET_BITS | \
                         I2C_IC_INTR_MASK_M_TX_ABRT_BITS)

static volatile bool slave_on;
static bool          tunnel_only;
static uint32_t      master_switches;
static uint32_t      master_wait_timeouts;


////////////////////////////////////////////////////////////////////////////////
// LOCK FOR THE VIRTUAL CHIP

static spin_lock_t *vw_lock;
static uint32_t     vw_irq_save;

void vw_platform_lock(void)
{
    uint32_t save = spin_lock_blocking(vw_lock);
    vw_irq_save = save;
}

void vw_platform_unlock(void)
{
    spin_unlock(vw_lock, vw_irq_save);
}


////////////////////////////////////////////////////////////////////////////////
// I2C SLAVE

static void drain_rx(i2c_hw_t *hw)
{
    while (hw->rxflr)
    {
        uint32_t d = hw->data_cmd;
        tp_rx((uint8_t)d, (d & I2C_IC_DATA_CMD_FIRST_DATA_BYTE_BITS) != 0);
    }
}

static void __isr tunnel_i2c_irq(void)
{
    i2c_hw_t *hw = i2c_get_hw(TUNNEL_I2C);
    uint32_t st = hw->intr_stat;

    if (st & I2C_IC_INTR_STAT_R_TX_ABRT_BITS) (void)hw->clr_tx_abrt;
    drain_rx(hw);
    if (st & I2C_IC_INTR_STAT_R_RD_REQ_BITS)
    {
        (void)hw->clr_rd_req;
        hw->data_cmd = tp_tx();
    }
    if (st & I2C_IC_INTR_STAT_R_STOP_DET_BITS)
    {
        (void)hw->clr_stop_det;
        drain_rx(hw);
        tp_stop();
    }
}

static void slave_enable(void)
{
    i2c_hw_t *hw = i2c_get_hw(TUNNEL_I2C);
    i2c_set_slave_mode(TUNNEL_I2C, true, A2B_TUNNEL_I2C_ADDR);
    (void)hw->clr_intr;
    hw->intr_mask = SLAVE_INTR_MASK;            // Masked at the block, so this is safe from either core
}

// Strong versions of the weak hooks in peripherals.cpp. Called with the i2c mutex held.
void i2c_master_acquire(void)
{
    if (!slave_on) return;
    i2c_hw_t *hw = i2c_get_hw(TUNNEL_I2C);
    absolute_time_t t = make_timeout_time_us(3000);           // Let any remote transaction finish
    while ((hw->status & I2C_IC_STATUS_SLV_ACTIVITY_BITS) && !time_reached(t)) tight_loop_contents();
    if (hw->status & I2C_IC_STATUS_SLV_ACTIVITY_BITS) master_wait_timeouts++;
    hw->intr_mask = 0;
    i2c_set_slave_mode(TUNNEL_I2C, false, A2B_TUNNEL_I2C_ADDR);
    tp_abort();
    master_switches++;
}

void i2c_master_release(void)
{
    if (!slave_on) return;
    slave_enable();
}


////////////////////////////////////////////////////////////////////////////////
// W5500 PROBE AND START

extern void i2c_initialize(void);               // peripherals.cpp

// Read VERSIONR (common block 0x0039) straight off the SPI bus - expect 0x04
bool a2b_tunnel_w5500_present(void)
{
    uint8_t hdr[3] = { 0x00, 0x39, 0x00 };      // Address 0x0039, BSB 0 (common), read, variable length
    uint8_t v = 0;
#ifdef USE_PIO
    extern wiznet_spi_handle_t spi_handle;      // wizchip_spi.c
    (*spi_handle)->frame_end();
    (*spi_handle)->frame_start();
    (*spi_handle)->write_buffer(hdr, 3);
    v = (*spi_handle)->read_byte();
    (*spi_handle)->frame_end();
#else
    gpio_put(PIN_CS, 0);
    spi_write_blocking(SPI_PORT, hdr, 3);
    spi_read_blocking(SPI_PORT, 0xFF, &v, 1);
    gpio_put(PIN_CS, 1);
#endif
    printf("A2B TUNNEL: W5500 VERSIONR = 0x%02X\n", v);
    return v == 0x04;
}

void a2b_tunnel_start(bool w5500_present)
{
    if (!vw_lock) vw_lock = spin_lock_init(spin_lock_claim_unused(true));

    tunnel_only = !w5500_present;
    if (tunnel_only)
    {
        // No real chip, so nothing to capture - the whole register map is virtual.
        // Bring it up through the normal driver so the ioLibrary state is consistent.
        vw_install(A2B_TUNNEL_SOCKETS_ONLY, false);
        uint8_t memsize[2][8] = {{2,2,2,2,2,2,2,2},{2,2,2,2,2,2,2,2}};
        ctlwizchip(CW_INIT_WIZCHIP, (void *)memsize);
    }
    else vw_install(A2B_TUNNEL_SOCKETS_WITH_W5500, true);

    i2c_initialize();                                       // Pins, pull ups, master baud (no-op if done)
    i2c_hw_t *hw = i2c_get_hw(TUNNEL_I2C);
    hw->enable = 0;
    hw->con   |= I2C_IC_CON_STOP_DET_IFADDRESSED_BITS;
    hw->rx_tl  = 0;
    hw->tx_tl  = 0;
    irq_set_exclusive_handler(TUNNEL_IRQ, tunnel_i2c_irq);
    slave_on = true;
    slave_enable();
    irq_set_enabled(TUNNEL_IRQ, true);                      // ISR runs on this core (core 0, the web core)

    printf("A2B TUNNEL: %s, I2C slave 0x%02X, virtual sockets 0x%02X\n",
           tunnel_only ? "NO W5500 - TUNNEL ONLY" : "W5500 + TUNNEL", A2B_TUNNEL_I2C_ADDR, vw_virtual_mask());
}

bool a2b_tunnel_only(void) { return tunnel_only; }

int a2b_tunnel_stats_text(char *buf, int len)
{
    const vw_stats_t *v = vw_get_stats();
    const tp_stats_t *t = tp_get_stats();
    return snprintf(buf, len,
        "A2B TUNNEL  mode %s  addr 0x%02X  mask 0x%02X\n"
        "  i2c transactions %lu  crc errors %lu  bad ops %lu  overruns %lu\n"
        "  connects %lu  rx bytes %lu  tx bytes %lu\n"
        "  frames real %lu  virtual %lu  master switches %lu  wait timeouts %lu\n",
        tunnel_only ? "tunnel only" : "w5500+tunnel", A2B_TUNNEL_I2C_ADDR, vw_virtual_mask(),
        t->transactions, t->crc_errors, t->bad_ops, t->overruns,
        v->connects, v->rx_bytes, v->tx_bytes,
        v->frames_real, v->frames_virtual, master_switches, master_wait_timeouts);
}
