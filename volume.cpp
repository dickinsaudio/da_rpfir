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
static volatile float s_gain      = 1.0f;
static volatile float s_target    = 1.0f;
static volatile bool  s_at_target = true;
static float          s_scale_up   = 1.0f;
static float          s_scale_down = 1.0f;

#define SILENCE_FLOOR  0.99e-5f   // -100 dB - floor used to avoid pow(0) / log(0)

// ---------------------------------------------------------------------------
// level_to_gain
// ---------------------------------------------------------------------------
// Level 0   -> 0.0  (silence / mute)
// Level 1   -> 10^(-99/20)  (-99 dB)
// Level 99  -> 10^(-1/20)   ( -1 dB)
// Level 100 -> 1.0           (  0 dB)
// Each integer step is exactly 1 dB: dB = level - 100
static float level_to_gain(int level)
{
    if (level <= 0)   return 0.0f;
    if (level >= 100) return 1.0f;
    const float dB = (float)(level - 100);
    return powf(10.0f, dB / 20.0f);
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

    // Work out N (number of samples to reach target) from each constraint,
    // then take the larger (slower) value so both limits are respected.
    float N = 0.0f;

    if (slew_db_per_s > 0.0f) {
        const float g      = ((float)s_gain > SILENCE_FLOOR) ? (float)s_gain : SILENCE_FLOOR;
        const float target = (new_target    > SILENCE_FLOOR) ? new_target    : SILENCE_FLOOR;
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

    const float g      = ((float)s_gain > SILENCE_FLOOR) ? (float)s_gain : SILENCE_FLOOR;
    const float target = (new_target    > SILENCE_FLOOR) ? new_target    : SILENCE_FLOOR;
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
        const float floor = (t > SILENCE_FLOOR) ? t : SILENCE_FLOOR;
        if (g <= floor) { g = t; s_at_target = true; }
    }

    s_gain = g;
    return g;
}

float volume_get()
{
    return s_gain;
}
