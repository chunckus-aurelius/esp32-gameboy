/*
 * Copyright (c) 2026 kuslota
 * SPDX-License-Identifier: MIT
 *
 * Host/ESP32 stand-ins for the CrankBoy helpers that peanut_gb.h expects.
 * reverse_bits_* are adapted from CrankBoy src/utility.h (MIT).
 */

#ifndef ESP32_CRANKBOY_PORT_H
#define ESP32_CRANKBOY_PORT_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FORCE_INLINE __attribute__((always_inline)) inline
#define __section__(x)
#define CB_IRAM_CODE
#define __shell CB_IRAM_CODE
#define CB_FAST_CODE CB_IRAM_CODE
#define clalign __attribute__((aligned(32)))
#define CB_ASSERT(x) ((void)0)

#ifndef likely
#define likely(x) (__builtin_expect(!!(x), 1))
#endif
#ifndef unlikely
#define unlikely(x) (__builtin_expect(!!(x), 0))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

enum cgb_support_e
{
    NON_GB_SYSTEM = 0,
    GB_SUPPORT_DMG = 1,
    GB_SUPPORT_CGB = 2,
    GB_SUPPORT_DMG_AND_CGB = 3,
};

extern int preferences_cgb_speed, preferences_ppu_timing, audio_enabled;

static FORCE_INLINE uint8_t reverse_bits_u8(uint8_t b)
{
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

static FORCE_INLINE uint32_t reverse_bits_in_each_byte_conditional_u16(uint16_t b, bool condition)
{
    if (!condition)
        return b;
    return reverse_bits_u8(b & 0xFF) | (reverse_bits_u8((b >> 8) & 0xFF) << 8);
}

static inline void* cb_malloc(size_t size) { return malloc(size); }
static inline void* cb_calloc(size_t count, size_t size) { return calloc(count, size); }
static inline void* cb_realloc(void* ptr, size_t size) { return realloc(ptr, size); }
static inline void cb_free(void* ptr) { free(ptr); }

static inline void* mallocz(size_t size)
{
    void* p = cb_malloc(size);
    if (p)
        memset(p, 0, size);
    return p;
}

static inline char* aprintf(const char* fmt, ...)
{
    va_list args, copy;
    va_start(args, fmt);
    va_copy(copy, args);
    int len = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    char* out = len < 0 ? NULL : cb_malloc((size_t)len + 1);
    if (out)
        vsnprintf(out, (size_t)len + 1, fmt, args);
    va_end(args);
    return out;
}

#endif
