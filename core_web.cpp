/*******************************************************************
 * Copyright (C) 2025 DickinsAudio
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

#include "da_rpfir.hpp"
#include <vector>
#include <algorithm> 
#include <numeric>
#include <pico/bootrom.h>
#include "hardware/adc.h"
#include <board_list.h>

#ifndef BANNER
#define BANNER file_dickinsaudio_png
#endif
#ifndef BANNER_STYLE
#define BANNER_STYLE "width: 50%;"
#endif 

#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)
#include STRINGIFY(BANNER.h)



#define HTTP_PORT    (80)           // Port for HTTP server
#define HTTP_SOCKETS (3)            // Number of sockets to use for HTTP


#define         idle_check_time_ms      (1000)         // Time between idle checks in ms
HttpServer      server;


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// STYLE TEMPLATE PAGE
//

// TODO  Would be nice to cache more of this somehow
static char __in_flash() page_prefix[] = 
    "<!DOCTYPE html><html lang=\"en\">"
    "<head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, shrink-to-fit=yes, initial-scale=1.0\">"
        "<style> html { overflow-y : scroll; } </style>"
        "<link rel=\"stylesheet\" href=\"styles.css\">"
        "<script src=\"scripts.js\"></script>"
    "</head><body>"
    "<header>"
        "<img src=\"banner.png\" style=" STRINGIFY(BANNER_STYLE) " class=\"banner\">"
    "</header>"
    "<div class=\"menu\">"
        "<button class=\"mbut\" onclick=\"location.href='overview.html'\">OVERVIEW</button>"
        "<button class=\"mbut\" onclick=\"location.href='statistics.html'\">STATISTICS</button>"
        "<button class=\"mbut\" onclick=\"location.href='system.html'\">SYSTEM</button>"
        "<button class=\"mbut\" onclick=\"bootloader()\">USB BOOT</button>"
    "</div>"
    "<progress id=\"I0\" class=\"bar\" value=\"0\" max=\"1000\"></progress><br>"
    "<progress id=\"I1\" class=\"bar\" value=\"0\" max=\"1000\"></progress><br>"
    "<br>"
    "<progress id=\"O0\" class=\"bar\" value=\"0\" max=\"1000\"></progress><br>"
    "<progress id=\"O1\" class=\"bar\" value=\"0\" max=\"1000\"></progress><br>"
    "<script>updateMeters();</script>";


static char __in_flash() page_scripts[] = 
    "function updateMeters() {"
        "fetch('get?meters')"
            ".then(response => response.json())"
            ".then(data => {"
                "document.getElementById('I0').value = data.I0;"
                "document.getElementById('I1').value = data.I1;"
                "document.getElementById('O0').value = data.O0;"
                "document.getElementById('O1').value = data.O1;"
                "setTimeout(updateMeters,50);})"
            ".catch(err => { setTimeout(updateMeters,2000); });"
    "}"
    "function set(s)  { fetch('set?' + s); }"
    "function get(s)  { fetch('get?' + s); }"
    "function setr(s) { fetch('set?' + s) .finally(() => { window.location.reload();}); }"
    "function getr(s) { fetch('get?' + s) .finally(() => { window.location.reload();}); }"
    "function upload(b) {"
        "const input = document.getElementById('input');"
        "if (input.files.length > 0) {"
            "const rdr = new FileReader();"
            "rdr.onload = e => fetch('upload'+(b ? '_bootloader' : ''), { method: 'POST', headers: {'Content-Type': 'application/octet-stream'}, body: e.target.result})"
                               ".then(res => res.text()).catch(err => console.error('Error:', err));"
            "rdr.readAsArrayBuffer(input.files[0]);"
            "setTimeout(() => { window.location.reload(); }, 60000);"
        "}"
    "}"
    "function show(h) {"
        "fetch(h)"
            ".then(response => response.text())"
            ".then(data => { document.getElementById(h).innerText = data; });"
    "}"
    "function reboot()      { fetch('set?reboot'); setTimeout(() => { window.location.reload(); }, 5000); }"
    "function bootloader()  { fetch('set?bootloader'); }"
    "function updateProg() {"
        "fetch('get?upload')"
            ".then(response => response.json())"
            ".then(data => { document.getElementById('prog').value = data.upload; setTimeout(updateProg, 50);});"
    "}"
    ;

    
static char __in_flash() page_css[] =
    "body {"
        "margin: 0;"
        "font-family: Arial, sans-serif;"
        "background: #f4f4f4;"
        "color: #333;"
        "line-height: 1.2;"
        "padding-left: 5px;"
    "}"
    "h1, h2, h3 {"
        "color: #2c3e50;"
        "margin-top: 0.1;"
        "margin-bottom: 0.25em;"
    "}"
    "h1 { font-size: 2rem; }"
    "h2 { font-size: 1.5rem; }"
    "h3 { font-size: 1.2rem; }"
    "img.banner {"
        "width: 100%;"
        "height: auto;"
        "display: block;"
        "padding: 0;"
        "margin-left: -5px;"
    "}"
    ".menu {"
        "display: flex;"
        "justify-content: flex-start;"
        "align-items: center;"
        "background-color: #f4f4f4;"
        "margin-bottom: 10px;"
    "}"
    ".mbut {"
        "padding: 10px 2px 10px 2px;"
        "background-color: #ccc;"
        "border: none;"
        "border-radius: 5px;"
        "font-size: 1rem;"
        "color: #333;"
        "cursor: pointer;"
        "margin: 5px 5px 0 0;"
        "width: 18%;"
    "}"
    ".mbut:active { background-color: #999; }"
    ".but {"
        "padding: 4px 5px 4px 5px;"
        "background-color: #ccc;"
        "border: none;"
        "border-radius: 5px;"
        "font-size: 1rem;"
        "color: #333;"
        "cursor: pointer;"
        "margin: 10px 5px 0 0;"
    "}"
    ".but:active { background-color: #999; }"
    ".bar { width: 65%; height: 20px; }"
    "pre  { display: inline; }"
    ".dev { margin-top: .3cm; margin-bottom: 0cm; font-weight: bold; }"
    ".str { font-size: larger; line-height: 1.1; }"
    ".stat { font-size: small; line-height: 0.9; }"
    ".info { font-size: larger; line-height: 1.0; }"
    
    "@media screen and (max-width: 900px) {"
    ".stat { font-size: xx-small; }"
    "}";


#define ADD(...)          { int n = snprintf(p,len,__VA_ARGS__); if (n<len) { p+=n; len-=n; } else { p+=len-1; len=0; }; }


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// OVERVIEW PAGE
//

const char *cgi_overview(const char* name, const char* arg, int len, char *buf)
{
    if (!name || !arg || len==0 || !buf) return "";
    char *p = buf;
    ADD("<title>%s</title>",flash->name);
    ADD("<h2>OVERVIEW &nbsp;&nbsp;%s</h2>",flash->name);
    ADD("&nbsp;&nbsp;&nbsp;&nbsp;UPTIME&nbsp;&nbsp;<div id=\"up\" style=\"display: inline;\"></div><br>");
    ADD("</body></html>");
    return buf;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SYSTEM PAGE
//

const char *cgi_sys(const char* name, const char* arg, int len, char *buf)
{
    if (!name || !arg || len==0 || !buf) return "";
    
    char *p = buf;
    ADD("<title>%s</title>",flash->name);
    ADD("<button class=\"but\" onclick=\"location.href='system.html'\">CPU</button>");
    ADD("<button class=\"but\" onclick=\"location.href='system.html?sysinfo'\">SYS INFO</button>");
    ADD("<button class=\"but\" onclick=\"location.href='system.html?syslog'\">SYS LOG</button>");
    
    if (strstr(arg,"syslog"))
    {
        ADD("<h2>SYS LOG</h2>");
        ADD("<div class=\"stat\"><pre id=\"syslog\"></pre></div><br><br><br>");
        ADD("<script>setTimeout(function ls() { show('syslog'); setTimeout(ls, 1000); }, 1);</script>");
    }
    else if (strstr(arg,"sysinfo"))
    {
        ADD("<h2>SYSTEM INFO</h2>");
        ADD("<button class=\"but\" onclick=\"reboot()\">REBOOT</button>");
        ADD("<form action=\"system.html\" method=\"GET\">");
        ADD("<input class=\"but\" type=\"submit\" value=\"CHANGE NAME\">");
        ADD("<input class=\"but\" type=\"text\" id=\"new_name\" name=\"new_name\" value=\"%s\">",flash->name);
        ADD("</form>");
        ADD("<h2>FIRMWARE DETAIL</h2><pre class=\"info\">");
        #define STR(x) #x
        #define STRING(x) STR(x)
        ADD("DEVICE BUILD        RPFIR (V%ld) - %s\n",0,DEVICE_BOARD_NAME_STRING[DEVICE_BOARD_NAME]);
        ADD("GIT VERSION         %s(%s)   %s\n",GIT_BRANCH, GIT_HASH, GIT_TAG);
        ADD("BUILD TIME          %s %s\n",__DATE__, __TIME__);
        ADD("<h2>FLASH INFO</h2><pre class=\"info\">");
        int f = flash_state(p,len); p+=f; len-=f;
        ADD("UPTIME              %lds</pre>",(int32_t)(time_us_64()/1000000));
        ADD("<br><br><button class=\"but\" onclick=\"bootloader()\">START USB BOOTLOADER</button><br>");
        ADD("<button class=\"but\" onclick=\"set('debug')\">CONSOLE DEBUG</button><br><br>");
        ADD("<br><br><br><br><br><br>");
        ADD("<label>DANGEROUS: Select this to Make firmware Upload Bootloader<input type=\"checkbox\" id=\"boot_check\"></label>");
    }
    else
    {
        ADD("<h2>CPU IDLE</h2>");
        ADD("<button class=\"but\" onclick=\"{ set('clearsystem'); ld; }\">CLEAR</button><br><br>");
        ADD("<div class=\"stat\"><pre id=\"cpu\"></pre></div>");
        ADD("<script>setTimeout(function ls() { show('cpu'); setTimeout(ls, 1000); }, 1);</script>");
    }
    ADD("</body></html>");
    return buf;
}
 

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// STATISTICS PAGE
//

static char __in_flash() page_statistics[] = "<h2>STATISTICS</h2>"
                    "<button class=\"but\" onclick=\"{ set('clearstats'); loadText(false); }\">CLEAR</button>"
                    "&nbsp;&nbsp;&nbsp;DOWNLOADS&nbsp;"
                    "<button class=\"but\" onclick=\"location.href='statistics.txt'\">STATS</button>"
                    "<button class=\"but\" onclick=\"location.href='trace.txt'\">TRACE</button>"
                    "<br><br><div class=\"stat\"><pre id=\"text\"></pre></div>"
                    "<script>"
                    "function loadText(retrigger = true) {"
                    "fetch('statistics.txt')"
                    ".then(response => response.text())"
                    ".then(data => {"
                    "document.getElementById('text').innerText = data;"
                    "if (retrigger) setTimeout(loadText, 950);"
                    "});"
                    "}"
                    "loadText();"
                    "</script>"
                    "</body></html>";

const char *cgi_statistics(const char* name, const char* arg, int len, char *buf)
{
    if (!name || !arg || len==0 || !buf) return "";
    char *p = buf;
    ADD("<title>%s</title>",flash->name);
    ADD(page_statistics);
    return buf;
}

int chunk_statistics(int id, int len, char* buf)
{
    static int32_t step[HTTP_SOCKETS] = {};
    if (id<0 || id>=HTTP_SOCKETS) return 0;
    if (len==0 || buf==0) { step[id]=0; return 0; };
    char *p = buf;
    int height = 15;
    switch(step[id]++)
    {
        case 0:  ADD("Time %lld\n\n",now_ns()/1000000000LL);
                 p += i2s_dma_timing.sntext(len, height, p); *p++='\n'; break;
        case 1:  p += i2s_dma_execution.sntext(len, height, p); *p++='\n'; *p++='\n'; break;
        default: return 0;
    }
    return p-buf;
}

int chunk_cpu(int id, int len, char* buf)
{
    static int32_t step[HTTP_SOCKETS] = {};
    if (id<0 || id>=HTTP_SOCKETS) return 0;
    if (len==0 || buf==0) { step[id]=0; return 0; };
    char *p = buf;
    int height = 12;
    switch(step[id]++)
    {
        case 0:  p += core_idle[0].sntext(len, height, p); *p++='\n'; break;
        case 1:  p += core_stall[0].sntext(len, height, p); *p++='\n'; break;
        case 2:  p += core_idle[1].sntext(len, height, p); *p++='\n'; break;
        case 3:  p += core_stall[1].sntext(len, height, p); *p++='\n'; *p++='\n'; break;
        case 4:  ADD("\n\n\nHTTP CLINETS\n\n"); server.get_client_list(len, p); p = p + strlen(p); *p++='\n'; break;
        default: return 0;
    }
    return p-buf;
}



///////////////////////////ci////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// COMMANDS AND SETTINGS
//
const char *cgi_set(const char* name, const char* arg, int len, char *buf)
{
    if (!name || !arg || len==0 || !buf) return "";
    if (strstr(arg,"clearstats")) 
    { 
        i2s_dma_timing.clear();
    }
    if (strstr(arg,"debug")) 
    { 
        LogConsole(LOG_UPTO(LOG_DEBUG));
    }
    if (strstr(arg,"clearsystem")) 
    { 
        core_idle[0].clear();
        core_idle[1].clear();
        core_stall[0].clear();
        core_stall[1].clear();
    }

    if (strstr(arg,"reboot"))
    {   
        watchdog_reboot(0,0,0);
        while(1);
    }
    if (strstr(arg,"bootloader"))
    {
        rom_reset_usb_boot(-1,0);
        while(1);
    }
    return ""; 
}

int out_meters[2]={};
int in_meters[2]={};


const char *cgi_get(const char* name, const char* arg, int len, char *buf)
{
    if (!name || !arg || len==0 || !buf) return "";
    char *p = buf;
    bool all = strstr(arg,"all");
    ADD("{\"up\":%" PRId32 ",",(int32_t)(time_us_64()/1000000));
    if (all || strstr(arg,"meters"))
    {
        int channels = (int)(sizeof(out_meters)/sizeof(int32_t));
        for (int i=0; i<channels; i++) ADD("\"O%d\":%d,",i,out_meters[i]);
        channels = (int)(sizeof(in_meters)/sizeof(int32_t));
        for (int i=0; i<channels; i++) ADD("\"I%d\":%d,",i,in_meters[i]);
    }

    p[-1]='}';
    return buf;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// MAIN THREAD AND HTTP SERVER SETUP
//

void start_web(void)
{

    Notice("STARTING NETWORK");
    w5500_startup();                    // Bring up the network
    Notice("STARTING DHCP");
    w5500_dhcp();
 
    // PRIME THE WEB PAGES
    Notice("WEB STARTING");
    server.start(HTTP_PORT,HTTP_SOCKETS,page_prefix);
    
    // TODO Check the cache times
    server.add("banner.png", BANNER, sizeof(BANNER),false, TYPE_BINARY, false, 3600);  // Cache the logo
    server.add("styles.css",    page_css,          0,                         false, TYPE_CSS,    false, 3600);  // Cache the style sheet
    server.add("scripts.js",    page_scripts,      0,                         false, TYPE_SCRIPT, false, 3600);  // Cache the scripts

    server.add_cgi("set",       cgi_set,false);
    server.add_cgi("get",       cgi_get,false);

    server.add("syslog",        syslog_buffer,     0,                         false, TYPE_TEXT, true);            // Syslog available on a web page

    server.add_cgi("index.html",     cgi_overview);
    server.add_cgi("overview.html",  cgi_overview);
    server.add_cgi("statistics.html",cgi_statistics);
    server.add_cgi("system.html",    cgi_sys);

    server.add_chunked("statistics.txt", chunk_statistics,TYPE_TEXT,true);
    server.add_chunked("cpu", chunk_cpu,TYPE_TEXT,true);

    char ip[18];
    snprintf(ip,18," %d.%d.%d.%d",flash->net_info.ip[0],flash->net_info.ip[1],flash->net_info.ip[2],flash->net_info.ip[3]);

    printf(" Time Latency Buf SmpRate  Offset Under\n");
    printf("  (s)    (ns) pos    (Hz)   (ppb) runs\n");

    int64_t next_log   = now_ns();
    int64_t next_peak  = next_log;
    int64_t next_web   = next_log;
    int64_t next_idle_check = next_log+100000000LL;  // Wait 100ms
    int64_t core_ticks[2] = {};

    // Flush any existing input before entering the main loop
    while (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT) {
        // Consume any buffered characters
    }
    
    while (1)
    {
        int64_t now = now_ns();
        
        core_stall[get_core_num()].time(now); 
        
        if (now > next_idle_check)
        {
            next_idle_check += idle_check_time_ms*1000000LL;
            
            for (int n=0; n<2; n++)
            {
                // Based on the loop taking 1.5us - largely the ::now() and the Histogram.time()
                const float max_ticks = (float)(idle_check_time_ms*1000000.0F/1E9F/100.0F/1.2E-6);
                if (core_stall[n].N() > core_ticks[n]) core_idle[n].add((float)(core_stall[n].N()-core_ticks[n]) / max_ticks);
                core_ticks[n] = core_stall[n].N();
            }

        }

        if (now > next_log)
        {
            next_log += 1000000000LL;
        }

        if (now > next_peak)
        {
            next_peak = now + 10000000LL;       // Fixed interval not fixed rate
            int channels = (int)(sizeof(out_meters)/sizeof(int32_t));
            for (int n=0; n<channels; n++)
            {
                int old = out_meters[n];
                int val = (1000.0F+300.0F*log10f((200.0F+audio_out_peaks[n])/(float)(0x7FFFFFFFL))+0.5)+8;
                
                if (val < old) val = old;
                val = val - 8;

                if (val<0) val = 0;
                out_meters[n] = val;
                audio_out_peaks[n]=0;
            }

            channels = (int)(sizeof(in_meters)/sizeof(int32_t));
            for (int n=0; n<channels; n++)
            {
                int old = in_meters[n];
                int val = (1000.0F+300.0F*log10f((200.0F+audio_in_peaks[n])/(float)(0x7FFFFFFFL))+0.5)+8;
                
                if (val < old) val = old;
                val = val - 8;

                if (val<0) val = 0;
                in_meters[n] = val;
                audio_in_peaks[n]=0;
            }
        }
        if (now > next_web)
        {
            next_web += 1000000LL;  
            server.tick();
        }

        if (0)
        {
            uint32_t gpio_state = gpio_get_all();
            printf("GPIO STATE (32-bit binary): ");
            for (int i = 0; i < 32; i++) {
            printf("%d", (gpio_state >> i) & 1);
            if (i % 4 == 3) printf(" ");  // Add space every 4 bits for readability
            }
            printf("PIO0 SM2 PC: %d ", pio_sm_get_pc(pio0, 2));
            printf(" \n");
        }
    }
}
