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

#include "histogram.hpp"
#include <log.hpp>
#include <math.h>
#include <cinttypes>

namespace DAES67 {

uint32_t    Histogram::m_rand = 0;
int64_t     Histogram::nows;

Histogram::Histogram()
{
    strncpy(m_name,"UNINITIALIZED",sizeof(m_name));
    m_bin0  = 0;
    m_width = 1;
}

Histogram::Histogram(const char* name, float first, float last)
{
    if (name==nullptr) name = "";
    configure(name, first, last);
}

Histogram::~Histogram()
{
}

void Histogram::configure(const char* name, float first, float last)
{
    if (name==nullptr) name = "";
    strncpy(m_name,name,sizeof(m_name)-1);
    m_name[sizeof(m_name)-1] = '\0';
    // Debug("Configuring Histogram \"%.95s\"  First %f  Last %f",name,first,last);
    m_bin0  = first;
    m_width = (last - first)/(m_bins-1);
    clear();
}
void Histogram::configure(const char* name)
{
    if (name==nullptr) name = "";
    // Debug("Renaming Histogram \"%.95s\" to \"%.95s\"",m_name,name);
    clear();
}

void Histogram::configure(float start, float end)
{
    configure(m_name,start,end);
    clear();
}

void Histogram::clear()
{
    m_N     = 0;
    m_T     = 0;
    m_sumX  = 0;
    m_sumX2 = 0;
    m_x     = 0;
    m_min   = 0;
    m_max   = 0;
    memset(m_bin,0,sizeof(m_bin));
}

float Histogram::mode() const
{
    if (m_N==0) return 0;
    int   at = 0;
    float mx = 0;
    for (int i = 0; i < m_bins; i++) 
    {
        if (m_bin[i] > mx) 
        {
            mx = m_bin[i];
            at = i;
        }
    }
    if (at==0)        return bin0();
    if (at==m_bins-1) return binN();

    float prev = m_bin[at-1];
    float curr = m_bin[at];
    float next = m_bin[at+1];
    float mode = at + (prev - next) / 2 / (prev - 2*curr + next);

    return m_bin0 + mode*m_width;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ASCII PLOT
//
//

#define pos(x, y) ((y)*width + (x))
const char symbols[] = {(char)5, ' ', '_', '.', 'x', 'X'}; //   _.xX

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define symbol(x) (symbols[max(0, min(symbols[0] - 1, (int)((x)*symbols[0] + 0.5))) + 1])

int Histogram::sntext(int len, int height, char *str, bool logY, uint32_t yMax) const
{
    int  width      = m_bins + 4;
    if (height*width > len) height = len / width;
    if (height < 5) return 0;
    return text(height, str, logY, yMax);
}

int Histogram::text(int height, char *str, bool logY, uint32_t yMax) const
{
    if (yMax == 0)  { for (int b=0; b<m_bins; b++) if (m_bin[b]>yMax) yMax=m_bin[b]; };
    if (yMax == 0)  yMax = 1;

    bool ylabel     = true;
    bool xlabel     = true;
    int  width      = m_bins + 2 + 2 * ylabel;
    int  barHeight  = height - 2 * xlabel;

    memset(str, ' ', (width * height));
    str[(width * height)] = 0;

    for (int n = 0; n < height; n++) {
        str[pos(width - 2, n)] = '\r';
        str[pos(width - 1, n)] = '\n';
    };

    float Ra = (barHeight - 0.2001F) / logf((yMax + 1) / 2.0F);
    float Rb = 0.2001F - Ra * logf(2.0F);

    if (yMax==1)
    {
        Ra = 2*barHeight;
        Rb = 0.0F;
    }

    for (int h = 0; h < barHeight; h++)
        for (int b = 0; b < m_bins; b++) {
            if (!(logY))
                str[pos(b + 2 * ylabel, h)] = symbol((float)m_bin[b] / yMax * barHeight - barHeight + 1 + h);
            else
                str[pos(b + 2 * ylabel, h)] = symbol(Ra * logf((float)m_bin[b] + 1) + Rb - barHeight + 1 + h);
        }

    if (ylabel && barHeight >= 8 && barHeight < 90) {
        char ylab[100];
        snprintf(ylab, 100, "%-9.4G%80s", (float)yMax, "");
        if (!(logY))
            ylab[barHeight - 1] = '0';
        else
            ylab[barHeight - 1] = '1';
        for (int h = 0; h < barHeight; h++) {
            str[pos(0, h)] = ylab[h];
            str[pos(1, h)] = '|';
        };
    }


    if (xlabel) {
        char xlab[1024];
        if (m_bin0<0 && m_width*(m_bins-1)>m_bin0)          // Special case of the 0 being in range - ensure we mark it
        {
            snprintf(xlab, 1024, "%-8.2G%*s%8.2G", m_bin0, m_bins - 16, "",m_bin0+(m_bins-1)*m_width);
            xlab[(int)(-m_bin0/m_width)] = '0';
        }
        else
        {
            snprintf(xlab, 1024, "%-8.2G%*s%8.2G%*s%8.2G", m_bin0, m_bins / 2 - 15, "", 
                                                           m_bin0+(m_bins-1)*m_width/2, m_bins - 9 - (m_bins / 2), "", 
                                                           m_bin0+(m_bins-1)*m_width);
        }
        
        
        
        for (int n = 0; n < m_bins; n++) {
            str[pos(n + 2 * ylabel, barHeight + 1)] = xlab[n];
            str[pos(n + 2 * ylabel, barHeight)]     = '-';
        };
    }

    // TITLE
    char tmp[128];
    snprintf(tmp, sizeof(tmp) - 1, "|%-15.95s|", m_name);
    int len = (int)strlen(tmp);
    for (int n = 0; n < len; n++) {
        str[pos(n + m_bins + 2 * ylabel - len, 1)] = tmp[n];
    };

    if (m_bins > 20 && barHeight > 8) {
        int   line = 2;
        char  tmp[20];

        snprintf(tmp, 20, "|N%14" PRId64 "|", m_N);
        for (int n = 0; n < 17; n++) {
            str[pos(n + m_bins - 17 + 2 * ylabel, line)] = tmp[n];
        };
        line++;

        snprintf(tmp, 20, "|mean %10.3E|", mean());
        for (int n = 0; n < 17; n++) {
            str[pos(n + m_bins - 17 + 2 * ylabel, line)] = tmp[n];
        };
        line++;

        snprintf(tmp, 20, "|std  %10.3E|", std());
        for (int n = 0; n < 17; n++) {
            str[pos(n + m_bins - 17 + 2 * ylabel, line)] = tmp[n];
        };
        line++;
        
        snprintf(tmp, 20, "|mode %10.3E|", mode());
        for (int n = 0; n < 17; n++) {
            str[pos(n + m_bins - 17 + 2 * ylabel, line)] = tmp[n];
        };
        line++;

        snprintf(tmp, 20, "|min  %10.3E|", m_min);
        for (int n = 0; n < 17; n++) {
            str[pos(n + m_bins - 17 + 2 * ylabel, line)] = tmp[n];
        };
        line++;

        snprintf(tmp, 20, "|max  %10.3E|", m_max);
        for (int n = 0; n < 17; n++) {
            str[pos(n + m_bins - 17 + 2 * ylabel, line)] = tmp[n];
        };
        line++;
    }
    return width*height;
}

}