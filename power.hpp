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
 * modification, extension and use and transfer within
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

#pragma once

// ---------------------------------------------------------------------------
// PWM starting value — empirically: 1016 gives ~43V, 1023 gives ~50V+, 0 also gives ~50V+.
// This is the safe nominal start point; the closed loop fine-tunes from here.
#define POWER_PWM_NOMINAL       ((int)(POWER_PWM_MAX * 0.994f))   // 1016 → ~43V

// ---------------------------------------------------------------------------
// Convenience target voltages (volts)
// ---------------------------------------------------------------------------
#define POWER_VOLTS_0DB             43.0f   // 0 dB amplifier rail
#define POWER_VOLTS_M3DB            30.4f   // -3 dB
#define POWER_VOLTS_M6DB            21.5f   // -6 dB
#define POWER_VOLTS_M9DB            15.2f   // -9 dB
#define POWER_VOLTS_M12DB           10.75f  // -12 dB
#define POWER_VOLTS_M15DB            7.6f   // -15 dB
#define POWER_VOLTS_M18DB            5.375f // -18 dB

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Initialise PWM outputs and ADC inputs.  VAMP_EN is driven low.
// Call once at startup before power_set_voltage() or power_enable().
void power_init();

// Enable or disable amplifier power (VAMP_EN).
void power_enable(bool on);

// Set the target rail voltage (volts) for both channels and seed an
// open-loop PWM estimate so the feedback loop converges quickly.
void power_set_voltage(float volts);

// Call periodically from the main loop.  Reads both ADC channels and
// steps each PWM value by ±1 until within the hysteresis band.
void power_update();

// Return the averaged rail voltage for channel 0 or 1
float power_read_voltage(int ch);
