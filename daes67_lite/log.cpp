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

#include <log.hpp>
#include <histogram.hpp>
#include <inttypes.h>

int __log_hpp_nConsole__ = LOG_UPTO(LOG_NOTICE);
int __log_hpp_nMask__    = LOG_UPTO(LOG_DEBUG);

void LogMask(int mask)
{
    __log_hpp_nMask__ = mask;       // Internal use
    setlogmask(mask);               // System screen
}

void LogConsole(int mask)
{
    __log_hpp_nConsole__ = mask;
}

#ifdef PICO_BUILD
#include "hardware/sync.h"
#ifndef syslog_size
#define syslog_size (16384)
#endif

char syslog_buffer[syslog_size];
int  syslog_w;
spin_lock_t *syslog_spin_lock = 0;

// Maintain a buffer and slide in chunks of 4k when we get into the last 1k

char  __log[256];
const char *__type[] = {"EMRG","ALRT","CRIT","EROR","WARN","NOTE","INFO","DEBG"};
void Log(int code, const char *format, ...)
{
    // Later, when you need to initialize it:
    if (syslog_spin_lock == 0) {
        syslog_spin_lock = spin_lock_init(spin_lock_claim_unused(true));
    }
    if (code<0 || code>LOG_DEBUG) return;
    uint32_t save = spin_lock_blocking(syslog_spin_lock);
    va_list args;
    va_start(args, format);
    vsnprintf(__log, 256, format, args);
    va_end(args);

    if (((1<<code) & __log_hpp_nConsole__)) 
    {
        printf(__log);
        printf("\n");
    }
    if (((1<<code) & __log_hpp_nMask__)) 
    {
        int len = snprintf(syslog_buffer+syslog_w, syslog_size-syslog_w-2, "%6" PRId64 " (%d:%02d) %s ", DAES67::now_ns()/1000000,(int)getpid(), (int)gettid(), __type[code]);
        len += snprintf(syslog_buffer+syslog_w+len, syslog_size-syslog_w-len-2, __log);
        syslog_buffer[syslog_w+len++] = '\n';
        syslog_buffer[syslog_w+len]   = '\0';
        syslog_w += len;
        if (syslog_w >= syslog_size-syslog_size/8) 
        {
            char *p = syslog_buffer+syslog_size/4;                      // Slide back at least 1/4
            while (*p != '\n' && p < syslog_buffer+syslog_size/2) p++;  // Find the next newline
            if (*p == '\n') p++;
            int chuck = p-syslog_buffer;
            memmove(syslog_buffer, p, syslog_w - chuck + 1);
            syslog_w -= chuck;
        }
    }
    spin_unlock(syslog_spin_lock, save);
};
#else

void Log(int code, const char *format, ...)
{
    va_list args1, args2;
    va_start(args1, format);
    if (((1<<code) & __log_hpp_nConsole__)) 
    {
        va_copy (args2, args1);
        vprintf(format, args2);
        va_end(args2);
        printf("\n");
    }
    if (((1<<code) & __log_hpp_nMask__)) 
    {
        char newformat[256] = {};
        snprintf(newformat, 256, "(%d:%2d) %s", (int)getpid(), (int)gettid(), format);
        vsyslog(code, newformat, args1);
    }
    va_end(args1);
};

#endif
