// Host stand-in for the Arduino core: just enough for the UI code to compile on macOS.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <algorithm>
#include <string>
#ifndef PI
#define PI 3.14159265358979f
#endif
extern uint64_t g_us;
inline uint32_t millis() { return (uint32_t)(g_us / 1000); }
inline uint32_t micros() { return (uint32_t)g_us; }
inline void delay(uint32_t) {}
inline uint32_t esp_random() { return ((uint32_t)rand() << 16) ^ (uint32_t)rand(); }
#define MALLOC_CAP_8BIT 0
inline void* heap_caps_malloc(size_t n, int) { return malloc(n); }
inline void heap_caps_free(void* p) { free(p); }
inline void setCpuFrequencyMhz(int) {}
struct FakeEsp { void restart() {} uint32_t getFreeHeap() { return 100000; } };
static FakeEsp ESP;
