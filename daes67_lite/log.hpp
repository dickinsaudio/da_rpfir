
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

// Very simple abstraction to catch logging if we want to do it another way later


#pragma once

#ifdef PICO_BUILD
#define syscall(...) 0
#define setlogmask(a)
#define        LOG_ERR              3        /* error conditions */
#define        LOG_WARNING          4        /* warning conditions */
#define        LOG_NOTICE           5        /* normal but significant condition */
#define        LOG_INFO             6        /* informational */
#define        LOG_DEBUG            7        /* debug-level messages */
#define        LOG_UPTO(x)          ((1<<(x+1))-1)
extern char syslog_buffer[];
#include "pico/multicore.h"
#include "hardware/irq.h"
#include "hardware/structs/scb.h"
#define getpid() (get_core_num())
#define gettid() ((scb_hw->icsr & 0x1f))

#else 
#include <syslog.h>
#include <sys/syscall.h>
#include <errno.h>
#include <unistd.h>
#include <unistd.h>
#define gettid() syscall(SYS_gettid)
#include <pthread.h>
#endif

#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sched.h>

extern int __log_hpp_nConsole__;
extern int __log_hpp_nMask__;


void Log(int code, const char *format, ...);
void LogMask(int mask);
void LogConsole(int mask);

#define Error(format, ...)    Log(LOG_ERR,     "ERROR %s (ERRNO %d) " format , __PRETTY_FUNCTION__, errno, ## __VA_ARGS__)
#define Warning(format, ...)  Log(LOG_WARNING, "WARNING " format , ## __VA_ARGS__)
#define Notice(format, ...)   Log(LOG_NOTICE,  format,  ## __VA_ARGS__)
#define Info(format, ...)     Log(LOG_INFO,    format,  ## __VA_ARGS__)
#define Debug(format, ...)    Log(LOG_DEBUG,   "DEBUG %s (ERRNO %d) " format,  __PRETTY_FUNCTION__, errno, ## __VA_ARGS__)

#ifndef PICO_BUILD
inline void set_priority(int priority, const char *thread_name)
{
	int    policy = SCHED_FIFO;
    struct sched_param param; 
    int max = sched_get_priority_max(policy);
    if (priority-1 >= max || priority<0) priority = max;
    else priority = priority - 1;
    param.sched_priority = priority;
	int ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
	ret = pthread_getschedparam(pthread_self(), &policy, &param);
    Info("STARTING %s    THREAD %d   REAL TIME PRIORITY -%d",thread_name,(int)gettid(),(int)param.sched_priority);
}
#endif