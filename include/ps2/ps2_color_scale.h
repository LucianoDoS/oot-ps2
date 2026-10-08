#ifndef PS2_COLOR_SCALE_H
#define PS2_COLOR_SCALE_H

#include "ps2_types.h"

/* Exact floor(value * 128 / 255) for a byte, including the opaque endpoint.
 * Restrict the input to u8: products and wider intermediate values require
 * their original division rather than this byte-only identity. */
static inline u8 ps2_color_scale_byte(u8 value) {
    return (value >> 1) + (value == 255);
}

#endif
