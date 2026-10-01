/**
 * ps2_fog.c — Fog Emulation for PS2
 *
 * N64 handles fog by generating a fog alpha value in the RSP (per vertex)
 * based on depth, and then the RDP color combiner blends the pixel with 
 * the fog color.
 *
 * On PS2, the GS has native Fog support via FGE and XYZF2 vertices.
 * We map the N64 fog parameters to GS fog.
 */

#include <gsKit.h>
#include <gsInline.h>
#include "ps2_types.h"
#include "rdp_to_gs.h"

/* ─────────────────────────────────────────────────────────── *
 *  Fog State
 * ─────────────────────────────────────────────────────────── */

void ps2_fog_update(RDPState* rdp) {
    if (!rdp->gs) return;

    /* Extract fog color set via G_SETFOGCOLOR */
    u8 r = (rdp->fogColor >> 24) & 0xFF;
    u8 g = (rdp->fogColor >> 16) & 0xFF;
    u8 b = (rdp->fogColor >>  8) & 0xFF;

    /* Set GS FOGCOL register */
    u64* packet = gsKit_heap_alloc(rdp->gs, 1, 16, GIF_AD);
    packet[0] = GIF_TAG(1, 1, 0, 0, 0, 1);
    packet[1] = GIF_AD;
    packet[2] = (u64)r | ((u64)g << 8) | ((u64)b << 16);
    packet[3] = GS_FOGCOL;

}

bool ps2_fog_enabled(const RDPState* rdp, const RSPState* rsp) {
    /* The standard two-cycle fog stage is fog * shadeAlpha + input *
     * (1 - shadeAlpha). Geometry fog supplies that shade alpha. */
    return (rsp->geometryMode & 0x00010000) &&
           ((rdp->otherMode.hi >> 20) & 3) == 1 &&
           (rdp->otherMode.lo & 0xcccc0000u) == 0xc8000000u;
}

/**
 * Calculates the fog factor (0-255) for a vertex based on its Z depth.
 */
u8 ps2_fog_calculate_factor(float z, const RSPState* rsp) {
    if (!(rsp->geometryMode & 0x00010000 /* G_FOG */)) {
        return 255; /* No fog */
    }

    /* gSPFogFactor uses projected Z/W in [-1,1], not normalized GS depth.
     * N64 alpha grows with fog; GS F uses the opposite direction. */
    float alpha = z * rsp->fogMul + rsp->fogOff;
    if (alpha <= 0) return 255;
    if (alpha >= 255) return 0;
    return 255 - (u8)alpha;
}

void ps2_fog_apply_xyz(gs_xyz2* xyz, float z, const RSPState* rsp) {
    u64 packed;
    __builtin_memcpy(&packed, &xyz->xyz, sizeof(packed));
    packed = (packed & 0x00ffffffffffffffull) |
             ((u64)ps2_fog_calculate_factor(z, rsp) << 56);
    __builtin_memcpy(&xyz->xyz, &packed, sizeof(packed));
    xyz->tag = GS_XYZF2;
}
