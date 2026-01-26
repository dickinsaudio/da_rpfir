
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

// Super simple HTTP server written in a way that allows for it to use
// only send small TCP/IP packets and talk to to the network stack in
// small requests, which allows it to work with the W5500 stack and 
// real time audio over IP 
// 

#pragma once

#include <stdint.h> 

namespace DAES67
{
    enum http_mode_t    { HTTP_MODE_NORMAL, HTTP_MODE_CGI, HTTP_MODE_CHUNK, HTTP_MODE_UPLOAD }; 
    enum content_type_t { TYPE_HTML, TYPE_TEXT, TYPE_CSS, TYPE_SCRIPT, TYPE_BINARY };  
    
    extern const char* content_type_string[];
    
    typedef const char *(*cgi_callback_t)   (const char *, const char *, int length, char *buf);    // Passes it a working buffer
    typedef int   (*chunk_callback_t) (int id, int length, char *buf);                              // Passes buffer to write into, return written
    typedef bool  (*upload_callback_t)(int id, int length, char *buf, int remain);                  // Passes data in buffer
    
    typedef struct
    {
        http_mode_t    mode;
        content_type_t typ;
        char        namex[32];
        const char *preamble;
        int         length;
        int         cached;
        bool        download;
        char       *body;
        cgi_callback_t    cgi;
        chunk_callback_t  chunk;
        upload_callback_t upload;
    } content_t;

    typedef struct 
    {
        enum state_t { CLOSED, IDLE, WAITING, REQUEST, UPLOADING, PARSING, RESPONDING, ERROR  } state;
        char *data;             // Current of current data buffer to write
        int   remain;           // Remaining space or words to write from the data buffer 
        char *data2;            // Pointer for the second segment of data 
        int   port;             // The port number to listen in on
        int   sock;             // The socket number
        int   client;           // The client number
#ifndef PICO_BUILD
        bool  keep_alive;       // True if we should keep the connection alive
        int64_t last_active;    // Last time we saw activity on this socket
#endif
        chunk_callback_t   chunk;
        upload_callback_t  upload;
    } socket_t;

    typedef struct 
    {
        uint32_t ip;
        uint32_t requests;
        uint32_t tx_packets;
        uint32_t tx_bytes;
        int64_t  last_seen;
    } client_t;

    class HttpServer
    {
    public:
        HttpServer();
        ~HttpServer();
        bool start(int port=8080, int sockets=4, const char *preamble=nullptr, int *socket_num=nullptr);
        bool stop();
        bool tick();                                // Returns true if there was work done, so while(tick()) clears all work
        
        // HTML CONTENT - Simple and callback generated
        bool add        (const char *name, const char *content, int len,  bool preamble = true, content_type_t type = TYPE_HTML, bool download = false, int cached = 0); 
        bool add_cgi    (const char *name, cgi_callback_t callback,       bool preamble = true, content_type_t type = TYPE_HTML, bool download = false, int cached = 0);               
        bool add_chunked(const char *name, chunk_callback_t callback,     content_type_t type = TYPE_HTML, bool save = false, int cached = 0);               
        bool add_upload (const char *name, upload_callback_t callback);                         // Call an upload - sends nullptr at end

        bool remove(const char *name);
        content_t* find (const char *name, bool create=false);      // Return null if no content found or if no slots free
        char* get_client_list(int length, char *buf);               // Returns a list of clients

        socket_t::state_t get_socket_state(int s) { return s>=0 && s<MAX_SOCKETS ? sock[s].state : socket_t::CLOSED; }

    private:
        static const int MAX_CONTENT = 32;
        static const int MAX_CLIENTS = 16;
#ifdef PICO_BUILD        
        static const int MAX_SOCKETS = 3;
        static const int BUFFER_SIZE = 4096;
        static const int MAX_SEND_SIZE = 128;
        static const int MAX_RECV_SIZE = 128;
#else
        static const int MAX_SOCKETS = 32;
        static const int BUFFER_SIZE = 16384;
        static const int MAX_SEND_SIZE = 1024;
        static const int MAX_RECV_SIZE = 1024;
#endif
        static const int MAX_REQUEST_SIZE = BUFFER_SIZE - 1;
        content_t content[MAX_CONTENT];
    public:
        socket_t  sock[MAX_SOCKETS];
        client_t  client[MAX_CLIENTS];
        char      buffer[MAX_SOCKETS][BUFFER_SIZE]; // Used for the header, CGI and chunk
        int       listener;
        int       socks;
        const char *preamble;
        unsigned int sock_op;                         // Used to fairly cycle through sockets on tick
    };
}
