#include "gb_video.h"
#include <string.h>
unsigned gb_video_convert_fast(gb_video_t *video, const uint8_t *packed, uint8_t *panel)
{
    static uint8_t lookup[4][256];
    static bool ready;
    if (!ready) {
        static const uint8_t bayer[4][4] = {{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
        static const uint8_t thresholds[4] = {0,5,11,16};
        for (unsigned y = 0; y < 4; ++y)
            for (unsigned byte = 0; byte < 256; ++byte) {
                unsigned nibble = 0;
                for (unsigned x = 0; x < 4; ++x)
                    if (bayer[y][x] >= thresholds[(byte >> (x * 2)) & 3]) nibble |= 8u >> x;
                lookup[y][byte] = nibble;
            }
        ready = true;
    }
    unsigned rows = 0;
    for (unsigned y = 0; y < GB_HEIGHT; ++y) {
        const uint8_t *line = packed + y * GB_PACKED_ROW;
        uint8_t *old = video->previous + y * GB_PACKED_ROW;
        if (video->valid && !memcmp(old, line, GB_PACKED_ROW)) continue;
        ++rows;
        const uint8_t *map = lookup[y & 3];
        uint8_t *dst = panel + (y + 28) * 25 + 2;
        /* x=20 starts in the lower nibble; retain both outside margins. */
        dst[0] = (dst[0] & 0xf0) | map[line[0]];
        for (unsigned x = 1; x < 20; ++x)
            dst[x] = (map[line[2*x-1]] << 4) | map[line[2*x]];
        dst[20] = (dst[20] & 0x0f) | (map[line[39]] << 4);
        memcpy(old, line, GB_PACKED_ROW);
    }
    video->valid = true;
    return rows;
}

unsigned gb_video_convert(gb_video_t *video, const uint8_t *packed, uint8_t *panel)
{
    static const uint8_t bayer[4][4] = {{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    static const uint8_t thresholds[4] = {0,5,11,16};
    unsigned rows = 0;
    for (unsigned y=0; y<GB_HEIGHT; ++y) {
        const uint8_t *line=packed+y*GB_PACKED_ROW;
        uint8_t *old=video->previous+y*GB_PACKED_ROW;
        if(video->valid && memcmp(old,line,GB_PACKED_ROW)==0) continue;
        ++rows;
        for(unsigned x=0;x<GB_WIDTH;++x) {
            unsigned shade=(line[x/4]>>((x%4)*2))&3;
            unsigned offset=(y+28)*25+(x+20)/8,mask=0x80u>>((x+20)%8);
            if(bayer[y%4][x%4]<thresholds[shade]) panel[offset]&=(uint8_t)~mask;
            else panel[offset]|=mask;
        }
        memcpy(old,line,GB_PACKED_ROW);
    }
    video->valid=true;
    return rows;
}

unsigned gb_video_prepare_gray(gb_video_t *video, const uint8_t *packed, uint8_t *panel)
{
    unsigned rows = 0;
    for (unsigned y=0; y<GB_HEIGHT; ++y) {
        const uint8_t *line = packed+y*GB_PACKED_ROW;
        uint8_t *old = video->previous+y*GB_PACKED_ROW;
        if (!video->valid || memcmp(old, line, GB_PACKED_ROW)) {
            ++rows;
            memcpy(old, line, GB_PACKED_ROW);
        }
        uint8_t *dst = panel+(y+28)*25+2;
        dst[0] &= 240u;
        memset(dst+1, 0, 19);
        dst[20] &= 15u;
    }
    video->valid = true;
    return rows;
}
