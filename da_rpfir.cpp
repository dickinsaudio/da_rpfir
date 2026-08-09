#include "da_rpfir.hpp"
#include "pico/malloc.h"
#include <pico/bootrom.h>
#include "peripherals.hpp"
#include "hardware/adc.h"
#include "hardware/pwm.h"



const char *DEVICE_BOARD_NAME_STRING[] = { "", "", "W5500_EVB_PICO", "W55RP20_EVB_PICO", "W5100S_EVB_PICO2", "W5500_EVB_PICO2" };

Histogram core_idle[2];
Histogram core_stall[2];




int main()
{
    // Set the voltage regulator
    vreg_set_voltage(REG_VOLTAGE);
    set_sys_clock_khz(CLK_SYS/1000, false);
    uint32_t freq = clock_get_hz(clk_sys);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, freq, freq);        // Allow overclock of PERI

    gpio_set_dir_all_bits(0x00000000);                          // GPIOS are all inputs
    for (int n=0; n<32; n++) gpio_set_pulls(n, false,false);    // No pullups or pulldowns

    stdio_init_all();
    sleep_ms(10);

    core_idle[0].configure("Core 0 Idle", 0, 100);
    core_idle[1].configure("Core 1 Idle", 0, 100);

    core_stall[0].configure("Core 0 Stall", 0, 0.010);
    core_stall[1].configure("Core 1 Stall", 0, 0.010);

    printf("\n\n\n");
    LogConsole(LOG_UPTO(LOG_DEBUG));

    flash_load();

    send_rgb(0, 0, 15);

    Notice("*********************************************");
    Notice("BOOT NUMBER                 %10ld",flash->loads);
    Notice("SYSTEM CLOCK DESIRED:       %10ld", CLK_SYS);
    Notice("SYSTEM CLOCK ACTUAL:        %10ld", clock_get_hz(clk_sys));

    uint vco, postdiv1, postdiv2;
    check_sys_clock_khz(CLK_SYS/1000, &vco, &postdiv1, &postdiv2);
    Notice("SYSTEM VCO:                 %10ld", vco);
    Notice("SYSTEM POSTDIV1:            %10ld", postdiv1);
    Notice("SYSTEM POSTDIV2:            %10ld", postdiv2);

    char buffer[2048] = {};
    flash_state(buffer, sizeof(buffer));
    Notice("%s",buffer);
    
    sleep_ms(100);

    Notice("*********************************************");
    Notice("STARTING AUDIO SERVER IN CORE 1");
    start_audio();

    Notice("*********************************************");
    Notice("STARTING WEB SERVER");
    start_web();
    
    rom_reset_usb_boot(-1,0);       // Back to bootloader

    // Should not arrive here
}        
