#pragma once
#include "gb_emulator.h"
/* 200x200 1-bit panel: 25 bytes per row, MSB first, set bit is white. */
#define GB_PANEL_BYTES 5000
/* Packed shades are four pixels/byte, leftmost pixel in the low two bits. */
typedef struct {uint8_t previous[GB_PACKED_BYTES]; bool valid;} gb_video_t;
/* Returns the rows changed since the previous call; panel pixels outside the image are untouched. */
unsigned gb_video_convert(gb_video_t *video, const uint8_t *packed, uint8_t *panel);
/* Same output as gb_video_convert. The lookup table is built on first call: use one display task. */
unsigned gb_video_convert_fast(gb_video_t *video, const uint8_t *packed, uint8_t *panel);
/* Exact rational DMG cadence; avoids cumulative rounding per frame. */
static inline uint64_t gb_frame_time_us(uint32_t frames)
{
    const uint64_t numerator = (uint64_t)GB_FRAME_CYCLES * 1000000u;
    return (uint64_t)frames * (numerator / GB_CLOCK_HZ) +
           (uint64_t)frames * (numerator % GB_CLOCK_HZ) / GB_CLOCK_HZ;
}

/* Tracks changed rows like gb_video_convert but fills the image area with black. */
unsigned gb_video_prepare_gray(gb_video_t *video, const uint8_t *packed, uint8_t *panel);
