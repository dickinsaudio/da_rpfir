/* pwm.c
 *
 * Original Author: ames Hamond
 * Date:   2026
 *
 * This is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3 as published by the
 * Free Software Foundation see <https://www.gnu.org/licenses/>.
 */

#include "pwm.h"

// returns detected value change, pos value clockwise. Best perfomance if run at 1kHz/1ms intervals.

uint pwmInit( uint pwmGpio, uint maxVal, uint freq_kHz, uint initVal )
{
  gpio_set_function(pwmGpio, GPIO_FUNC_PWM);
  uint slice_num = pwm_gpio_to_slice_num(pwmGpio);

  ///pwm_config_set_wrap( slice_num, maxVal );

  uint channel = pwm_gpio_to_channel(pwmGpio);

  uint clkDiv = SYS_CLK_KHZ / (float)( freq_kHz * maxVal);

  pwm_config config = pwm_get_default_config();
  pwm_config_set_clkdiv(&config, clkDiv);
  pwm_config_set_wrap(&config, maxVal);
    
  if ( initVal > maxVal ) initVal = maxVal;
  pwm_set_chan_level(slice_num, PWM_CHAN_A, initVal);

  pwm_init(slice_num, &config, true);
  pwm_set_enabled(slice_num, true);

  return slice_num;
}

inline void setPemLvl( uint pwmGpio, uint lvl ) { pwm_set_gpio_level(pwmGpio, lvl); }

