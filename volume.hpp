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

#pragma once

// ---------------------------------------------------------------------------
// API  (state is private to volume.cpp)
// ---------------------------------------------------------------------------

// Set target volume (0 = silence, 100 = 0 dB full scale).
//
//   time_ms        - minimum time to reach target (milliseconds)
//   slew_db_per_s  - maximum slew rate (dB/second)
//
// Both constrain the ramp; whichever gives the slower transition wins.
// If time_ms == 0 and slew_db_per_s == 0 the gain snaps instantly.
// Call from non-RT thread (Core 0) only.
void  volume_set(int level, float time_ms, float slew_db_per_s);

// Advance gain one sample toward the target; returns current gain.
// Fast-path cost (already at target): one branch + two loads.
// Call from RT audio thread (Core 1) at every sample.
float volume_update();

// Return current gain without advancing the ramp.
float volume_get();
