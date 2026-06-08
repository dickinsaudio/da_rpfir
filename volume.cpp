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

// Volume control - gain ramping for dual-core audio DSP
// See volume.hpp for architecture notes.

#include "volume.hpp"
#include "da_rpfir.hpp"
#include <math.h>

// ---------------------------------------------------------------------------
// State - 32-bit float reads/writes are single-instruction on Cortex-M33.
// Core 0 writes s_target/s_scale*/s_at_target via volume_set_*().
// Core 1 reads/writes all fields via volume_update().
// ---------------------------------------------------------------------------
static volatile float s_gain      = 0.0f;
static volatile float s_target    = 0.0f;
static volatile bool  s_at_target = true;
static float          s_scale_up   = 1.0f;
static float          s_scale_down = 1.0f;


#define VOLUME_SILENCE_FLOOR   0.99e-4f   // -100 dB - floor used to avoid pow(0) / log(0)
#define VOLUME_MAX_DB          0.2f       // headroom: level 100 = -0.2 dB




// db = single([20*log10(realmin('single')), -30 - 50*(0.39*((39:-1:0)/39) + 0.61*((39:-1:0)/39).^2), -29.5:0.5:0]);
static float s_volume_table[101] = 
{ 
    500.00,  80.00,  77.96,  75.95,  73.99,  72.06,  70.18,  68.34,  66.53,  64.77, 
     63.05,  61.36,  59.72,  58.12,  56.56,  55.03,  53.55,  52.11,  50.71,  49.34, 
     48.02,  46.74,  45.50,  44.30,  43.13,  42.01,  40.93,  39.89,  38.89,  37.93, 
     37.01,  36.12,  35.28,  34.48,  33.72,  33.00,  32.32,  31.68,  31.08,  30.52, 
     30.00,  29.50,  29.00,  28.50,  28.00,  27.50,  27.00,  26.50,  26.00,  25.50, 
     25.00,  24.50,  24.00,  23.50,  23.00,  22.50,  22.00,  21.50,  21.00,  20.50, 
     20.00,  19.50,  19.00,  18.50,  18.00,  17.50,  17.00,  16.50,  16.00,  15.50, 
     15.00,  14.50,  14.00,  13.50,  13.00,  12.50,  12.00,  11.50,  11.00,  10.50, 
     10.00,   9.50,   9.00,   8.50,   8.00,   7.50,   7.00,   6.50,   6.00,   5.50, 
      5.00,   4.50,   4.00,   3.50,   3.00,   2.50,   2.00,   1.50,   1.00,   0.50, 
     VOLUME_MAX_DB
};


// ---------------------------------------------------------------------------
// level_to_gain
// ---------------------------------------------------------------------------
// Level 0   -> 0.0  (silence / mute)
// Level 1   -> 10^((-99 - 0.5)/20)  (-99.5 dB)
// Level 100 -> 10^(-0.5/20)          (-0.5 dB = VOLUME_GAIN_OFFSET_DB)
// Each integer step is exactly 1 dB: dB = (level - 100) - VOLUME_GAIN_OFFSET_DB
static float level_to_gain(int level)
{
    if (level <= 0)  return 0.0f;
    if (level > 100) level = 0;    // Safer to mute on invalid level
    const float dB = s_volume_table[level];
    return powf(10.0f, - dB / 20.0f);
}

void volume_set(int level, float time_ms, float slew_db_per_s)
{
    const float new_target = level_to_gain(level);

    // Instant snap
    if (time_ms == 0.0f && slew_db_per_s == 0.0f) {
        s_target    = new_target;
        s_gain      = new_target;
        s_at_target = true;
        return;
    }

    if (new_target == s_target) return;
    s_target = new_target;

    // If current gain is zero and target is higher, seed gain to SILENCE_FLOOR
    // so the multiplicative ramp has a non-zero starting point.
    if (new_target > 0.0f && s_gain == 0.0f) s_gain = VOLUME_SILENCE_FLOOR;

    // Work out N (number of samples to reach target) from each constraint,
    // then take the larger (slower) value so both limits are respected.
    float N = 0.0f;

    if (slew_db_per_s > 0.0f) {
        const float g      = ((float)s_gain > VOLUME_SILENCE_FLOOR) ? (float)s_gain : VOLUME_SILENCE_FLOOR;
        const float target = (new_target    > VOLUME_SILENCE_FLOOR) ? new_target    : VOLUME_SILENCE_FLOOR;
        const float dB_dist = fabsf(20.0f * log10f(target / g));
        N = dB_dist / slew_db_per_s * (float)SAMPLE_RATE;
    }

    if (time_ms > 0.0f) {
        const float N_time = (time_ms / 1000.0f) * (float)SAMPLE_RATE;
        if (N_time > N) N = N_time;
    }

    if (N < 1.0f) {   // computed ramp is sub-sample - just snap
        s_gain      = new_target;
        s_at_target = true;
        return;
    }

    const float g      = ((float)s_gain > VOLUME_SILENCE_FLOOR) ? (float)s_gain : VOLUME_SILENCE_FLOOR;
    const float target = (new_target    > VOLUME_SILENCE_FLOOR) ? new_target    : VOLUME_SILENCE_FLOOR;
    const float scale  = powf(target / g, 1.0f / N);

    if (new_target > (float)s_gain) {
        s_scale_up   = scale;
        s_scale_down = 1.0f;
    } else {
        s_scale_down = scale;
        s_scale_up   = 1.0f;
    }

    s_at_target = false;
}

// Worst case at 192kHz this will take about 20 cycles and so be 4Mhz or about 1.5% of CPU time
// But soft volume is important
float volume_update()
{
    if (s_at_target) return s_gain;

    float g = s_gain;
    const float t = s_target;

    // Note branch prediction eats this up
    if (g < t) {
        g *= s_scale_up;
        if (g >= t) { g = t; s_at_target = true; }
    } else {
        g *= s_scale_down;
        const float floor = (t > VOLUME_SILENCE_FLOOR) ? t : VOLUME_SILENCE_FLOOR;
        if (g <= floor) { g = t; s_at_target = true; }
    }

    s_gain = g;
    return g;
}

float volume_get()
{
    return s_gain;
}
