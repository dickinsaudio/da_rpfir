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

#pragma once

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Basic C libraries
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <ctime>
#include <stdint.h>


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The RP2040 SDK
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/unique_id.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "hardware/flash.h"
#pragma GCC diagnostic pop


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The W5x00 Ethernet driver
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
extern "C" {
#include "port_common.h"
#include "wizchip_conf.h"
//#include "w5x00_spi.h"
#include "socket.h"
#include "dhcp.h"
//#include "w5x00_gpio_irq.h"
}
#pragma GCC diagnostic pop


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Configuration details

#if !( (DEVICE_BOARD_NAME == WIZnet_Ethernet_HAT) || (DEVICE_BOARD_NAME == W5100S_EVB_PICO) || (DEVICE_BOARD_NAME == W5500_EVB_PICO) || (DEVICE_BOARD_NAME == W55RP20_EVB_PICO) || (DEVICE_BOARD_NAME == W5100S_EVB_PICO2) || (DEVICE_BOARD_NAME == W5500_EVB_PICO2) )
#error "DEVICE_BOARD_NAME must be WIZnet_Ethernet_HAT, W5100S_EVB_PICO, W5500_EVB_PICO, W55RP20_EVB_PICO, W5100S_EVB_PICO2, or W5500_EVB_PICO2"
#endif

extern const char* DEVICE_BOARD_NAME_STRING[];

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core AES67 and other application stuff
#include "log.hpp"
#include "histogram.hpp"
#include "http_server.hpp"
#include "upsample.h"
#include "deinterleave.h"
#include "dma.h"
#include "flash.hpp"

using namespace DAES67;

// USEFUL CLOCK FREQUENCIES IN RELATION TO DESIRED PIO
// F = [ repmat((30:135)'*12000000,49,1) repmat([1:7]',106*7,1) reshape(repmat([1:7],106*7,1)',[],1) ];
// FF = F(:,1)./F(:,2)./F(:,3);
// F = F(FF<300000000 & FF>100000000,:);
// FF = F(:,1)./F(:,2)./F(:,3);
// unique(sort(FF(find(abs(FF/(48000*64*16*2) - round(FF/(48000*64*16*2)*256)/256)==0))))
//
// 144000000   192000000   240000000   288000000


#define     REG_VOLTAGE     VREG_VOLTAGE_1_15                               // Voltage regulator setting       
#define     CLK_SYS         (288000000L)                                    // The system clock frequency


#define     FILTER_INPUT     1                // The I2S input to take (0=L  1=R)
//#define     FILTER_0         SGR_High_LR4   
//#define     FILTER_1         SGR_Mid_LR4    
//#define     FILTER_0         Passthrough    
//#define     FILTER_1         SGR_Low_LR4    
//#define    FILTER_0         SGR_High_Glenn  
//#define    FILTER_1         SGR_Mid_Glenn   
//#define FILTER_0         Passthrough
//#define FILTER_1         SGR_Low_Glenn
//#define FILTER_0         SGR_High_Pass
//#define FILTER_1         SGR_Mid_Pass
//#define FILTER_0         Passthrough
//#define FILTER_1         SGR_Low_Pass
#define FILTER_0         Passthrough
#define FILTER_1         Passthrough


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION PROTOTYPES NOT WORTH HEADERS

void start_web(void);
void start_audio(void);
void stop_audio();

void w5500_startup(void);
void w5500_dhcp(void);

extern int32_t audio_out_peaks[2];
extern int32_t audio_in_peaks[2];

extern Histogram i2s_dma_timing;
extern Histogram i2s_dma_execution;

extern Histogram core_idle[2];
extern Histogram core_stall[2];

