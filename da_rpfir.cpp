#include "da_rpfir.hpp"
#include "pico/malloc.h"
#include <pico/bootrom.h>
#include "pwm.h"

const char *DEVICE_BOARD_NAME_STRING[] = { "", "", "W5500_EVB_PICO", "W55RP20_EVB_PICO", "W5100S_EVB_PICO2", "W5500_EVB_PICO2" };

Histogram core_idle[2];
Histogram core_stall[2];

#define VAMP_EN       24
#define VSEN0         28
#define VSEN1         29
#define VA_PWM0       22
#define VA_PWM1       23
#define I2S_EN        26 
#define DAC_EN        27

#define PWM_MAX 1023
#define PWM_FREQ_KHZ  100

#define _0DB_PWM_VAL    ( PWM_MAX * 0.994 )  //eg 43V
#define M_3DB_PWM_VAL   ( PWM_MAX * 0.756 )  //eg 30.4V
#define M_6DB_PWM_VAL   ( PWM_MAX * 0.590 )  //eg 21.5V
#define M_9DB_PWM_VAL   ( PWM_MAX * 0.474 )  //eg 15.2V


int main()
{
    gpio_set_dir_all_bits(0x00000000);                          // GPIOS are all inputs
    for (int n=0; n<32; n++) gpio_set_pulls(n, false,false);    // No pullups or pulldowns

    gpio_init(VAMP_EN);                             // PMIC on Amp disable
    gpio_set_dir(VAMP_EN, GPIO_OUT);
    gpio_put(I2S_EN, 0);      

    gpio_init(DAC_EN);                             // DAC Off
    gpio_set_dir(DAC_EN, GPIO_OUT);
    gpio_put(DAC_EN, 0);

    gpio_init(I2S_EN);                             // Renderer Off 
    gpio_set_dir(I2S_EN, GPIO_OUT);
    gpio_put(I2S_EN, 0);


    core_idle[0].configure("Core 0 Idle", 0, 100);
    core_idle[1].configure("Core 1 Idle", 0, 100);

    core_stall[0].configure("Core 0 Stall", 0, 0.010);
    core_stall[1].configure("Core 1 Stall", 0, 0.010);


    // Set the voltage regulator
    vreg_set_voltage(REG_VOLTAGE);
    set_sys_clock_khz(CLK_SYS/1000, false);
    uint32_t freq = clock_get_hz(clk_sys);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, freq, freq);        // Allow overclock of PERI
    stdio_init_all();
    sleep_ms(10);
    
    printf("\n\n\n");
    LogConsole(LOG_UPTO(LOG_DEBUG));

    sleep_ms(100);                // Lowers the chance of a fail during flash load and resave
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

    Notice("*********************************************");
    Notice("STARTING AUDIO SERVER IN CORE 1");
    start_audio();
    Notice("STARTING WEB SERVER");
   
#if 1
 
    gpio_init(VA_PWM0);
    pwmInit( VA_PWM0, PWM_MAX, PWM_FREQ_KHZ, 0 );

    gpio_init(VA_PWM1); 
    pwmInit( VA_PWM1, PWM_MAX, PWM_FREQ_KHZ, 0 );

    setPemLvl(VA_PWM0, M_6DB_PWM_VAL);
    setPemLvl(VA_PWM1, M_6DB_PWM_VAL);
    sleep_ms(100); // Wait for rails to stabilize

#endif

    gpio_put(DAC_EN, 1);        // Start the DAC so that it gives a clock to ASRC
    sleep_ms(100);

    gpio_put(I2S_EN, 1);        // If ASRC is up, then turn on the renderer
    sleep_ms(100);
  
    gpio_put(VAMP_EN, 1);       // Turn on the amp power

    start_web();
    
    rom_reset_usb_boot(-1,0);       // Back to bootloader

    // Should not arrive here
}        
