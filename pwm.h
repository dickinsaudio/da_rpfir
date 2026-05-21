/* pwm.h
 *
 * written by James Hamond
 * 
 * Released under the GNU General Public License, https://www.gnu.org/licenses/.
 * 
 */
#ifndef PWM_H
#define PWM_H

#include "pico/stdlib.h"
#include "hardware/pwm.h"

//#define SET_PWM_CH0_LVL( s, v ) pwm_set_chan_level( s, PWM_CHAN_A, v ); //params slice#, value
//#define SET_PWM_LVL( x, y ) pwm_set_gpio_level( x, y );

#if __cplusplus
extern "C" {
#endif

uint pwmInit( uint pwmGpio, uint maxVal, uint freq_kHz, uint initVal );

void setPemLvl( uint pwmGpio, uint lvl );

#if __cplusplus
}
#endif

#endif  // PWM_H
