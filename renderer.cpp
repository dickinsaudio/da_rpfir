/*******************************************************************
 * Copyright (C) 2023 DickinsAudio
 * 
 * This source code is the proprietary information of DickinsAudio.
 * All rights reserved.
 * 
 * This code is provided under specific license agreements and is 
 * intended for evaluation and consideration for licensed use. 
 * For discussions on licensing terms and pricing, please contact 
 * info@dickins.com
 * 
 * Licensed users are permitted full use of this code for the 
 * development and building of applications and systems, including 
 * modification, extension of the code, and use and transfer within
 * alternate representations, repositories and licensing frameworks
 * as allowed by the licensing arrangements in place with 
 * DickinsAudio.
 * 
 * Any use of this code outside of evaluation, consideration for 
 * licensed use, or as aggreed by license by licensed users is 
 * strictly prohibited.
 * 
 * DickinsAudio assumes no liability, either directly or indirectly, 
 * for the use of this software in relation to the use of the software 
 * and its relationship to any third-party intellectual property.
 *******************************************************************/

// MR-MOD Ethernet Media Renderer - SPI driver
// Ref: engineered SA MR-MOD datasheet v.117e

#include "renderer.hpp"
#include "pico/stdlib.h"

//---------------------------------------------------------------------------
// Protocol notes (datasheet section 4.1)
//
// Each register access = two separate CS pulses with ≥7µs gap between them.
//   Pulse 1: send 16-bit Header Word, receive 16-bit Status Word
//   Pulse 2: send 16-bit Data/Dummy Word, receive 16-bit Status(+Data) Word
//
// If the READY bit (bit 15) of the received word is 0, the DSP was busy.
// Repeat the same word (with a fresh CS pulse) until READY is set.
//
// VALID bit (bit 12) = 0 means transfer succeeded; 1 means error.
//
// SPI mode: CPOL=0, CPHA=0 (slave samples on rising edge), MSB first.
// Maximum SPI clock: 4 MHz.
//---------------------------------------------------------------------------

#define RETRY_MAX   16          // retries before giving up on a single word
#define CS_GAP_US   50           // t(SSD) min is 7µs; add 1µs margin

static inline void cs_select()   { gpio_put(RENDERER_PIN_CS, 0); }
static inline void cs_deselect() { gpio_put(RENDERER_PIN_CS, 1); }

// Transfer one 16-bit word (SPI must already be configured for 16-bit mode).
static inline uint16_t spi16(uint16_t tx)
{
    uint16_t rx;
    spi_write16_read16_blocking(RENDERER_SPI, &tx, &rx, 1);
    return rx;
}

// Send one 16-bit word in its own CS assertion window, retrying if the
// MR-MOD signals not-ready. Returns false if max retries reached or the
// transfer reports an error (VALID=1).
static bool send_word(uint16_t tx, uint16_t *rx_out)
{
    for (int i = 0; i < RETRY_MAX; i++) {
        cs_select();
        uint16_t rx = spi16(tx);
        cs_deselect();
        busy_wait_us(CS_GAP_US);

        if (rx & RENDERER_STATUS_READY) {
            *rx_out = rx;
            return !(rx & RENDERER_STATUS_VALID);   // VALID=0 → success
        }
    }
    return false;
}

//---------------------------------------------------------------------------
// Public API
//---------------------------------------------------------------------------

void renderer_init(void)
{
    spi_init(RENDERER_SPI, RENDERER_SPI_BAUD);
    spi_set_format(RENDERER_SPI, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(RENDERER_PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(RENDERER_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(RENDERER_PIN_MISO, GPIO_FUNC_SPI);

    gpio_init(RENDERER_PIN_CS);
    gpio_put(RENDERER_PIN_CS, 1);   // Set HIGH before enabling output to avoid CS glitch
    gpio_set_dir(RENDERER_PIN_CS, GPIO_OUT);

    // INT pin: active-low output from MR-MOD, asserted when registers change
    gpio_init(RENDERER_SPI_INT);
    gpio_set_dir(RENDERER_SPI_INT, GPIO_IN);
    gpio_pull_up(RENDERER_SPI_INT);
}

bool renderer_int_asserted(void)
{
    return !gpio_get(RENDERER_SPI_INT);  // active-low
}

bool renderer_read_reg(uint8_t addr, uint8_t *data)
{
    uint16_t status, status_data;

    // Pulse 1: header (read op)
    if (!send_word(RENDERER_HDR(RENDERER_HDR_READ, addr), &status))
        return false;

    // Pulse 2: dummy, collect data in response
    if (!send_word(0x0000, &status_data))
        return false;

    *data = (uint8_t)(status_data & 0xFF);
    return true;
}

bool renderer_write_reg(uint8_t addr, uint8_t data)
{
    uint16_t status;

    // Pulse 1: header (write op)
    if (!send_word(RENDERER_HDR(RENDERER_HDR_WRITE, addr), &status))
        return false;

    // Pulse 2: data word (bits 15-8 must be 0 per spec, Table 4-2)
    if (!send_word((uint16_t)data, &status))
        return false;

    return true;
}

uint8_t renderer_poll_volume(void)
{
    uint8_t vol = 0;
    renderer_read_reg(RENDERER_REG_VOL, &vol);
    return vol;
}

// Read all three interrupt flag registers and return them packed into a uint32_t
// (IF0 → bits 7:0, IF1 → bits 15:8, IF2 → bits 23:16).
uint32_t renderer_read_interrupts(void)
{
    uint8_t if0 = 0, if1 = 0, if2 = 0;
    renderer_read_reg(RENDERER_REG_IF0, &if0);
    renderer_read_reg(RENDERER_REG_IF1, &if1);
    renderer_read_reg(RENDERER_REG_IF2, &if2);
    return ((uint32_t)if2 << 16) | ((uint32_t)if1 << 8) | (uint32_t)if0;
}

// Write 0 to all interrupt flag registers to clear them and deassert INT.
void renderer_clear_interrupts(void)
{
    renderer_write_reg(RENDERER_REG_IF0, 0);
    renderer_write_reg(RENDERER_REG_IF1, 0);
    renderer_write_reg(RENDERER_REG_IF2, 0);
}
