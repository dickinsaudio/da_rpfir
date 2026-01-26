
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

#include "http_server.hpp"
#include "log.hpp"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <string.h>
#include <histogram.hpp>

#ifdef PICO_BUILD
#include "wizchip_conf.h"

inline int ready_in(int sock)           // Faster alternative to GetSn_RX_RSR
{ 
    uint16_t val0=0,val1=0;
    WIZCHIP_READ_BUF(Sn_RX_RSR(sock), (uint8_t *)&val0, 2);    
    while(val0 != val1)                 // Fall through on 0, or retest for stable
    {
        val1 = val0;
        WIZCHIP_READ_BUF(Sn_RX_RSR(sock), (uint8_t *)&val0, 2);
    }
    return (val0>>8) + ((val0&0xFF)<<8);       // Change endianess
}

inline int ready_out(int sock)          // Faster alternative to GetSn_TX_FSR
{ 
    uint16_t val0=0,val1=0;
    WIZCHIP_READ_BUF(Sn_TX_FSR(sock), (uint8_t *)&val0, 2);    
    while(val0 != val1)                 // Fall through on 0, or retest for stable
    {
        val1 = val0;
        WIZCHIP_READ_BUF(Sn_TX_FSR(sock), (uint8_t *)&val0, 2);
    }
    return (val0>>8) + (val0<<8);       // Change endianess
}


#define MAX_SPI_PARCEL                 32                       // Break any SPI transfers into smaller (let in AES ISR)
#define close(sn)                      my_close(sn)
#define socket(sn, proto, port, flags) my_socket(sn, port)
#define listen(sn)                     my_listen(sn)
#define disconnect(sn)                 my_disconnect(sn)
#define send(sn, buf, len, flags)      my_send(sn, buf, len)
#define recv(sn, buf, len, flags)      my_recv(sn, buf, len)
#define SOCK_OK 1
#define SOCKERR_SOCKSTATUS -7;
#define SOCKERR_SOCKCLOSED -4;

// W5500 library has some isses with listen and close that can create deadlocks
// under high load.  Avoid using while(getSn_SR(sn) ! = ....) on a single state
// Using these as a leaner version of sockets compatible functions for TCP.

inline int8_t my_close(uint8_t sn) 
{
    while (getSn_SR(sn) != SOCK_CLOSED)
    {
        setSn_CR(sn,Sn_CR_CLOSE);
        while(getSn_CR(sn)) {};
	    if (getSn_SR(sn) == SOCK_CLOSED) break;
        printf("Extra close\n");
        sleep_ms(1);
    };
    setSn_IR(sn, 0xFF);
	return SOCK_OK;
}

// For TCP and listening on specific port only
int8_t my_socket(uint8_t sn, uint16_t port)
{
	my_close(sn);
    setSn_MR(sn, (Sn_MR_TCP));
    setSn_PORT(sn,port);	
    setSn_CR(sn,Sn_CR_OPEN);
    while(getSn_CR(sn));
    while(getSn_SR(sn) == SOCK_CLOSED);
    return (int8_t)sn;
}

inline int8_t my_listen(uint8_t sn)
{
	setSn_CR(sn,Sn_CR_LISTEN);
	while(getSn_CR(sn));
    int SR = getSn_SR(sn);
    if (SR == SOCK_LISTEN || SR == SOCK_ESTABLISHED) return SOCK_OK;
    my_close(sn);
    return SOCKERR_SOCKCLOSED;
}

int8_t my_disconnect(uint8_t sn)
{
	setSn_CR(sn,Sn_CR_DISCON);
	while(getSn_CR(sn));
    return SOCK_OK;
}

// Break the SPI transfers up
int32_t my_send(uint8_t sn, uint8_t * buf, uint16_t len)
{
    if (len==0) return 0;
    uint8_t tmp = getSn_SR(sn);
    if(tmp != SOCK_ESTABLISHED) return SOCKERR_SOCKSTATUS;
    int freesize = ready_out(sn);
    if (len > freesize) len = freesize;         // check size not to exceed MAX size.
    if (len==0) return 0;                       // No space available

    uint16_t ptr = getSn_TX_WR(sn);
    uint32_t addrsel = 0;
    uint16_t remain = len;

    while (remain)
    {
        uint8_t state = getSn_SR(sn);
        if (state != SOCK_ESTABLISHED) return SOCKERR_SOCKSTATUS;           // Likely client closed
        int chunk = MAX_SPI_PARCEL;
        if (chunk > remain) chunk = remain;
        addrsel = ((uint32_t)ptr << 8) + (WIZCHIP_TXBUF_BLOCK(sn) << 3);
        WIZCHIP_WRITE_BUF(addrsel, buf, chunk);
        ptr    += chunk;
        buf    += chunk;
        remain -= chunk;
    }

    setSn_TX_WR(sn,ptr);
    setSn_CR(sn,Sn_CR_SEND);
    while(getSn_CR(sn));
    return (int32_t)(len-remain);
}

int32_t my_recv(uint8_t sn, uint8_t * buf, uint16_t len)
{
    uint8_t  tmp = getSn_SR(sn);
    if(tmp != SOCK_ESTABLISHED) return SOCKERR_SOCKSTATUS;
    int recvsize = ready_in(sn);
    if(recvsize < len) len = recvsize;   
    if (len==0) return 0;

    uint16_t ptr = getSn_RX_RD(sn);
    uint32_t addrsel = 0;
    uint16_t remain = len;

    while (remain)
    {
        uint8_t state = getSn_SR(sn);
        if (state != SOCK_ESTABLISHED) return SOCKERR_SOCKSTATUS;
        int chunk = MAX_SPI_PARCEL;
        if (chunk > remain) chunk = remain;
        addrsel = ((uint32_t)ptr << 8) + (WIZCHIP_RXBUF_BLOCK(sn) << 3);
        WIZCHIP_READ_BUF(addrsel, buf, chunk);
        ptr    += chunk;
        buf    += chunk;
        remain -= chunk;
    }

    setSn_RX_RD(sn,ptr);
    setSn_CR(sn,Sn_CR_RECV);
    while(getSn_CR(sn));
    return (int32_t)(len-remain);
}


#else
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <fcntl.h> 
#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/if.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <poll.h>
#include <signal.h>
inline int ready_in(int sock) { 
    int len; 
    if (ioctl(sock, FIONREAD, &len)<0) return 0;
    return len; 
};
#endif


namespace DAES67
{

const char* content_type_string[] = { "text/html", "text/plain", "text/css", "text/javascript", "application/octet-stream" };

HttpServer::HttpServer() 
{ 
    for (int n=0; n<MAX_CONTENT; n++)
    {
        content[n].namex[0] = 0;
        content[n].preamble = nullptr;
        content[n].body = nullptr;
        content[n].length = 0;
        content[n].cgi = nullptr;
        content[n].chunk = nullptr;
    }
    for (int n=0; n<MAX_CLIENTS; n++)
    {
        client[n].ip = 0;
        client[n].last_seen = 0;
        client[n].requests = 0;
        client[n].tx_packets = 0;
        client[n].tx_bytes = 0;
    }

#ifndef PICO_BUILD    
    signal(SIGPIPE, SIG_IGN);           // Ensure we get returns on send/recv to closed sockets
#endif
};

bool HttpServer::start(int port, int sockets, const char* pre, int *socket_num)
{
    if (sockets > MAX_SOCKETS) sockets = MAX_SOCKETS;
    socks = sockets;
    preamble = pre;
    sock_op=0;

    for (int n=0; n<socks; n++)
    {
        sock[n].data = nullptr; 
        sock[n].remain = 0;
        sock[n].state = socket_t::CLOSED;
        sock[n].port  = port;
        if (socket_num) sock[n].sock = socket_num[n];
        else            sock[n].sock = -1;
    }
    listener = 0;

#ifndef PICO_BUILD
    listener = socket(AF_INET, SOCK_STREAM, 0); 
    if (listener < 0)       
    { 
        Error("LISTENER Failed to create socket.\n"); 
        listener = 0;  
        return false; 
    };

    int flags = fcntl(listener, F_GETFL, 0);            // Get the file status flags for the socket
    fcntl(listener, F_SETFL, flags | O_NONBLOCK);       // Add non blocking

    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        Error("LISTENER Failed to bind socket %d to port %d", listener, port);
        close(listener);
        listener = 0;
        return false;
    }
    
    if (listen(listener, 3*sockets) < 0) 
    {
        Error("LISTENER Failed to listen socket %d to port %d", listener, port);
        close(listener);
        return false;
    }    
#endif
    return true;
}

bool HttpServer::stop()
{
    if (listener) close(listener);
    for (int n=0; n<socks; n++)
        if (sock[n].sock>=0) close(sock[n].sock);
    socks=0;
#ifdef PICO_BUILD
    sleep_ms(20);
#endif
    return true;
}

HttpServer::~HttpServer()
{
    stop();
}


static char C2D(char c)
{
    c = tolower(c); 
    if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return 10 + c -'a';
	return (char)c;
}

void unescape_http_url(char * url)
{
	int x, y;
	for (x = 0, y = 0; url[y]; ++x, ++y) {
		if ((url[x] = url[y]) == '%') {
			url[x] = C2D(url[y+1])*0x10+C2D(url[y+2]);
			y+=2;
		}
	}
	url[x] = '\0';
}

bool HttpServer::tick()
{
    bool work = false;
    if (socks==0) return work;
    int scan = socks;
    while (scan-- && !work)        // Cycle through sockets, but only do one action
    {
        int s   = sock_op;
        sock_op = (sock_op+1) % socks;

#ifndef PICO_BUILD                  // Timeout any keep alive connections after 3 seconds
        if (sock[s].state == socket_t::WAITING && sock[s].keep_alive && now_ns() - sock[s].last_active > 3 * 1000000000LL) 
        {
            close(sock[s].sock);
            sock[s].sock = -1;
            sock[s].state = socket_t::CLOSED;
            continue;
        }    
#else
        // Catch some socket state changes from far end as a backup / recover
        if (sock[s].sock>0)
        {
            int state = getSn_SR(sock[s].sock);
            if (state == SOCK_CLOSE_WAIT && sock[s].state!=socket_t::CLOSED)  // The client has signalled a close - disconnect immediately
            { 
                disconnect(sock[s].sock);
                sock[s].state = socket_t::CLOSED;
                sock[s].sock = -1;
                work = true;
                continue; 
            };
            if (state == SOCK_CLOSED     && sock[s].state!=socket_t::CLOSED)  // The socket is closed, so we can move to use it again right away
            { 
                sock[s].state = socket_t::CLOSED;  
                sock[s].sock = -1;
                work = true;
                continue;
            }
            if (state == SOCK_INIT       && sock[s].state!=socket_t::IDLE)    // If for some reason we ever get stuck here, move along
            {   
                printf("CATCH SLOT %d INIT\n", s); 
                listen(sock[s].sock); work=true; 
                sock[s].state = socket_t::IDLE; 
                continue; 
            };
        }
#endif

        switch (sock[s].state)  
        {
            case socket_t::CLOSED: {
                work = true;    // We will definitely do something
#ifdef PICO_BUILD
                if (sock[s].sock<0) sock[s].sock = s+1;
                if(socket(sock[s].sock, Sn_MR_TCP, sock[s].port,0x00) != sock[s].sock)
                {
                    Error("Failed to open W5500 socket %d to listen on port %d", s+1, sock[s].port); 
                    sock[s].state = socket_t::ERROR; 
                    break;
                }
                listen(sock[s].sock);
#else
                if (sock[s].sock>=0) close(sock[s].sock);
                sock[s].sock=-1;
                sock[s].keep_alive = false;
                sock[s].last_active = 0;
                static bool first = true;
                if (listener==0 && first) { Error("Listener not created."); first=false; break; };
#endif
                sock[s].state = socket_t::IDLE;
                break;
            }

            case socket_t::IDLE: {
#ifdef PICO_BUILD                
                if (getSn_SR(sock[s].sock) == SOCK_ESTABLISHED)
                {
                    uint32_t ip;
                    getSn_DIPR(sock[s].sock, (uint8_t *)&ip);
#else
                struct sockaddr_in addr;
                socklen_t addr_len = sizeof(addr);
                int fd = accept(listener, (struct sockaddr *)&addr, &addr_len);
                if (fd>=0)
                {

                    int fl = fcntl(fd, F_GETFL, 0);
                    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
                    int one = 1;
                    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
                    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));

                    uint32_t ip = addr.sin_addr.s_addr;
                    sock[s].sock = fd;
#endif
                    sock[s].state = socket_t::WAITING;
                    int n=0, oldest = 0;
                    int64_t  now    = now_ns();
                    int64_t  old    = now;
                    for (; n<MAX_CLIENTS; n++)
                    {
                        if (client[n].ip == 0 || client[n].ip == ip) break;
                        if (client[n].last_seen < old) { old = client[n].last_seen; oldest = n; };
                    }
                    if (n<MAX_CLIENTS) sock[s].client = n;
                    else
                    {
                        sock[s].client = oldest;
                        client[oldest].ip = 0;
                        client[oldest].last_seen = 0;
                        client[oldest].requests = 0;
                        client[oldest].tx_packets = 0;
                        client[oldest].tx_bytes = 0;
                    }
                    client[sock[s].client].requests++;
                    client[sock[s].client].ip = ip;
                    client[sock[s].client].last_seen = now;
                }
                break;
            }

            case socket_t::WAITING: {
                int len = ready_in(sock[s].sock);
                if (len>0)
                {
#ifndef PICO_BUILD
                    sock[s].last_active = now_ns();
#endif
                    sock[s].state  = socket_t::REQUEST;
                    sock[s].remain = MAX_REQUEST_SIZE;
                    sock[s].data   = buffer[s];
                    sock[s].data[0]= '\0';
                }
                if (len<0)
                {
#ifdef PICO_BUILD                    
                    disconnect(sock[s].sock);     
#else
                    close(sock[s].sock);
                    sock[s].sock = -1;
#endif                    
                    sock[s].state = socket_t::CLOSED;
                    break;
                }
                break;
            }

            case socket_t::REQUEST: {
                int len = ready_in(sock[s].sock);
                if (len > 0)
                {
                    work = true;
#ifndef PICO_BUILD
                    sock[s].last_active = now_ns();
#endif
                    if (len > sock[s].remain) len = sock[s].remain;
                    if (len > MAX_RECV_SIZE)  len = MAX_RECV_SIZE;
                    len = recv(sock[s].sock, (uint8_t *)sock[s].data, len, MSG_DONTWAIT);
                    if (len > 0)
                    {
                        sock[s].data += len;
                        sock[s].data[0] = '\0';          // Keep it always null termianted
                        sock[s].remain -= len;
                    }
                    else                                // Reset socket here because of some error
                    {
                        sock[s].state = socket_t::CLOSED;
                    }
                }
                if (len==0 || sock[s].remain==0)        // If we have a full request, move to parsing
                {
                    sock[s].state = socket_t::PARSING;
                }
                break;
            }

            case socket_t::PARSING: {
                work = true;
                char *meth = strtok(buffer[s], " ");
                char *url  = strtok(NULL, " \n\r\0");
                char *data = strtok(NULL, "\0");
                int   data_len = MAX_REQUEST_SIZE - sock[s].remain - (data-buffer[s]);

                enum method_t { GET, POST, HEAD, UNKNOWN } method = UNKNOWN;
                if (meth) 
                {
                    if (!strcasecmp(meth,"GET"))  method = GET;
                    if (!strcasecmp(meth,"POST")) method = POST;
                    if (!strcasecmp(meth,"HEAD")) method = HEAD;
                }

#ifndef PICO_BUILD
                sock[s].keep_alive = true;  // Default to keep alive unless connection: close seen      
                char *conn_hdr = strcasestr(buffer[s], "Connection:");
                if (conn_hdr && strcasestr(conn_hdr, "close")) 
                {
                    sock[s].keep_alive = false;
                }
#endif

                char *header = buffer[s];               // Place to make the response header
                header[0] = '\0';                       // Ensure it is null terminated
                sock[s].remain = 0;                     // By default there is no body

                if (method != UNKNOWN)
                {
                    char *args = strchr(url, '?');                  // Find the first '?'
                    if (args != nullptr) { *args = '\0'; args++; }  // Move to the arguments part
                    else args = (char *)"";                         // No arguments

                    char *file = url;                               // The file part is now 
                    if (file[0] == '/') file++;                     // the modified url
                    if (*file == '\0') file = (char *)"index.html";

                    content_t *content, temp;
                    if (strcmp(file, "clients")==0)
                    { 
                        temp.body = get_client_list(sizeof(buffer[s])-200, buffer[s]+200);
                        temp.mode = HTTP_MODE_NORMAL;
                        temp.typ = TYPE_TEXT;
                        strncpy(temp.namex, "clients", sizeof(temp.namex));
                        temp.length = strlen(temp.body);
                        temp.cached = 0;
                        temp.preamble = nullptr;
                        temp.download = false;
                        temp.cgi      = nullptr;
                        temp.chunk    = nullptr;
                        temp.upload   = nullptr;
                        content = &temp;
                    }
                    else content = find(file);

                    // Avoid responding to pre-fetches from Chrome prefetch optimization
                    if (data_len>0 && (strstr(data, "Prefetch") || strstr(data, "PREFETCH") || strstr(data, "prefetch")))
                    {
                        header = (char *)"HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/html\r\n\r\nService Unavailable\r\n\r\n";
                    }
                    else if (content)
                    {
                        if (method == POST && content->mode == HTTP_MODE_UPLOAD && data)
                        {
                            sock[s].remain =0;
                            char *length = strstr(data, "Content-Length:");
                            char *start  = strstr(data, "\r\n\r\n");

                            int   len    = 0;
                            if (length)
                            {
                                length += 15;
                                while(*length==' ') length++;
                                char *end = strstr(length, "\r\n");
                                if (end) { *end = '\0'; len = atoi(length); };
                            }
                            if (len>0 && start)
                            {
                                start+=4;
                                if (content->upload)
                                {
                                    sock[s].upload = content->upload;
                                    sock[s].upload(s, data_len - (int)(start-data), start, len);
                                    sock[s].remain = len - (data_len - (int)(start-data));
                                    sock[s].state = socket_t::UPLOADING;
                                }
                            }
                            break;  // No need to respond
                        }  
                        
                        sock[s].upload = nullptr;       // Clear the upload and chunk callbacks
                        sock[s].chunk  = nullptr;       //
                        sock[s].data   = nullptr;       // Clear the data buffer
                        sock[s].data2  = nullptr;       // Clear the second segment of data

                        char *tmp, save[32];            // Copy the file name if we need to save it
                        tmp = strrchr(file, '/'); if (!tmp) tmp=file;
                        snprintf(save,sizeof(save),"%s",tmp);
                        char *p= header;

                        if (content->mode == HTTP_MODE_CGI)
                        {
                            if (method == GET || method == POST) content->body = (char *)content->cgi(file, args, sizeof(buffer[s])-200, buffer[s]+200);
                            else content->body = (char *)"";
                        }
                        
                        p += sprintf(p, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n",content_type_string[content->typ]);

                        switch (content->mode)
                        {
                            case HTTP_MODE_CGI: 
                            case HTTP_MODE_NORMAL: {
                                int data_length;
                                if (content->length) data_length = content->length;
                                else data_length = strlen(content->body);
                                sock[s].data  = content->body;              // So the data is the body
                                sock[s].remain = data_length;               // And the remaining length
                                if (content->preamble)                      // 
                                {                                           // If there is a preamble
                                    data_length+=strlen(content->preamble); // Then add the length
                                    sock[s].data2 = sock[s].data;           // Shift the data to the second segment
                                    sock[s].data = (char *)content->preamble;  // The data is the preamble
                                    sock[s].remain = strlen(sock[s].data);  // And the remaining length
                                }
                                p += sprintf(p, "Content-Length: %d\r\n", data_length);
                                break; 
                            }; 

                            case HTTP_MODE_CHUNK: {
                                p += sprintf(p, "Transfer-Encoding: chunked\r\n");
                                content->chunk(s,0,nullptr);                    // Clear any previous chunk
                                sock[s].chunk = content->chunk;                 // Indicate that this is chunked callback
                                sock[s].data = nullptr;                         // First chunck is created in RESPOND
                                sock[s].remain = 0;    
                                break;
                            };

                            case HTTP_MODE_UPLOAD:                              // Should not happen
                            default:       
                                break;
                        }

                        if (content->mode == HTTP_MODE_NORMAL || content->mode == HTTP_MODE_CGI || content->mode == HTTP_MODE_CHUNK)
                        {
#ifndef PICO_BUILD
                            if (sock[s].keep_alive) 
                            {
                                p += sprintf(p, "Connection: keep-alive\r\n");
                                p += sprintf(p, "Keep-Alive: timeout=2, max=1000\r\n");
                            } 
                            else 
                            {
                                p += sprintf(p, "Connection: close\r\n");
                            }
#else
                            p += sprintf(p, "Connection: close\r\n");
#endif
                        }

                        if (content->cached>0) p += sprintf(p, "Cache-Control: public, max-age=%d\r\n", content->cached);
                        if (content->download) p += sprintf(p, "Content-Disposition: attachment; filename=\"%s\"\r\n", save);
                        p += sprintf(p, "\r\n");
                    }
                    else                    header = (char *)"HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n404 Not Found";
                }
                else                        header = (char *)"HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\n\r\n400 Bad Request";
                
                int len = strlen(header);
                while (len)
                {
                    int sent = send(sock[s].sock, (uint8_t *)header, len>MAX_SEND_SIZE?MAX_SEND_SIZE:len, MSG_DONTWAIT |  MSG_NOSIGNAL);
                    if (sent<0)  { sock[s].state = socket_t::CLOSED; break; };
                    if (sent==0) { };       // Just a reminder that this can happen when W5500 is full
                    len -= sent;
                    header += sent;
                    client[sock[s].client].tx_packets++;
                    client[sock[s].client].tx_bytes += sent;
                }

                if (method == HEAD) sock[s].remain = 0;                     // Only send header

                if ( ( sock[s].remain==0 && !sock[s].chunk ) || sock[s].state == socket_t::CLOSED) 
                {
#ifdef PICO_BUILD                    
                    disconnect(sock[s].sock);     
                    sock[s].state = socket_t::CLOSED;
#else
                    if (sock[s].keep_alive && sock[s].state != socket_t::CLOSED) 
                    {
                        sock[s].state = socket_t::WAITING;
                        sock[s].last_active = now_ns();
                    } 
                    else 
                    {
                        close(sock[s].sock);
                        sock[s].sock = -1;
                        sock[s].state = socket_t::CLOSED;
                    }
#endif
                }
                else sock[s].state = socket_t::RESPONDING; // If we have no data to send, then move to RESPONDING
                break;
            }

            case socket_t::UPLOADING: {
                if (sock[s].remain<0 || !sock[s].upload) { sock[s].state = socket_t::CLOSED; break; };

                if (sock[s].remain>0)
                {
                    int len = ready_in(sock[s].sock);
                    if (len > 0)
                    {
                        work = true;
                        if (len > sock[s].remain) len = sock[s].remain;
                        if (len > MAX_RECV_SIZE)  len = MAX_RECV_SIZE;
                        len = recv(sock[s].sock, (uint8_t *)buffer[s], len, MSG_DONTWAIT);
                        sock[s].remain -= len;
                        if (sock[s].upload) sock[s].upload(s, len, buffer[s], sock[s].remain);
                        break;
                    }
                }
                if (sock[s].remain==0) sock[s].upload(s, 0, nullptr, 0);
#ifdef PICO_BUILD
                disconnect(sock[s].sock);        // A healthy disconnect
#else
                close(sock[s].sock);
                sock[s].sock = -1;
#endif
                sock[s].state = socket_t::CLOSED;
                break;
            }

            case socket_t::RESPONDING: {
                work = true;                
                if (sock[s].remain>0)                       // First send any remaining data                
                {                    
                    int len = sock[s].remain;
                    if (len > MAX_SEND_SIZE) len = MAX_SEND_SIZE;
                    len = send(sock[s].sock, (uint8_t *)sock[s].data, len, MSG_DONTWAIT |  MSG_NOSIGNAL);
                    if (len > 0)
                    {
#ifndef PICO_BUILD
                        sock[s].last_active = now_ns();
#endif
                        sock[s].data += len;
                        sock[s].remain -= len;
                        client[sock[s].client].tx_packets++;
                        client[sock[s].client].tx_bytes += len;
                    }
                    if (len<0)                              // An error, but it could be client side
                    {                                       // so we just move to CLOSED state and
                        sock[s].state = socket_t::CLOSED;
                    }
                    if (len==0) { };                        // Just a reminder that this can happen when W5500 is full
                }
                else if (sock[s].data2)                     // If we have a second segment of data
                {
                    sock[s].data = sock[s].data2;           // Then send that
                    sock[s].data2 = nullptr;
                    sock[s].remain = strlen(sock[s].data);
                }
                else if (sock[s].chunk)                     // If we have a chunked response, send the next chunk
                {
                    int len = sock[s].chunk(s, BUFFER_SIZE-9, buffer[s]+6);
                    if (len>0)
                    {
                        buffer[s][len+6] = '\r'; buffer[s][len+7] = '\n'; buffer[s][len+8] = '\0';
                        snprintf(buffer[s],5, "%04"  PRIX16, (int16_t)len); buffer[s][4] = '\r'; buffer[s][5] = '\n';
                        sock[s].data = buffer[s];
                        sock[s].remain = len+8;                 // Leave off the last '\0'
                    }
                    else
                    {
                        sock[s].chunk = nullptr;                // No more chunks
                        strcpy(buffer[s], "0\r\n\r\n");    // Signal end by zero length chunk
                        sock[s].data = buffer[s];
                        sock[s].remain = 5;
                    }
                }
                else
                {
#ifdef PICO_BUILD
                    disconnect(sock[s].sock);        // A healthy disconnect
                    sock[s].state = socket_t::CLOSED;
#else
                    if (sock[s].keep_alive) 
                    {
                        sock[s].state = socket_t::WAITING;
                        sock[s].last_active = now_ns();
                    } 
                    else 
                    {
                        sock[s].state = socket_t::CLOSED;
                        close(sock[s].sock);
                        sock[s].sock = -1;
                    }
#endif
                } 
            } break;

            case socket_t::ERROR: break;
        }
    }
    return work;
}

bool HttpServer::add(const char *name, const char *data, int len, bool header, content_type_t type, bool download, int cached)
{
    content_t *content = find(name, true);
    if (!content) return false;
    content->mode       = HTTP_MODE_NORMAL;
    content->typ        = type;
    content->preamble   = header ? preamble : nullptr;
    content->length     = len;
    content->cached     = cached;
    content->download   = download;
    content->body       = (char *)data;
    content->cgi        = nullptr;
    content->chunk      = nullptr;
    content->upload     = nullptr;
    return true;
}

bool HttpServer::add_cgi(const char *name, cgi_callback_t callback, bool header, content_type_t type, bool download, int cached)
{
    content_t *content = find(name, true);
    if (!content) return false;
    content->mode       = HTTP_MODE_CGI;
    content->typ        = type;
    content->preamble   = header ? preamble : nullptr;
    content->length     = 0;
    content->cached     = cached;
    content->download   = download;
    content->body       = nullptr;
    content->cgi        = callback;
    content->chunk      = nullptr;    
    content->upload     = nullptr;
    return true;
}

bool HttpServer::add_chunked (const char *name, chunk_callback_t callback, content_type_t type, bool download, int cached)
{
    content_t *content = find(name, true);
    if (!content) return false;
    content->mode       = HTTP_MODE_CHUNK;
    content->typ        = type;
    content->preamble   = nullptr;
    content->length     = 0;
    content->cached     = cached;
    content->download   = download;    
    content->body       = nullptr;
    content->cgi        = nullptr;
    content->chunk      = callback;
    content->upload     = nullptr;
    return true;
}

bool HttpServer::add_upload(const char *name, upload_callback_t callback)
{
    content_t *content = find(name, true);
    if (!content) return false;
    content->mode       = HTTP_MODE_UPLOAD;
    content->typ        = TYPE_BINARY;
    content->preamble   = nullptr;
    content->length     = 0;
    content->cached     = 0;
    content->download   = false;
    content->body       = nullptr;
    content->cgi        = nullptr;
    content->chunk      = nullptr;
    content->upload     = callback;
    return true;
}        

content_t * HttpServer::find(const char *name, bool create)
{
    int n;
    if (name==nullptr) return nullptr;
    for (n=0; n<MAX_CONTENT; n++)
    {
        if (content[n].namex[0] != '\0' && strcmp(content[n].namex, name)==0) break;
    }

    if (!create) return n<MAX_CONTENT ? &content[n] : nullptr;

    if (n==MAX_CONTENT)
        for (n=0; n<MAX_CONTENT; n++) 
            if (content[n].namex[0] == '\0') break;

    if (n==MAX_CONTENT) return nullptr;

    snprintf(content[n].namex,sizeof(content[n].namex),"%s",name);
    content[n].preamble = nullptr;
    content[n].body = nullptr;
    content[n].length = 0;
    content[n].cgi = nullptr;
    content[n].chunk = nullptr;
    content[n].upload = nullptr;
    content[n].mode = HTTP_MODE_NORMAL;
    content[n].typ = TYPE_HTML;
    content[n].download = false;
    content[n].cached = 0;
    return &content[n];
}    

bool HttpServer::remove(const char *name)
{
    content_t *content = find(name);
    if (!content) return false;
    content->namex[0] = '\0';
    return true;
}

char* HttpServer::get_client_list(int length, char *buf)
{
    if (buf==nullptr) return nullptr;
    buf[0] = '\0';
    int n=0;
    n += snprintf(buf,length,"CLIENT IP        REQUESTS    TXPCKTS    TXBYTES     SEEN\n");
    //                        xxx.yyy.zzz.www 123456789 1234567890 1234567890 12345678
    int64_t now = now_ns();
    for (int i=0; i<MAX_CLIENTS; i++)
    {
        if (client[i].ip)
        {
            char ip[32]; 
            char *p = (char *)&client[i].ip;
            snprintf(ip, sizeof(ip), "%d.%d.%d.%d", p[0], p[1], p[2], p[3]);
            n += snprintf(buf+n, length-n, "%-15s %9" PRIu32 " %10" PRIu32 " %10" PRIu32 " %8" PRId32 "\n",ip, 
                        client[i].requests, client[i].tx_packets, client[i].tx_bytes, 
                        (int32_t)((now - client[i].last_seen)/1000000000LL));
            if (n>=length) break;
        }
    }
    return buf;
}


} // namespace DAES67

