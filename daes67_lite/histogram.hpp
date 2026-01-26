/*******************************************************************
 * Copyright (C) 2023 DickinsAudio
 * 
 * This source code is the proprietary information of DickinsAudio.
 * All rights reserved.t char
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

#include <string>

#include <sys/types.h>
#include <time.h>
#ifdef PICO_BUILD
#define CLOCK_MONOTONIC_RAW 0
#include "pico/stdlib.h"
#include "hardware/structs/systick.h"
#else
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/if.h>
#include <sys/syscall.h>
#endif
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <thread>

// TODO Replace calls to Histogra::now() with now_ns()

#pragma once

namespace DAES67
{
#ifdef PICO_BUILD
    inline int64_t now_ns() { return time_us_64()*1000; };
#else
    inline int64_t now_ns() 
    { 
        timespec now; 
        clock_gettime(CLOCK_MONOTONIC_RAW, &now); 
        return ((int64_t)now.tv_sec)*1000000000ULL +  now.tv_nsec;        
    };
#endif
    

    class Histogram
    {
        public:
        Histogram();
	    Histogram(const char* name, float first = 0, float last = 1);
	    ~Histogram();

        void configure(const char *name, float start, float end);
        void configure(float start, float end);
        void configure(const char *name);
        void clear(); 


        void add(float x, int count=1)   // Add a value to the histogram
        {
            if (m_N==0) { m_max=x; m_min=x; };
            if (x > m_max) m_max = x;
            if (x < m_min) m_min = x;
    
            // The bin is obtained by adding a [0-1) float and then taking the floor
            int bin = (int)((x - m_bin0) / m_width + (float)rand() * (1.0F / 4294967296.0F));
            if (bin<0)       bin=0;
            if (bin>=m_bins) bin=m_bins-1;
            m_bin[bin] += count;
            m_N        += count;
            m_sumX     += x;
            m_sumX2    += x*x;
            m_x         = x;
        }

        int64_t start(int64_t t=0)                 // Start or restart the timer to add a time interval
        {
            if (t==0) m_T = now();
            else      m_T = t;
            return    m_T;
        }

        int64_t time(int64_t t=0, int count = 1)   // Add a time interval to the histogram since last start or time                            
        {
            if      (m_T==0)          return start(t);
            if      (t==0)            t = now();
            else if (t <100000000)    t = now()-t;  // Drop to 100ms - otherwise can cause wierdness if stuff kicks off quick
            add((float)(t - m_T)/1E9F, count);
            m_T = t;
            return m_T;
        };

        int text(int height, char *str, bool logY=true, uint32_t yMax=0) const;                 // Returns the string length of the chart
        int sntext(int len, int height, char *str, bool logY=true, uint32_t yMax=0) const;      // Safe version, which will truncate height

        const char* name() const { return m_name; };
        int      bins() const { return m_bins; };
        float    bin0() const { return m_bin0; };
        float    binN() const { return m_bin0 + m_bins*m_width; };
        int32_t  bin(int n) const { if (n<0 || n>=m_bins) return 0; return m_bin[n]; };
        int64_t  since()const { return now() - m_T; };
        int64_t  T()    const { return m_T; };
        int64_t  N()    const { return m_N; };
        float    mean() const { if (m_N==0) return 0; return (float)m_sumX / (float)m_N; };
        float    min()  const { return m_min; };
        float    max()  const { return m_max; };
        float    mode() const;
        float    std()  const { if (m_N==0) return 0; return sqrtf((float)(m_sumX2 / m_N - (m_sumX / m_N) * (m_sumX / m_N))); };
        float    latest() const { return m_x; };

        static int64_t nows;
        int64_t  now() const { nows++; return now_ns(); };

        static uint32_t rand()     { return (m_rand =  m_rand * 0x0019660d + 0x3c6ef35f); };
	private:
        static uint32_t m_rand;         // A single number used by all instances to create random dither
        static const int m_bins = 101;

    private:                  // The actual data which should be the full class struct in shm
        char      m_name[96]; // Name of the histogram
        float     m_bin0;     // Centre of the first bin
        float     m_width;    // Width of each bin
        int64_t   m_T;        // Time stamp of last entry
        int64_t   m_N;        // Number of counts we have added to histogram
        double    m_sumX;     // Need a double here to keep the true mean
        double    m_sumX2;    // Same    
        float     m_x;        // The last value added
        float     m_min;
        float     m_max;
        uint32_t  m_bin[m_bins]; 
    };
}
