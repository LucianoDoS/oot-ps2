/**
 * rdp_color.c — RDP Color / Render Mode Commands for PS2
 *
 * Implements the "state" side of the RDP: colors, blending,
 * combine mode, other mode, scissor, rectangles, etc.
 *
 * The N64 RDP has a sophisticated 2-cycle color combiner:
 *   output = (A - B) * C + D   (per component, per cycle)
 * with ~64 possible input sources per slot.
 *
 * On PS2 we approximate this using the GS register set:
 *   - ALPHA    → blending equation
 *   - TEX0/1   → texture parameters
 *   - RGBAQ    → vertex color (handles prim/env color injection)
 *   - TEST     → alpha test, depth test
 *
 * This file also implements:
 *   G_SETSCISSOR  → GS SCISSOR register
 *   G_FILLRECT    → gsKit_prim_sprite (filled rectangle)
 *   G_TEXRECT     → gsKit_prim_sprite_texture (textured rect, used for 2D HUD/menus)
 *   G_SETCIMG     → track framebuffer pointer
 *   G_SETZIMG     → track z-buffer pointer
 */

#include <gsKit.h>
#include <gsInline.h>
#include <string.h>
#include <stdio.h>

#include "ps2_types.h"
#include "ps2_segment.h"
#include "rdp_to_gs.h"
#include "ps2_texture.h"

/* ─────────────────────────────────────────────────────────── *
 *  Utility: pack RGBA u8s into a u32 (R in MSB)
 * ─────────────────────────────────────────────────────────── */
static inline u32 pack_rgba(u8 r, u8 g, u8 b, u8 a) {
    return ((u32)r << 24) | ((u32)g << 16) | ((u32)b << 8) | a;
}

/* PS2 GS alpha 0–0x80 (128 = fully opaque); N64 uses 0–255 */
static inline u8 n64_alpha_to_gs(u8 a) {
    return (u8)((u32)a * 128 / 255);
}

/* ─────────────────────────────────────────────────────────── *
 *  G_SETPRIMCOLOR
 *  w0: [15:8]=minlod, [7:0]=lodfrac
 *  w1: RRGGBBAA
 * ─────────────────────────────────────────────────────────── */
void rdp_setprimcolor(u32 w0, u32 w1, RDPState* rdp) {
    (void)w0;
    rdp->primColor = w1;  /* store as RRGGBBAA */
}

/* ─────────────────────────────────────────────────────────── *
 *  G_SETENVCOLOR — environment color (used in combine modes)
 * ─────────────────────────────────────────────────────────── */
void rdp_setenvcolor(u32 w0, u32 w1, RDPState* rdp) {
    (void)w0;
    rdp->envColor = w1;
}

/* ─────────────────────────────────────────────────────────── *
 *  G_SETFOGCOLOR
 * ─────────────────────────────────────────────────────────── */
void rdp_setfogcolor(u32 w0, u32 w1, RDPState* rdp) {
    (void)w0;
    rdp->fogColor = w1;
}

/* ─────────────────────────────────────────────────────────── *
 *  G_SETBLENDCOLOR
 * ─────────────────────────────────────────────────────────── */
void rdp_setblendcolor(u32 w0, u32 w1, RDPState* rdp) {
    (void)w0;
    rdp->blendColor = w1;
}

/* ─────────────────────────────────────────────────────────── *
 *  G_SETFILLCOLOR — used for fast rectangle fills (clear screen)
 * ─────────────────────────────────────────────────────────── */
void rdp_setfillcolor(u32 w0, u32 w1, RDPState* rdp) {
    (void)w0;
    rdp->fillColor = w1;
}

/* ─────────────────────────────────────────────────────────── *
 *  G_SETCOMBINE — color combiner mode
 *
 *  The combiner is complex (16-bit mode selector).
 *  On PS2 we read the mode and set GS ALPHA register accordingly
 *  as a best approximation. Common OoT modes:
 *
 *  SHADE           → per-vertex color passthrough
 *  SHADE+TEX       → texture * vertex color (most geometry)
 *  PRIM            → constant primitive color
 *  TEX * PRIM      → tinted texture (e.g. red heart pieces)
 *  NOISE           → approximated with random noise (special)
 * ─────────────────────────────────────────────────────────── */

/* Color combiner input sources (for mode decoding) */
#define CC_TEXEL0   1
#define CC_PRIM     3
#define CC_SHADE    4
#define CC_ENV      5

void rdp_setcombine(u32 w0, u32 w1, RDPState* rdp) {
    rdp->combiner.w0 = w0;
    rdp->combiner.w1 = w1;

    /* Decode cycle 1 color A source (bits 52–55 of 64-bit command) */
    /* w0 bits [28:24] = a0 (color A cycle 0) */
    u8 a0 = (w0 >> 20) & 0xF;

    /* Choose GS ALPHA equation based on dominant mode:
     *   GS_ALPHA(A, B, C, D): out = (A-B)*C>>7 + D
     *   Common mapping: A=src, B=0, C=dst_alpha, D=dst */

    GSGLOBAL* gs = rdp->gs;
    if (!gs) return;

    if (a0 == CC_PRIM) {
        /* Prim color mode: use constant color from PRIM register */
        u8 r = (rdp->primColor >> 24) & 0xFF;
        u8 g = (rdp->primColor >> 16) & 0xFF;
        u8 b = (rdp->primColor >>  8) & 0xFF;
        u8 a = n64_alpha_to_gs(rdp->primColor & 0xFF);
        /* Will be applied as vertex RGBAQ when drawing */
        rdp->combiner.r = r;
        rdp->combiner.g = g;
        rdp->combiner.b = b;
        rdp->combiner.a = a;
    } else if (a0 == CC_ENV) {
        u8 r = (rdp->envColor >> 24) & 0xFF;
        u8 g = (rdp->envColor >> 16) & 0xFF;
        u8 b = (rdp->envColor >>  8) & 0xFF;
        u8 a = n64_alpha_to_gs(rdp->envColor & 0xFF);
        rdp->combiner.r = r;
        rdp->combiner.g = g;
        rdp->combiner.b = b;
        rdp->combiner.a = a;
    } else {
        /* Default: passthrough (SHADE or TEXEL0) */
        rdp->combiner.r = 0x80;
        rdp->combiner.g = 0x80;
        rdp->combiner.b = 0x80;
        rdp->combiner.a = 0x80;
    }
}

/* ─────────────────────────────────────────────────────────── *
 *  G_RDPSETOTHERMODE — sets pipeline, cycle, blend formula
 *
 *  Key bits we care about in the low word (w1):
 *    [29:28] = cycle type: 0=1-cycle, 1=2-cycle, 2=copy, 3=fill
 *    [19:16] = alpha compare mode
 *    [15:14] = depth source (0=pixel Z, 1=prim depth)
 *    [13:0]  = blender configuration (2-cycle blend formula)
 *
 *  Blender cycle 0 config (bits 13–10):
 *    P, A, Q, B sources → we map to GS ALPHA register
 * ─────────────────────────────────────────────────────────── */

/* Blender sources */
#define BL_CLR_IN   0   /* input color */
#define BL_CLR_MEM  1   /* framebuffer color */
#define BL_CLR_BL   2   /* blend color */
#define BL_CLR_FOG  3   /* fog color */
#define BL_A_IN     0   /* input alpha */
#define BL_A_FOG    1   /* fog alpha */
#define BL_A_SHADE  2   /* shade alpha */
#define BL_1MA      0   /* 1 - mem_alpha */
#define BL_A_MEM    1   /* mem alpha */
#define BL_1        2   /* 1.0 */
#define BL_0        3   /* 0.0 */

/* ─────────────────────────────────────────────────────────── *
 *  rdp_apply_othermode — traduce el other mode actual a registros GS
 *
 *  Se extrae a su propia función porque ahora la llaman TRES sitios
 *  (el comando completo y las dos variantes parciales).
 *
 *  Corrección importante: los desplazamientos de bits del blender
 *  estaban mal. En el other mode de N64 el blender vive en los bits
 *  [31:16] de la palabra baja, repartidos en dos ciclos:
 *      P1[31:30] A1[29:28] M1[27:26] B1[25:24]
 *      P2[23:22] A2[21:20] M2[19:18] B2[17:16]
 *  El código anterior leía P de [31:30] pero B de [19:18], mezclando
 *  ciclos distintos.
 * ─────────────────────────────────────────────────────────── */
void rdp_apply_othermode(RDPState* rdp) {
    GSGLOBAL* gs = rdp->gs;
    if (!gs) return;

    u32 lo = rdp->otherMode.lo;
    u32 hi = rdp->otherMode.hi;

    /* Tipo de ciclo: bits [21:20] de la palabra ALTA */
    u8 cycleType = (u8)((hi >> 20) & 0x3);

    /* Comparación de alfa: bits [1:0] de la palabra baja */
    u8 alphaCompare = (u8)(lo & 0x3);

    /* Blender, primer ciclo */
    u8 P1 = (u8)((lo >> 30) & 0x3);
    u8 A1 = (u8)((lo >> 28) & 0x3);
    u8 M1 = (u8)((lo >> 26) & 0x3);
    u8 B1 = (u8)((lo >> 24) & 0x3);
    (void)A1; (void)M1;

    u64 gsAlpha;
    if (cycleType == 3) {
        /* Modo fill: sin mezcla */
        gsAlpha = GS_SETREG_ALPHA(0, 1, 0, 1, 0);
    } else if (P1 == BL_CLR_IN && B1 == BL_1MA) {
        /* Mezcla alfa clásica: src*a + dst*(1-a) */
        gsAlpha = GS_SETREG_ALPHA(0, 1, 0, 1, 0x80);
    } else if (P1 == BL_CLR_IN && B1 == BL_0) {
        gsAlpha = GS_SETREG_ALPHA(0, 1, 0, 1, 0);     /* opaco */
    } else if (P1 == BL_CLR_FOG) {
        gsAlpha = GS_SETREG_ALPHA(1, 0, 1, 0, 0x80);  /* niebla */
    } else {
        gsAlpha = GS_SETREG_ALPHA(0, 1, 0, 1, 0x80);
    }

    rdp->gs_alpha = gsAlpha;
    gsKit_set_primalpha(gs, gsAlpha, 0);

    /* Test de alfa + test de profundidad, en UN solo registro TEST.
     * Antes se escribían por separado desde dos archivos distintos y el
     * segundo pisaba al primero. */
    u32 zmode = (lo >> 10) & 0x3;
    bool zCompare = (lo & 0x0010) != 0;
    (void)zmode;

    int ate  = (alphaCompare == 1) ? 1 : 0;
    int atst = 6;      /* GEQUAL */
    int aref = 0x40;   /* umbral típico de OoT */
    int zte  = 1;      /* el GS exige ZTE=1; para desactivar se usa ZTST=1 */
    int ztst = zCompare ? 2 : 1;   /* 2 = GEQUAL, 1 = ALWAYS */

    u64 test = GS_SETREG_TEST(ate, atst, aref, 0, 0, 0, zte, ztst);
    rdp->gs_test = test;
    gsKit_set_test(gs, test);
}

void rdp_setothermode(u32 w0, u32 w1, RDPState* rdp) {
    rdp->otherMode.hi = w0 & 0x00FFFFFF;
    rdp->otherMode.lo = w1;
    rdp_apply_othermode(rdp);
}

/* ─────────────────────────────────────────────────────────── *
 *  G_SETOTHERMODE_H / _L  (NUEVOS)
 *
 *  Estos faltaban por completo, y son los que OoT usa de verdad:
 *  gDPSetCycleType, gDPSetTextureFilter, gDPSetRenderMode,
 *  gDPSetAlphaCompare... todos se codifican como modificaciones
 *  PARCIALES del other mode, no como el comando completo.
 *
 *  Codificación F3DEX2:
 *    w0[15:8] = longitud - 1  (bits afectados)
 *    w0[ 7:0] = desplazamiento
 *    w1       = valor a insertar
 * ─────────────────────────────────────────────────────────── */
static u32 om_insert(u32 current, u32 w0, u32 w1) {
    u32 len   = ((w0 >> 0) & 0xFF) + 1;
    u32 shift = ((w0 >> 8) & 0xFF);
    /* F3DEX2 guarda el shift "al revés": 32 - shift - len */
    if (shift > 32) shift = 32;
    u32 mask = (len >= 32) ? 0xFFFFFFFFu : (((1u << len) - 1u) << shift);
    return (current & ~mask) | (w1 & mask);
}

void rdp_setothermode_h(u32 w0, u32 w1, RDPState* rdp) {
    rdp->otherMode.hi = om_insert(rdp->otherMode.hi, w0, w1);
    rdp_apply_othermode(rdp);
}

void rdp_setothermode_l(u32 w0, u32 w1, RDPState* rdp) {
    rdp->otherMode.lo = om_insert(rdp->otherMode.lo, w0, w1);
    rdp_apply_othermode(rdp);
}

/* ─────────────────────────────────────────────────────────── *
 *  G_SETSCISSOR — define the render scissor rectangle
 *  Coordinates are 10.2 fixed-point.
 * ─────────────────────────────────────────────────────────── */
void rdp_setscissor(u32 w0, u32 w1, RDPState* rdp) {
    rdp->scissorX0 = (w0 >> 12) & 0xFFF;
    rdp->scissorY0 = (w0      ) & 0xFFF;
    rdp->scissorX1 = (w1 >> 12) & 0xFFF;
    rdp->scissorY1 = (w1      ) & 0xFFF;

    /* Convert from 10.2 → pixel coordinates */
    u16 x0 = rdp->scissorX0 >> 2;
    u16 y0 = rdp->scissorY0 >> 2;
    u16 x1 = rdp->scissorX1 >> 2;
    u16 y1 = rdp->scissorY1 >> 2;

    if (rdp->gs) {
        GS_SET_SCISSOR(GS_SETREG_SCISSOR(x0, x1, y0, y1));
    }
}

/* ─────────────────────────────────────────────────────────── *
 *  G_FILLRECT — filled rectangle (used for screen clears)
 *  w0: [23:12]=x1, [11:0]=y1  (lower-right, 10.2 fixed)
 *  w1: [23:12]=x0, [11:0]=y0  (upper-left,  10.2 fixed)
 * ─────────────────────────────────────────────────────────── */
void rdp_fillrect(u32 w0, u32 w1, RDPState* rdp) {
    float x1 = (float)((w0 >> 12) & 0xFFF) / 4.0f;
    float y1 = (float)((w0      ) & 0xFFF) / 4.0f;
    float x0 = (float)((w1 >> 12) & 0xFFF) / 4.0f;
    float y0 = (float)((w1      ) & 0xFFF) / 4.0f;

    if (!rdp->gs) return;

    /* Decode fill color as RGBA16 (two packed pixels) */
    u16 fc16 = (u16)(rdp->fillColor >> 16);
    u8 r = ((fc16 >> 11) & 0x1F) << 3;
    u8 g = ((fc16 >>  6) & 0x1F) << 3;
    u8 b = ((fc16 >>  1) & 0x1F) << 3;
    u8 a = (fc16 & 1) ? 0x80 : 0;

    u64 color = GS_SETREG_RGBA(r, g, b, a);
    gsKit_prim_sprite(rdp->gs, x0, y0, x1, y1, 1, color);
}

/* ─────────────────────────────────────────────────────────── *
 *  G_TEXRECT — textured rectangle (2D sprites, HUD, menus)
 *
 *  The TEXRECT command is 3 DL commands long:
 *    cmd 0: G_TEXRECT  + x1/y1
 *    cmd 1: tile + x0/y0
 *    cmd 2: s/t coords + ds/dt (texture velocity, usually 0)
 * ─────────────────────────────────────────────────────────── */
void rdp_texrect(u8 op, u32 w0, u32 w1, const Gfx* pc, RDPState* rdp) {
    (void)op;

    /* Command 0: x1, y1 (lower-right, 10.2 fixed) */
    float x1 = (float)((w0 >> 12) & 0xFFF) / 4.0f;
    float y1 = (float)((w0      ) & 0xFFF) / 4.0f;
    u8    tile_idx = (w1 >> 24) & 0x7;

    /* Comando 1 (pc+1): x0, y0
     * ANTES: se leía con read_be32 incondicionalmente. Los TEXRECT del
     * HUD y los menús los construye el juego en tiempo de ejecución, o
     * sea LITTLE-endian, así que se decodificaban al revés y el HUD
     * salía en coordenadas absurdas. Ahora usa el mismo modo que el
     * resto del intérprete (ver ps2_segment.h). */
    u32 c1w0 = GFX_READ_W0(pc + 1);
    u32 c1w1 = GFX_READ_W1(pc + 1);
    float x0 = (float)((c1w1 >> 12) & 0xFFF) / 4.0f;
    float y0 = (float)((c1w1      ) & 0xFFF) / 4.0f;
    (void)c1w0;

    /* Command 2 (pc+2): texture S/T start + dS/dT */
    u32 c2w0 = GFX_READ_W0(pc + 2);
    u32 c2w1 = GFX_READ_W1(pc + 2);
    float s0 = (float)(s16)((c2w0 >> 16) & 0xFFFF) / 32.0f;
    float t0 = (float)(s16)( c2w0        & 0xFFFF) / 32.0f;
    float ds = (float)(s16)((c2w1 >> 16) & 0xFFFF) / 1024.0f;
    float dt = (float)(s16)( c2w1        & 0xFFFF) / 1024.0f;
    (void)ds; (void)dt;

    if (!rdp->gs) return;

    TileDescriptor* tile = &rdp->tile[tile_idx];
    if (!tile->gsTextureLoaded) {
        ps2_texture_upload(tile, rdp);
    }

    float s1 = s0 + (x1 - x0);
    float t1 = t0 + (y1 - y0);

    gsKit_prim_sprite_texture(
        rdp->gs, &tile->gsTexture,
        x0, y0,   s0, t0,
        x1, y1,   s1, t1,
        1,
        GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80));
}

/* ─────────────────────────────────────────────────────────── *
 *  G_SETCIMG — set color image (framebuffer) pointer
 * ─────────────────────────────────────────────────────────── */
void rdp_setcimg(u32 w0, u32 w1, RDPState* rdp) {
    rdp->colorImage = ps2_segmented_to_virtual(w1);
    rdp->fbWidth    = (w0 & 0xFFF) + 1;
    rdp->fbFmt      = (w0 >> 19) & 0x7;
    /* On PS2 we always render to the gsKit framebuffer;
     * we track this pointer for reference but don't use it directly. */
}

/* ─────────────────────────────────────────────────────────── *
 *  G_SETZIMG — set z-buffer pointer
 * ─────────────────────────────────────────────────────────── */
void rdp_setzimg(u32 w0, u32 w1, RDPState* rdp) {
    (void)w0;
    rdp->zImage = ps2_segmented_to_virtual(w1);
    /* PS2 GS z-buffer is managed by gsKit; we track for reference. */
}

/* ─────────────────────────────────────────────────────────── *
 *  ps2_rdp_begin_frame() — called at start of each frame
 *  Resets per-frame GS state.
 * ─────────────────────────────────────────────────────────── */
void ps2_rdp_begin_frame(RDPState* rdp) {
    if (!rdp->gs) return;
    gsKit_clear(rdp->gs, GS_SETREG_RGBA(0, 0, 0, 0x80));

    /* Invalidate all cached texture uploads each frame
     * (OoT reuses the same TMEM slots for different textures) */
    for (int i = 0; i < MAX_TILES; i++) {
        rdp->tile[i].gsTextureLoaded = false;
    }
}
