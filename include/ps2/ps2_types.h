/**
 * ps2_types.h — Type Definitions for OoT PS2 Port
 *
 * Bridges N64 libultra types to PS2 equivalents.
 * Handles the critical Big-Endian (N64) → Little-Endian (PS2) differences.
 */

#ifndef PS2_TYPES_H
#define PS2_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ─────────────────────────────────────────────────────────── *
 *  Basic integer types (libultra compatibility)
 * ─────────────────────────────────────────────────────────── */
#ifdef _EE
#include <tamtypes.h>
#else
typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef int8_t    s8;
typedef int16_t   s16;
typedef int32_t   s32;
typedef int64_t   s64;
#endif
typedef float     f32;
typedef double    f64;

/* libultra uses these as well */
typedef s32       OSPri;
typedef s32       OSId;
typedef u64       OSTime;

/* ─────────────────────────────────────────────────────────── *
 *  Endianness utilities (N64=BE → PS2=LE)
 * ─────────────────────────────────────────────────────────── */

/* Swap bytes of a 16-bit value */
static inline u16 bswap16(u16 x) {
    return (u16)((x >> 8) | (x << 8));
}

/* Swap bytes of a 32-bit value */
static inline u32 bswap32(u32 x) {
    return ((x & 0xFF000000u) >> 24) |
           ((x & 0x00FF0000u) >>  8) |
           ((x & 0x0000FF00u) <<  8) |
           ((x & 0x000000FFu) << 24);
}

/* Swap bytes of a 64-bit value */
static inline u64 bswap64(u64 x) {
    return ((u64)bswap32((u32)(x & 0xFFFFFFFF)) << 32) |
            (u64)bswap32((u32)(x >> 32));
}

/* Read a BE u32 from a byte pointer (used for ROM data) */
static inline u32 read_be32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
           ((u32)p[2] <<  8) |  (u32)p[3];
}

/* Read a BE u16 from a byte pointer */
static inline u16 read_be16(const u8* p) {
    return (u16)(((u16)p[0] << 8) | p[1]);
}

/* ─────────────────────────────────────────────────────────── *
 *  N64 Vector / Matrix types (used throughout OoT)
 * ─────────────────────────────────────────────────────────── */

typedef struct {
    s16 x, y, z;
    s16 pad;
} Vec3s;

typedef struct {
    f32 x, y, z;
} Vec3f;

/* 4×4 matrix as used by RSP F3DEX2 — stored row-major */
typedef f32 MtxF[4][4];

/* Fixed-point 4×4 matrix as used by N64 display lists */
typedef struct {
    s16 intPart[4][4];
    u16 fracPart[4][4];
} Mtx;

/* ─────────────────────────────────────────────────────────── *
 *  Display List command word (64-bit, Big-Endian on N64)
 *  On PS2 we keep storage as u64 but interpret bytes manually.
 * ─────────────────────────────────────────────────────────── */
typedef u64 Gfx;        /* one Display List command   */
typedef Gfx* GfxPtr;    /* pointer into a display list */

/* Extract opcode (high byte of first 32-bit word) */
#define GFX_OPCODE(gfx)   ((u8)(bswap32((u32)((gfx) >> 32)) >> 24))

/* ─────────────────────────────────────────────────────────── *
 *  Alignment helpers
 * ─────────────────────────────────────────────────────────── */
#define ALIGN4(x)   (((x) + 3)  & ~3)
#define ALIGN8(x)   (((x) + 7)  & ~7)
#define ALIGN16(x)  (((x) + 15) & ~15)
#define ALIGN64(x)  (((x) + 63) & ~63)

/* ─────────────────────────────────────────────────────────── *
 *  Compile-time asserts
 * ─────────────────────────────────────────────────────────── */
_Static_assert(sizeof(u8)  == 1, "u8 must be 1 byte");
_Static_assert(sizeof(u16) == 2, "u16 must be 2 bytes");
_Static_assert(sizeof(u32) == 4, "u32 must be 4 bytes");
_Static_assert(sizeof(u64) == 8, "u64 must be 8 bytes");
_Static_assert(sizeof(Gfx) == 8, "Gfx must be 8 bytes (one DL command)");

#endif /* PS2_TYPES_H */
