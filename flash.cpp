#include "da_rpfir.hpp"
#include "board_list.h"

#define FLASH_OFFSET (2044*1024)

struct flash_page_t
{
    flash_t     used;
    char        pad[2*FLASH_PAGE_SIZE - sizeof(flash_t)];
} flash_local_page;

flash_t *const flash = &flash_local_page.used;


// Must run from RAM on RP2350 to avoid executing from flash during write
void __no_inline_not_in_flash_func() flash_save()
{    
    uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(FLASH_OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(interrupts);   
    interrupts = save_and_disable_interrupts();
    flash_range_program(FLASH_OFFSET,(const uint8_t *)flash, sizeof(flash_page_t));
    restore_interrupts(interrupts);   
};

void flash_init()
{
    memset(flash,0,sizeof(flash_t));        // Initialization
        
    flash->magic = RPFIR_MAGIC;            // Just a sanity number so we know the flash has been initialized
    flash->version = 0;
    flash->board = DEVICE_BOARD_NAME;       // The type of board we are running on
    flash->loads = 1;                       // Count the number of initial loads (boot)

    flash->net_info.mac[0] = 0x00;          
    flash->net_info.mac[1] = 0x08;      
    flash->net_info.mac[2] = 0xDC;          
#if DEVICE_BOARD_NAME==W55RP20_EVB_PICO
    uint8_t txbuf[1+4+16] = {0};
    uint8_t rxbuf[1+4+16] = {0};
    txbuf[0] = 0x4b;
    flash_do_cmd(txbuf, rxbuf, 1+4+16);
    memcpy(flash->id.id, rxbuf+1+4+8, std::min((int)sizeof(flash->id.id),8));   // Longer UID on the W55RP40 Chip
    flash->net_info.mac[3] = flash->id.id[1];                                   // But most bytes are the same
    flash->net_info.mac[4] = flash->id.id[2];                                   // So pick the interesting ones
    flash->net_info.mac[5] = flash->id.id[6];
#else                                                                           // Use this also for default for now
//#elif DEVICE_BOARD_NAME==W5500_EVB_PICO                                       // RP2040 PICO EVK has shorter UID
    pico_get_unique_board_id(&flash->id);   // Put the unique ID into the flash structure
    flash->net_info.mac[3] = flash->id.id[5];                                   // 
    flash->net_info.mac[4] = flash->id.id[6];  
    flash->net_info.mac[5] = flash->id.id[7];
#endif

    snprintf(flash->name,64,"DA-%02X%02X",flash->net_info.mac[4],flash->net_info.mac[5]); 

    // IP address is set or cleared on boot by DHCP - so we don't need to set it here
};

void flash_load()
{
    memcpy(flash,(const void*)(XIP_BASE + FLASH_OFFSET),sizeof(flash_t));    
    if (flash->magic != RPFIR_MAGIC) flash_init();
    flash->loads++;
    flash_save();
}; 

#define ADD(...)          { int n = snprintf(p,len,__VA_ARGS__); if (n<len) { p+=n; len-=n; } else { p+=len-1; len=0; }; }

int flash_state(char *str, int len)
{
    char *p=str;  
    ADD("VERSION MARKER      %08lX\n",flash->magic); 
    ADD("RPFIR   VERSION     %ld\n",flash->version);
    ADD("BOARD NAME          %s\n",DEVICE_BOARD_NAME_STRING[flash->board]);
    ADD("NUMBER OF LOADS     %ld\n",flash->loads); 
    ADD("DEVICE NAME         %s\n",flash->name); 
    ADD("FLASH ID            %02X%02X%02X%02X%02X%02X%02X%02X\n",flash->id.id[0],flash->id.id[1],flash->id.id[2],flash->id.id[3],flash->id.id[4],flash->id.id[5],flash->id.id[6],flash->id.id[7]); 
    ADD("NETWORK MAC         %02X:%02X:%02X:%02X:%02X:%02X\n",flash->net_info.mac[0],flash->net_info.mac[1],flash->net_info.mac[2],flash->net_info.mac[3],flash->net_info.mac[4],flash->net_info.mac[5]); 
    ADD("NETWORK IP          %d.%d.%d.%d\n",flash->net_info.ip[0],flash->net_info.ip[1],flash->net_info.ip[2],flash->net_info.ip[3]); 
    ADD("NETWORK SUBNET      %d.%d.%d.%d\n",flash->net_info.sn[0],flash->net_info.sn[1],flash->net_info.sn[2],flash->net_info.sn[3]); 
    ADD("NETWORK GATEWAY     %d.%d.%d.%d\n",flash->net_info.gw[0],flash->net_info.gw[1],flash->net_info.gw[2],flash->net_info.gw[3]); 
    ADD("NETWORK DNS         %d.%d.%d.%d\n",flash->net_info.dns[0],flash->net_info.dns[1],flash->net_info.dns[2],flash->net_info.dns[3]); 
    ADD("NETWORK DHCP MODE   %s\n",flash->net_info.dhcp == NETINFO_DHCP ? "DHCP" : "STATIC"); 
    return p-str;
}