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

// SIMPLE WRAPPER OF W5500 TO PROVIDE SETUP AND UDP RECEIVCE CALLBACK

#include "da_rpfir.hpp"

extern "C" {
#include "wizchip_spi.h"
}

#define SOCK 4

static void wizchip_read_burst(uint8_t *pBuf, uint16_t len)
{
    uint8_t tx_data = 0xFF;
    spi_read_blocking(SPI_PORT, tx_data, pBuf, len);
}

static void wizchip_write_burst(uint8_t *pBuf, uint16_t len)
{
    spi_write_blocking(SPI_PORT, pBuf, len);
}


////////////////////////////////////////////////////////////////////////////////
// CRITICAL SECTION MANAGEMENT FOR WIZCHIP
//
// Ok to setup in the enter() as setup is in one core before any other use.
// Second tier of locking provided so that we can perform a group of operations
// in the ISR and guarantee no stalls from the other core.

spin_lock_t *wiz_lock;
int32_t      wiz_lock_depth;
uint32_t     wiz_save_isr[4];          // Should only need 2, but 4 is safe if a surplus nest is used
uint32_t     wiz_core;          
uint16_t     wiz_any_port;


/*
static void wizchip_critical_enter()
{
    if (wiz_lock==0) wiz_lock = spin_lock_init(spin_lock_claim_unused(true));
    uint32_t save;
    uint32_t core = get_core_num();
    while(1)                                                    // Atomic test for availbility or us already owning it
    {                                                           //
        save = spin_lock_blocking(wiz_lock);                    // Disable interrupts and get the lock
        if (wiz_lock_depth==0 || wiz_core == core) break;       // Check if it is free or we already have it
        spin_unlock(wiz_lock, save);                            // Otherwise, release lock, enable interrupts and try again
    }
    wiz_core = core;                                            // Take ownership
    wiz_save_isr[wiz_lock_depth] = save;                        // Save the ISR at this level
    wiz_lock_depth++;                                           // Increment the depth
    spin_unlock_unsafe(wiz_lock);                               // Unlock the resource but keep interrupts off
}

static void wizchip_critical_exit()
{
    uint32_t save = spin_lock_blocking(wiz_lock);               // Disable interrupts and get the lock - make this safe
    if (wiz_lock_depth>0)                                       // Note wiz_core shouled = core, but plenty wrong if this not true
    {
        wiz_lock_depth--;                                       // Decrement the depth    
      save = wiz_save_isr[wiz_lock_depth];                    // Retrieve the original ISR at this level
    }
    spin_unlock(wiz_lock, save);                            // Otherwise, release lock, enable interrupts and try again
}
*/
static void wizchip_critical_enter()
{
}
    
static void wizchip_critical_exit()
{
}

void w5500_startup(void)
{
    // Enable the burst read and write functions
    WIZCHIP.IF.SPI._read_burst   = wizchip_read_burst;
    WIZCHIP.IF.SPI._write_burst  = wizchip_write_burst;
    WIZCHIP.CRIS._enter      = wizchip_critical_enter;
    WIZCHIP.CRIS._exit       = wizchip_critical_exit;
    wiz_any_port = 0x8000 + (flash->loads % 16) * 256;           // Create a range of ports that is new from last boot

    Notice("INITIALIZING SPI AND W5500");
    wizchip_spi_initialize();           // NOTE MAKE SURE TO PATCH THIS TO BE 36Mhz not 5Mhz SPI
    wizchip_reset();
    Notice("SPI BAUDRATE                %10d",   spi_get_baudrate(SPI_PORT));
    Notice("WIZCHIP RESET - WAITING FOR PHY LINK");    
    Notice("WIZCHIP RESET - WAITING FOR PHY LINK");    
    wizchip_initialize();               // NOTE This routine will wait for a PHY link
    wizchip_check();
    Notice("MAC ADDRESS       %02X:%02X:%02X:%02X:%02X:%02X", flash->net_info.mac[0], flash->net_info.mac[1], flash->net_info.mac[2], flash->net_info.mac[3], flash->net_info.mac[4], flash->net_info.mac[5]);
    Notice("PHY LINK          %02X", getPHYCFGR());
    setSHAR(flash->net_info.mac);       // Set the MAC address

    if (flash->net_info.ip[0] != 0)
    {
        Notice("QUICKSTART WITH IP ADDRESS %d.%d.%d.%d",   flash->net_info.ip[0], flash->net_info.ip[1], flash->net_info.ip[2], flash->net_info.ip[3]);
        network_initialize(flash->net_info);
    }

    // Additional WIZnet setup
    setINTLEVEL(300);               // Set the interrupt wait time to about 10us (allow ISR to exit)
    setSIR(0x10);                   // Enable socket 4 interrupt (disable any others)
    setRTR(4000);                   // Set the retransmission time to 400ms (in case we go global)
    setRCR(3);                      // Set the retransmission count to 4
}

extern "C" {
    void reset_DHCP_timeout(void);
};

void w5500_dhcp(void)
{
    uint8_t buffer[768] __attribute__((aligned(4))) = {};   // OK on stack as only used pre startup
    reg_dhcp_cbfunc(0, 0, 0);           // Use default callbacks that will set the IP if we get one
    int wait;                           // Run DHCP to get or renew the IP address
    for (int tries=0; tries<5; tries++)
    {
        Notice("DHCP ATTEMPT %2d / %2d   ",tries+1,10);
        DHCP_init(1, buffer);               // Use socket 1
        for (wait=13; wait>0; wait--) 
        {
            if (DHCP_run() == DHCP_IP_LEASED) break;
            sleep_ms(100);
        }
        Notice("");
        DHCP_stop();
        if (wait>0) break;
    }

    if (wait==0)                              // And if that fails, use the default zero config using the unique id
    {
        Notice("DHCP FAILED USING DEFAULT ZERO CONF IP");
        flash->net_info.ip[0]  = 169;           flash->net_info.ip[1]  = 254;       flash->net_info.ip[2]  = 1;                 flash->net_info.ip[3]  = 1;       
        flash->net_info.sn[0]  = 255;           flash->net_info.sn[1]  = 255;       flash->net_info.sn[2]  = 0;                 flash->net_info.sn[3]  = 0;
        flash->net_info.gw[0]  = 0;             flash->net_info.gw[1]  = 0;         flash->net_info.gw[2]  = 0;                 flash->net_info.gw[3]  = 0;
        flash->net_info.dns[0] = 8;             flash->net_info.dns[1] = 8;         flash->net_info.dns[2] = 8;                 flash->net_info.dns[3] = 8;
        flash->net_info.dhcp   = NETINFO_STATIC;      
        network_initialize(flash->net_info);
    }
    else
    {
    }

    ctlnetwork(CN_GET_NETINFO, (void *)&flash->net_info);
    
    // Not sure why, but this is not always correct
    //if (wait>0) flash->net_info.dhcp = NETINFO_DHCP; else flash->net_info.dhcp = NETINFO_STATIC;
    
    //void flash_save(void);
    //flash_save();

    Notice("IP ADDRESS        %d.%d.%d.%d",   flash->net_info.ip[0], flash->net_info.ip[1], flash->net_info.ip[2], flash->net_info.ip[3]);
    Notice("SUBNET MASK       %d.%d.%d.%d",   flash->net_info.sn[0], flash->net_info.sn[1], flash->net_info.sn[2], flash->net_info.sn[3]);
    Notice("GATEWAY           %d.%d.%d.%d", flash->net_info.gw[0], flash->net_info.gw[1], flash->net_info.gw[2], flash->net_info.gw[3]);
}



