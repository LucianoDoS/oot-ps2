#ifndef COLOR_PACKING_H
#define COLOR_PACKING_H

#include "color.h"

/* Packed display-list colors are RRGGBBAA; color structs retain RGBA bytes
 * on both CPUs. A union word aliases those fields only on a big-endian CPU. */
static inline void Color_RGBA8_SetPacked(Color_RGBA8_u32* color, u32 packed) {
#if PLATFORM_PS2
    color->r = packed >> 24;
    color->g = packed >> 16;
    color->b = packed >> 8;
    color->a = packed;
#else
    color->rgba = packed;
#endif
}

static inline u32 Color_RGBA8_GetPacked(const Color_RGBA8_u32* color) {
#if PLATFORM_PS2
    return RGBA8(color->r, color->g, color->b, color->a);
#else
    return color->rgba;
#endif
}

#endif
