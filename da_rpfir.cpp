#include "da_rpfir.hpp"
#include "pico/malloc.h"
#include <pico/bootrom.h>
#include "pwm.h"
#include "CT7302.h"
#include "peripherals.hpp"
#include "renderer.hpp"
#include "volume.hpp"
#include "hardware/adc.h"
#include "power.hpp"
#include "hardware/pwm.h"



const char *DEVICE_BOARD_NAME_STRING[] = { "", "", "W5500_EVB_PICO", "W55RP20_EVB_PICO", "W5100S_EVB_PICO2", "W5500_EVB_PICO2" };

Histogram core_idle[2];
Histogram core_stall[2];

#define I2S_EN        26 
#define DAC_EN        27



void setup_ct7302()
{

//  i2c_scan(0x91);

    CT73xxInit(0x10);

    CT73xxSetRegisterValue(0x05, 0x09);          // Set SRC to 192kHz

#if 0
    printf("CT7302 registers:\n");
    for (int addr = 0; addr <= 0xFF; addr++)
    { 
        if (addr % 16 == 0) printf("%02X: ", addr);
        printf("  0x%02X", CT73xxGetRegisterValue((BYTE)addr));
        if (addr % 16 == 15) printf("\n");
    }
#endif

}





int main()
{
    // Set the voltage regulator
    vreg_set_voltage(REG_VOLTAGE);
    set_sys_clock_khz(CLK_SYS/1000, false);
    uint32_t freq = clock_get_hz(clk_sys);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, freq, freq);        // Allow overclock of PERI

    gpio_set_dir_all_bits(0x00000000);                          // GPIOS are all inputs
    for (int n=0; n<32; n++) gpio_set_pulls(n, false,false);    // No pullups or pulldowns

    gpio_init(DAC_EN);                             // DAC Off
    gpio_set_dir(DAC_EN, GPIO_OUT);
    gpio_put(DAC_EN, 0);

    gpio_init(I2S_OUT_SD_PIN);                     // I2S SD pin - set to output and low to prevent noise during boot
    gpio_set_dir(I2S_OUT_SD_PIN, GPIO_OUT);
    gpio_put(I2S_OUT_SD_PIN, 0);

    // Keep SPI CS pins deasserted (high) during boot so renderer and W5500 don't see spurious transactions
    gpio_pull_up(17);   // SPI0 CS (W5500 / Renderer shared)

    gpio_init(I2S_EN);                             // Renderer Off 
    gpio_set_dir(I2S_EN, GPIO_OUT);
    gpio_put(I2S_EN, 0);

    power_init();

    stdio_init_all();
    sleep_ms(10);

    i2c_write(0x10, 0x10, 0xC7);                    // Force the output of the ASRC off until we need it

    core_idle[0].configure("Core 0 Idle", 0, 100);
    core_idle[1].configure("Core 1 Idle", 0, 100);

    core_stall[0].configure("Core 0 Stall", 0, 0.010);
    core_stall[1].configure("Core 1 Stall", 0, 0.010);

    printf("\n\n\n");
    LogConsole(LOG_UPTO(LOG_DEBUG));

    flash_load();

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
    
    gpio_put(DAC_EN, 1);        // Start the DAC so that it gives a clock to ASRC
    sleep_ms(100);

    setup_ct7302();            // Start the CT7302 and set it to max volume (0dB attenuation)

    gpio_put(I2S_EN, 1);        // If ASRC is up, then turn on the renderer
    sleep_ms(100);
  
    power_enable(true);       // Turn on the amp power
    sleep_ms(100);

    Notice("*********************************************");
    Notice("STARTING AUDIO SERVER IN CORE 1");
    start_audio();

#if 0
    Notice("*********************************************");
    Notice("STARTING WEB SERVER");
    start_web();
#else

    renderer_init();

    // Prime the volume with whatever the renderer currently reports so the
    // DSP gain is never stuck at 0 waiting for the first interrupt.
    {
        uint8_t vol = renderer_poll_volume();
        if (vol == 0 || vol & 0x80) volume_set(0, 0, 0);
        else                        volume_set(vol, 0, 0);
    }

    int64_t next_renderer_poll = now_ns();
    int64_t next_status_dump = now_ns();

    while(1)
    {
        int64_t time = now_ns();
        if (time > next_renderer_poll) 
        {   
            if (renderer_int_asserted()) 
            {
                printf("Renderer INT asserted\n");
                uint32_t interrupts = renderer_read_interrupts();
                if (interrupts & RENDERER_IF0_VOL) {
                    uint8_t vol = renderer_poll_volume();
                    if (vol == 0 || vol&0x80) volume_set(0,   200, 0);  // Mute immediately
                    else                      volume_set(vol, 100, 80);  
                    printf("Renderer volume: %d\n", vol);
                }
                renderer_clear_interrupts();        
                next_renderer_poll += + 10000000;
            }
            else next_renderer_poll = now_ns() + 1000000;
        }

        if (time > next_status_dump) 
        {
            extern uint16_t power_pwm_values[2];
            printf("ADC: %.3f  %.3f    PWM: %d  %d\n", power_read_voltage(0), power_read_voltage(1), power_pwm_values[0], power_pwm_values[1]);
            next_status_dump += 100000000;            
        }
    }
#endif
    
    rom_reset_usb_boot(-1,0);       // Back to bootloader

    // Should not arrive here
}        
