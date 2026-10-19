/*******************************************************************
 * Copyright (C) 2026 DickinsAudio
 *
 * NATIVE END TO END TEST HARNESS
 *
 * Runs the real daes67_lite HttpServer (PICO_BUILD path, i.e. the W5500
 * register level code) and the real WIZnet ioLibrary against the virtual
 * W5500, with the tunnel protocol driven from a TCP socket that stands in
 * for the A2B I2C link (SimTransport framing in host/transports.py):
 *
 *   request  [kind 'W'|'R'][node][wlen u16][rlen u16][wdata]
 *   response [0 ack | 1 nack][rdata]
 *******************************************************************/

#include <sys/socket.h>                     // System headers first - the ioLibrary redefines socket names
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdlib.h>
#include <string>
#include "http_server.hpp"
#include "log.hpp"
extern "C" {
#include "wizchip_conf.h"
#include "vw5500.h"
#include "tunnel_proto.h"
}

extern "C" void vw_platform_lock(void)   {}
extern "C" void vw_platform_unlock(void) {}

using namespace DAES67;
static HttpServer server;
static std::string big_page;

static const char *cgi_hello(const char *, const char *, int, char *buf)
{
    sprintf(buf, "<html><body><h1>Hello over A2B</h1><p>uptime %lld ms</p></body></html>", (long long)(now_ns()/1000000));
    return buf;
}

static int chunk_count;
static int chunk_numbers(int, int len, char *buf)
{
    if (!buf) { chunk_count = 0; return 0; }             // Server resets the generator first
    if (chunk_count >= 20) { chunk_count = 0; return 0; }
    return snprintf(buf, len, "line %02d of the chunked response\n", chunk_count++);
}

static bool read_full(int fd, uint8_t *p, int n)
{
    while (n > 0)
    {
        int r = (int)recv(fd, p, n, 0);
        if (r <= 0) return false;
        p += r; n -= r;
    }
    return true;
}

int main(int argc, char **argv)
{
    int port = 9500, node = 0;
    double nack_rate = 0;
    double byte_us = 0;                                 // Model the bus: time per byte (25 = 400 kHz, 90 = 100 kHz)
    for (int i=1; i<argc; i++)
    {
        if (!strcmp(argv[i], "--port") && i+1<argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--node") && i+1<argc) node = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--nack-rate") && i+1<argc) nack_rate = atof(argv[++i]);
        else if (!strcmp(argv[i], "--byte-us") && i+1<argc) byte_us = atof(argv[++i]);
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);

    // Bring the virtual chip up exactly as the firmware does with no W5500 fitted
    vw_install((1u<<1)|(1u<<2)|(1u<<3), false);
    uint8_t memsize[2][8] = {{2,2,2,2,2,2,2,2},{2,2,2,2,2,2,2,2}};
    if (ctlwizchip(CW_INIT_WIZCHIP, (void *)memsize) == -1) { printf("CW_INIT_WIZCHIP failed\n"); return 1; }
    uint8_t link = 0;
    ctlwizchip(CW_GET_PHYLINK, &link);
    printf("VERSIONR 0x%02X  PHYLINK %d\n", getVERSIONR(), link);

    for (int i=0; i<200; i++) big_page += "<p>" + std::to_string(i) + " The quick brown fox jumps over the lazy dog over A2B.</p>\n";
    server.start(80, 3, "<!-- preamble -->\n");
    server.add("big.html", big_page.c_str(), (int)big_page.size(), false, TYPE_HTML);
    server.add_cgi("index.html", cgi_hello, false);
    server.add_chunked("chunks.txt", chunk_numbers, TYPE_TEXT, false);
    printf("BIG PAGE %zu bytes\n", big_page.size());

    int ls = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(ls, (sockaddr *)&a, sizeof(a)) < 0 || listen(ls, 2) < 0) { perror("bind"); return 1; }
    printf("SIM I2C LINK on 127.0.0.1:%d (node %d)\n", port, node);

    int cs = -1;
    int64_t next_tick = now_ns();
    uint64_t txn = 0;
    while (1)
    {
        pollfd p = { cs >= 0 ? cs : ls, POLLIN, 0 };
        poll(&p, 1, 1);
        if (cs < 0 && (p.revents & POLLIN))
        {
            cs = accept(ls, nullptr, nullptr);
            setsockopt(cs, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            printf("LINK CONNECTED\n");
        }
        else if (cs >= 0 && (p.revents & (POLLIN|POLLHUP)))
        {
            uint8_t h[6], w[1024], r[1024];
            if (!read_full(cs, h, 6)) { close(cs); cs = -1; printf("LINK CLOSED\n"); continue; }
            int wlen = h[2] | (h[3]<<8), rlen = h[4] | (h[5]<<8);
            if (wlen > (int)sizeof(w) || rlen > (int)sizeof(r) || !read_full(cs, w, wlen)) { close(cs); cs = -1; continue; }
            bool nack = h[1] != node || (nack_rate > 0 && drand48() < nack_rate);
            uint8_t status = nack ? 1 : 0;
            if (!nack)
            {
                // One I2C transaction: write phase, repeated start, read phase, stop
                for (int i=0; i<wlen; i++) tp_rx(w[i], i==0);
                if (h[0] == 'R') for (int i=0; i<rlen; i++) r[i] = tp_tx();
                tp_stop();
                txn++;
            }
            if (byte_us > 0)                            // Address bytes + payload, busy wait for accuracy
            {
                int64_t until = now_ns() + (int64_t)((wlen + (h[0]=='R' ? rlen + 1 : 0) + 1) * byte_us * 1000.0);
                while (now_ns() < until) { if (now_ns() >= next_tick) { next_tick += 1000000LL; server.tick(); } }
            }
            send(cs, &status, 1, 0);
            if (!nack && h[0] == 'R') send(cs, r, rlen, 0);
        }

        if (now_ns() >= next_tick)                      // Same 1 ms tick as core_web.cpp
        {
            next_tick += 1000000LL;
            server.tick();
        }
    }
}
