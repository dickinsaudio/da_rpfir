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
