///////////////////////////////////////////////////////////////////////////
// Flash structure for storing config and state
//
// The flash structure is stored in the last page of flash memory. It is
// 4k and holds enough to store the config and state of the dongle to
// operate at boot time.
//


#define RPFIR_MAGIC   0xDA541125       // Change this when the Flash structure changes

typedef struct 
{
    uint32_t                magic;      // Magic number to indicate the flash has been initialized
    uint32_t                version;    // The version of the dongle
    uint32_t                board;      // The type of processor board
    uint32_t                loads;      // Number of times the flash has been loaded
    char                    name[64];   // Name for the device
    pico_unique_board_id_t  id;
    wiz_NetInfo             net_info;
} flash_t;

extern flash_t *const flash;

void flash_load(void);
void flash_save(void);
int  flash_state(char *str, int len);



