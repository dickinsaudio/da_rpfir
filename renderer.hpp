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

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "hardware/spi.h"

//---------------------------------------------------------------------------
// SPI hardware config - adjust pins to match board layout
// W5500 and Renderer are on the same SPI spi0)
//---------------------------------------------------------------------------

#define RENDERER_SPI        spi0
#define RENDERER_PIN_SCK    18
#define RENDERER_PIN_MOSI   19
#define RENDERER_PIN_MISO   16
#define RENDERER_PIN_CS     17
#define RENDERER_SPI_BAUD   4000000     // 1 MHz (max 4 MHz per spec)

#define RENDERER_SPI_INT    21

//---------------------------------------------------------------------------
// Register addresses (Table 4-6)
//---------------------------------------------------------------------------
#define RENDERER_REG_VOL    0x40    // Digital Volume Level        OUT  Byte  0-100
#define RENDERER_REG_DVC    0x41    // Digital Volume Control       IN  Byte
#define RENDERER_REG_ETL    0x42    // Elapsed Time LSB            OUT  Byte
#define RENDERER_REG_ETH    0x43    // Elapsed Time MSB            OUT  Byte
#define RENDERER_REG_TDL    0x44    // Track Duration LSB          OUT  Byte
#define RENDERER_REG_TDH    0x45    // Track Duration MSB          OUT  Byte
#define RENDERER_REG_PS     0x46    // Player Status               OUT  Byte
#define RENDERER_REG_ARN    0x47    // Artist Name                 OUT  String
#define RENDERER_REG_ALN    0x48    // Album Name                  OUT  String
#define RENDERER_REG_TRN    0x49    // Track Name                  OUT  String
#define RENDERER_REG_COV    0x4A    // Cover                       OUT  Buffer
#define RENDERER_REG_TRF    0x4B    // Track Format                OUT  String
#define RENDERER_REG_IP0    0x4C    // IP Address byte 0           OUT  Byte
#define RENDERER_REG_IP1    0x4D    // IP Address byte 1           OUT  Byte
#define RENDERER_REG_IP2    0x4E    // IP Address byte 2           OUT  Byte
#define RENDERER_REG_IP3    0x4F    // IP Address byte 3           OUT  Byte
#define RENDERER_REG_IF0    0x50    // Interrupt Flags 0           OUT  Byte
#define RENDERER_REG_IF1    0x51    // Interrupt Flags 1           OUT  Byte
#define RENDERER_REG_IF2    0x52    // Interrupt Flags 2           OUT  Byte
#define RENDERER_REG_OFMT   0x59    // Output Format               OUT  Byte
#define RENDERER_REG_ELS    0x60    // Ethernet Link Status        OUT  Byte
#define RENDERER_REG_SR     0x61    // Sample Rate                 OUT  Byte
#define RENDERER_REG_BPS    0x62    // Bits Per Sample             OUT  Byte
#define RENDERER_REG_MAG    0x65    // Magic (always 0xA5)         OUT  Byte
#define RENDERER_REG_SCR    0x66    // Scratch                  IN/OUT  Byte
#define RENDERER_REG_MAC0   0x68    // Ethernet MAC Address byte 0 OUT  Byte
#define RENDERER_REG_MAC1   0x69    // Ethernet MAC Address byte 1 OUT  Byte
#define RENDERER_REG_MAC2   0x6A    // Ethernet MAC Address byte 2 OUT  Byte
#define RENDERER_REG_MAC3   0x6B    // Ethernet MAC Address byte 3 OUT  Byte
#define RENDERER_REG_MAC4   0x6C    // Ethernet MAC Address byte 4 OUT  Byte
#define RENDERER_REG_MAC5   0x6D    // Ethernet MAC Address byte 5 OUT  Byte
#define RENDERER_REG_FIRM   0x6F    // Firmware Version            OUT  String

//---------------------------------------------------------------------------
// SPI header word (Table 4-1)
// Bit 15-8: 0  |  Bit 7: R/W  |  Bits 6-0: address
//---------------------------------------------------------------------------
#define RENDERER_HDR_WRITE          0x0000
#define RENDERER_HDR_READ           0x0080
#define RENDERER_HDR_ADDR(a)        ((uint16_t)((a) & 0x7F))
#define RENDERER_HDR(rw, addr)      ((uint16_t)((rw) | RENDERER_HDR_ADDR(addr)))

//---------------------------------------------------------------------------
// SPI status word bits returned by MR-MOD (Tables 4-3, 4-4)
// Upper nibble of the 16-bit response
//---------------------------------------------------------------------------
#define RENDERER_STATUS_READY       0x8000  // 1 = device ready, transfer valid
#define RENDERER_STATUS_STATE       0x4000  // 1 = CMD state, 0 = DATA state
#define RENDERER_STATUS_PARITY      0x2000  // even parity over bits 15-0
#define RENDERER_STATUS_VALID       0x1000  // 0 = success, 1 = error

//---------------------------------------------------------------------------
// Register bit fields
//---------------------------------------------------------------------------

// DVC (0x41): bit 0 mode
#define RENDERER_DVC_VOL_ON         0x00    // apply digital volume attenuation
#define RENDERER_DVC_VOL_OFF        0x01    // bypass digital volume

// PS (0x46): bits 1:0
#define RENDERER_PS_STOPPED         0x00
#define RENDERER_PS_PLAYING         0x01
#define RENDERER_PS_PAUSED          0x02

// SR (0x61): bits 2:0
#define RENDERER_SR_44100           0
#define RENDERER_SR_48000           1
#define RENDERER_SR_88200           2
#define RENDERER_SR_96000           3
#define RENDERER_SR_176400          4
#define RENDERER_SR_192000          5
#define RENDERER_SR_352800          6
#define RENDERER_SR_384000          7

// BPS (0x62): bits 1:0
#define RENDERER_BPS_16             0
#define RENDERER_BPS_24             1
#define RENDERER_BPS_32             2

// IF0 (0x50): interrupt flags
#define RENDERER_IF0_VOL            (1 << 0)    // volume changed
#define RENDERER_IF0_ET             (1 << 1)    // elapsed time changed
#define RENDERER_IF0_TD             (1 << 2)    // track duration changed
#define RENDERER_IF0_PS             (1 << 3)    // player status changed
#define RENDERER_IF0_ARN            (1 << 4)    // artist name changed
#define RENDERER_IF0_ALN            (1 << 5)    // album name changed
#define RENDERER_IF0_TRN            (1 << 6)    // track name changed
#define RENDERER_IF0_COV            (1 << 7)    // cover changed

// IF1 (0x51): interrupt flags
#define RENDERER_IF1_TRF            (1 << 0)    // track format changed
#define RENDERER_IF1_IP             (1 << 1)    // IP address changed
#define RENDERER_IF1_ELS            (1 << 2)    // ethernet link status changed
#define RENDERER_IF1_SR             (1 << 3)    // sample rate changed
#define RENDERER_IF1_BPS            (1 << 4)    // bits per sample changed
#define RENDERER_IF1_MAC            (1 << 5)    // MAC address changed

// IF2 (0x52): interrupt flags
#define RENDERER_IF2_OFMT           (1 << 0)    // output format changed

//---------------------------------------------------------------------------
// API
//---------------------------------------------------------------------------

// Initialise SPI1 and CS GPIO. Call once before any other function.
void    renderer_init(void);

// Read a single Byte-type register. Returns true on success.
bool    renderer_read_reg(uint8_t addr, uint8_t *data);

// Write a single Byte-type register. Returns true on success.
bool    renderer_write_reg(uint8_t addr, uint8_t data);

// Poll REG_VOL (0x40) and return volume as 0-100. Returns 0 on comms error.
uint8_t renderer_poll_volume(void);

// Returns true when the INT pin is asserted (active-low) by the MR-MOD.
bool    renderer_int_asserted(void);

// Read all interrupt flag registers and return IF0.
// Test against RENDERER_IF0_* bits to identify the source.
uint8_t renderer_read_interrupts(void);

// Write 0 to all interrupt flag registers to clear them and deassert INT.
void    renderer_clear_interrupts(void);
