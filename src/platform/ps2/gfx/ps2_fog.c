/**
 * ps2_fog.c — Fog Emulation for PS2
 *
 * N64 handles fog by generating a fog alpha value in the RSP (per vertex)
 * based on depth, and then the RDP color combiner blends the pixel with 
 * the fog color.
 *
 * On PS2, the GS has native Fog support via the FOG register and FST vertex attribute.
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
    GS_SET_FOGCOL(GS_SETREG_FOGCOL(r, g, b));

    /* 
     * To fully enable GS fog, we also need to:
     * 1. Enable FGE (Fog Enable) in PRIM register when drawing triangles.
     * 2. Pass F (fog factor) for each vertex (0.0 = full fog, 255.0 = no fog).
     * 
     * This will be hooked into the f3dex2_tri1/tri2 submission logic
     * when rsp->geometryMode & G_FOG is true.
     */
}

/**
 * Calculates the fog factor (0-255) for a vertex based on its Z depth.
 */
u8 ps2_fog_calculate_factor(float z, const RSPState* rsp) {
    if (!(rsp->geometryMode & 0x00010000 /* G_FOG */)) {
        return 255; /* No fog */
    }

    /* Basic linear fog calculation based on screen Z */
    /* OoT uses G_MW_FOG (moveword) to set fog min/max distances.
       For now, we approximate a generic fog curve. */
    
    float fogStart = 0.2f;
    float fogEnd = 1.0f;
    
    if (z <= fogStart) return 255;
    if (z >= fogEnd) return 0;
    
    float factor = 1.0f - ((z - fogStart) / (fogEnd - fogStart));
    return (u8)(factor * 255.0f);
}
