// Native stand-in for daes67_lite/log.hpp (PICO_BUILD path pulls in the SDK)
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
namespace DAES67 {
inline int64_t now_ns() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec*1000000000LL + t.tv_nsec; }
}
inline void sleep_ms(unsigned ms) { usleep(ms*1000); }
#define Error(format, ...)   printf("ERROR " format "\n", ## __VA_ARGS__)
#define Notice(format, ...)  printf(format "\n", ## __VA_ARGS__)
