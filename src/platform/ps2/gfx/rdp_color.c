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
#include "ps2_gfx_profile.h"

/* These are the last values emitted, rather than the desired RDP values.
 * Temporary depth clears and ground blend passes must use the same helpers.
 * Invalidate at each task because gsKit manages frame/context setup itself. */
static struct {
    GSGLOBAL* gs;
    u64 value[2][3];
    u8 valid[2];
} s_gsState;

void ps2_gs_state_reset(void) { memset(&s_gsState, 0, sizeof(s_gsState)); }

static bool gs_state_changed(RDPState* rdp, unsigned slot, u64 value) {
    GSGLOBAL* gs = rdp->gs;
    if (s_gsState.gs != gs) { ps2_gs_state_reset(); s_gsState.gs = gs; }
    unsigned context = gs->PrimContext & 1;
    if ((s_gsState.valid[context] & (1u << slot)) && s_gsState.value[context][slot] == value) {
        rdp->stats.stateReuse++; return false;
    }
    s_gsState.value[context][slot] = value;
    s_gsState.valid[context] |= 1u << slot;
    rdp->stats.stateWrites++;
    return true;
}

void ps2_gs_set_alpha(RDPState* rdp, u64 alpha) {
    if (!rdp->gs) return;
    rdp->gs->PrimAlpha = alpha;
    rdp->gs->PABE = 0;
    if (gs_state_changed(rdp, 0, alpha)) gsKit_set_primalpha(rdp->gs, alpha, 0);
}

void ps2_gs_set_test(RDPState* rdp) {
    if (!rdp->gs) return;
    GSTEST* test = rdp->gs->Test;
    u64 value = GS_SETREG_TEST(test->ATE,test->ATST,test->AREF,test->AFAIL,
                              test->DATE,test->DATM,test->ZTE,test->ZTST);
    if (gs_state_changed(rdp, 1, value)) gsKit_set_test(rdp->gs, 0);
}

void ps2_gs_set_zbuf(RDPState* rdp, u64 zbuf) {
    if (!rdp->gs || !gs_state_changed(rdp, 2, zbuf)) return;
    u64* packet = gsKit_heap_alloc(rdp->gs, 1, 16, GIF_AD);
    packet[0] = GIF_TAG_AD(1); packet[1] = GIF_AD;
    packet[2] = zbuf; packet[3] = GS_ZBUF_1 + rdp->gs->PrimContext;
}

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
    if (rdp->primLodFrac == (w0 & 255) && rdp->primColor == w1) return;
    rdp->primLodFrac = w0 & 255;
    rdp->primColor = w1;  /* store as RRGGBBAA */
    for (int i = 0; i < MAX_TILES; i++) rdp->tile[i].gsTextureLoaded = false;
}

/* ─────────────────────────────────────────────────────────── *
 *  G_SETENVCOLOR — environment color (used in combine modes)
 * ─────────────────────────────────────────────────────────── */
void rdp_setenvcolor(u32 w0, u32 w1, RDPState* rdp) {
    (void)w0;
    if (rdp->envColor == w1) return;
    rdp->envColor = w1;
    for (int i = 0; i < MAX_TILES; i++) rdp->tile[i].gsTextureLoaded = false;
}

/* ─────────────────────────────────────────────────────────── *
 *  G_SETFOGCOLOR
 * ─────────────────────────────────────────────────────────── */
void rdp_setfogcolor(u32 w0, u32 w1, RDPState* rdp) {
    (void)w0;
    rdp->fogColor = w1;
    ps2_fog_update(rdp);
}

/* ─────────────────────────────────────────────────────────── *
 *  G_SETBLENDCOLOR
 * ─────────────────────────────────────────────────────────── */
void rdp_setblendcolor(u32 w0, u32 w1, RDPState* rdp) {
    (void)w0;
    rdp->blendColor = w1;
    /* The low byte is the threshold used by G_AC_THRESHOLD. */
    if ((rdp->otherMode.lo & 3) == 1) rdp_apply_othermode(rdp);
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
    if (rdp->combiner.w0 == w0 && rdp->combiner.w1 == w1) return;
    rdp->combiner.w0 = w0;
    rdp->combiner.w1 = w1;
    for (int i = 0; i < MAX_TILES; i++) rdp->tile[i].gsTextureLoaded = false;

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

static void combiner_sources(const RDPState* rdp, int cycle, bool alpha, u8 s[4]) {
    u32 a = rdp->combiner.w0, b = rdp->combiner.w1;
    if (alpha) {
        s[0] = cycle ? (b >> 21) & 7 : (a >> 12) & 7;
        s[1] = cycle ? (b >> 3) & 7 : (b >> 12) & 7;
        s[2] = cycle ? (b >> 18) & 7 : (a >> 9) & 7;
        s[3] = cycle ? b & 7 : (b >> 9) & 7;
    } else {
        s[0] = cycle ? (a >> 5) & 15 : (a >> 20) & 15;
        s[1] = cycle ? (b >> 24) & 15 : (b >> 28) & 15;
        s[2] = cycle ? a & 31 : (a >> 15) & 31;
        s[3] = cycle ? (b >> 6) & 7 : (b >> 15) & 7;
    }
}

typedef struct {
    u32 w0, w1, type, blend, primMask, envMask;
    u32 lookupPrimMask[4], lookupEnvMask[4];
    bool valid, shade[2], dynamicAlpha, textureLerp, pathLerp;
    u8 sources[2][2][4];
} CombinerAnalysis;

/* Decode once per material, not once per vertex/channel. Key the words
 * directly so callers which restore an RDPState also invalidate the analysis. */
static const CombinerAnalysis* combiner_analysis(const RDPState* rdp) {
    static CombinerAnalysis cache;
    u32 type = (rdp->otherMode.hi >> 20) & 3;
    u32 blend = rdp->otherMode.lo & 0x5003;
    if (cache.valid && cache.w0 == rdp->combiner.w0 && cache.w1 == rdp->combiner.w1 &&
        cache.type == type && cache.blend == blend) return &cache;
    memset(&cache, 0, sizeof(cache));
    cache.w0 = rdp->combiner.w0; cache.w1 = rdp->combiner.w1;
    cache.type = type; cache.blend = blend; cache.valid = true;
    int cycles = type == 1 ? 2 : 1;
    for (int cycle = 0; cycle < cycles; cycle++) for (int alpha = 0; alpha < 2; alpha++) {
        u8* sources = cache.sources[cycle][alpha];
        combiner_sources(rdp, cycle, alpha, sources);
        for (int slot = 0; slot < 4; slot++) {
            u8 source = sources[slot];
            if (source == 4 || (!alpha && slot == 2 && source == 11)) cache.shade[alpha] = true;
            if (type == 2 || (!cache.w0 && !cache.w1)) continue;
            u32 mask = alpha ? 255u : 0xffffff00u;
            if (!alpha && slot == 2 && source >= 7 && source <= 12) {
                if (source == 10) cache.primMask |= 255;
                if (source == 12) cache.envMask |= 255;
            } else {
                if (source == 3) cache.primMask |= mask;
                if (source == 5) cache.envMask |= mask;
            }
        }
    }
    const u8 expected[2][2][4] = {{{2,1,12,1},{7,7,7,6}},{{0,15,4,7},{7,7,7,0}}};
    cache.textureLerp = type == 1 && !blend && !memcmp(cache.sources, expected, sizeof(expected));
    if (cache.textureLerp) cache.envMask &= ~255u;
    cache.pathLerp = type == 1 && cache.w0 == 0xfc262a04u && cache.w1 == 0x1f5893f8u;
    const u8* alpha = cache.sources[0][1];
    cache.dynamicAlpha = type == 0 && (alpha[0] == 1 || alpha[0] == 2) &&
        alpha[1] == 7 && alpha[2] == 3 && alpha[3] == 7 && cache.sources[0][0][2] != 10;
    /* Each output channel may depend on an earlier cycle's channel or alpha.
     * Keep conservative unions of constant inputs, including factors that
     * might cancel algebraically, so reusing a LUT never drops a dependency. */
    if (type != 2 && (cache.w0 || cache.w1)) {
        for (int cycle = 0; cycle < cycles; cycle++) {
            u32 nextPrim[4] = {0}, nextEnv[4] = {0};
            for (int component = 0; component < 4; component++) {
                const u8* selectors = cache.sources[cycle][component == 3];
                for (int slot = 0; slot < 4; slot++) {
                    unsigned source = selectors[slot];
                    if (component != 3 && slot == 2 && source >= 7 && source <= 12) {
                        if (source == 7) {
                            nextPrim[component] |= cache.lookupPrimMask[3];
                            nextEnv[component] |= cache.lookupEnvMask[3];
                        } else if (source == 10) nextPrim[component] |= 255u;
                        else if (source == 12) nextEnv[component] |= 255u;
                    } else if (source == 0 && !(component == 3 && slot == 2)) {
                        nextPrim[component] |= cache.lookupPrimMask[component];
                        nextEnv[component] |= cache.lookupEnvMask[component];
                    } else if (source == 3) nextPrim[component] |= 255u << ((3-component)*8);
                    else if (source == 5) nextEnv[component] |= 255u << ((3-component)*8);
                }
            }
            memcpy(cache.lookupPrimMask, nextPrim, sizeof(nextPrim));
            memcpy(cache.lookupEnvMask, nextEnv, sizeof(nextEnv));
        }
        /* This material defers primitive alpha to the GS vertex; its texture
         * alpha LUT is the identity rather than the scalar alpha equation. */
        if (cache.dynamicAlpha) cache.lookupPrimMask[3] = cache.lookupEnvMask[3] = 0;
    }
    return &cache;
}

bool ps2_combiner_uses_shade(const RDPState* rdp, bool alpha) {
    return combiner_analysis(rdp)->shade[alpha];
}
bool ps2_combiner_texture_lerp(const RDPState* rdp) {
    return combiner_analysis(rdp)->textureLerp;
}
bool ps2_combiner_path_lerp(const RDPState* rdp) {
    return combiner_analysis(rdp)->pathLerp;
}
void ps2_combiner_constants(const RDPState* rdp, u32* prim, u32* env) {
    const CombinerAnalysis* analysis = combiner_analysis(rdp);
    *prim = rdp->primColor & analysis->primMask;
    *env = rdp->envColor & analysis->envMask;
}
bool ps2_combiner_dynamic_alpha(const RDPState* rdp) {
    return combiner_analysis(rdp)->dynamicAlpha;
}

void ps2_combiner_lookup_dependencies(const RDPState* rdp, u32 primMasks[4], u32 envMasks[4]) {
    const CombinerAnalysis* analysis = combiner_analysis(rdp);
    memcpy(primMasks, analysis->lookupPrimMask, sizeof(analysis->lookupPrimMask));
    memcpy(envMasks, analysis->lookupEnvMask, sizeof(analysis->lookupEnvMask));
}

static int combiner_input(const RDPState* rdp, int source, int slot, bool alpha, int component,
                          const u8 texel[4], const u8 shade[4], const u8 combined[4]) {
    if (!alpha && slot == 2 && source >= 7 && source <= 12) {
        switch (source) {
            case 7: return combined[3];
            case 8: case 9: return texel[3];
            case 10: return rdp->primColor & 255;
            case 11: return shade[3];
            case 12: return rdp->envColor & 255;
        }
    }
    switch (source) {
        case 0: return (alpha && slot == 2) ? 0 : combined[component];
        case 1: case 2: return texel[component]; /* TEXEL1 needs a second texture path. */
        case 3: return (rdp->primColor >> ((3 - component) * 8)) & 255;
        case 4: return shade[component];
        case 5: return (rdp->envColor >> ((3 - component) * 8)) & 255;
        case 6: return (slot == 2) ? 0 : 255;
        default: return 0;
    }
}

void ps2_combiner_color(const RDPState* rdp, const u8 texel[4], const u8 shade[4], u8 output[4]) {
    /* Copy cycle bypasses the color combiner entirely. */
    if (((rdp->otherMode.hi >> 20) & 3) == 2) { memcpy(output, texel, 4); return; }
    if (!rdp->combiner.w0 && !rdp->combiner.w1) { memcpy(output, texel, 4); return; }
    const CombinerAnalysis* analysis = combiner_analysis(rdp);
    u8 combined[4] = {0};
    int cycles = ((rdp->otherMode.hi >> 20) & 3) == 1 ? 2 : 1;
    for (int cycle = 0; cycle < cycles; cycle++) {
        u8 next[4];
        for (int component = 0; component < 4; component++) {
            bool alpha = component == 3;
            const u8* sources = analysis->sources[cycle][alpha];
            int inputs[4];
            for (int slot = 0; slot < 4; slot++)
                inputs[slot] = combiner_input(rdp, sources[slot], slot, alpha, component,
                                               texel, shade, combined);
            int value = (inputs[0] - inputs[1]) * inputs[2] / 255 + inputs[3];
            next[component] = value < 0 ? 0 : value > 255 ? 255 : value;
        }
        memcpy(combined, next, 4);
    }
    memcpy(output, combined, 4);
}

/* Build a material's LUT without resolving source selectors thousands of
 * times. References 0..255 are constants, 256 is the changing texel channel,
 * and 257..260 refer to the previous cycle's RGBA. Keep the scalar evaluator
 * above as the general vertex path and an independent diagnostic oracle. */
typedef struct {
    u16 source[4];
    u8 operation;
} LookupEquation;

static int vertex_color_value(u16 source, const u8 shade[4], const u8 combined[4]) {
    return source < 256 ? source : source < 260 ? shade[source - 256] : combined[source - 260];
}

/* Untextured polygons use white texels. Compile their fixed selectors and
 * constants once, retaining all four shade channels and both cycle clamps.
 * The scalar evaluator above remains the independent reference. */
void ps2_combiner_vertex_color(const RDPState* rdp, const u8 shade[4], u8 output[4]) {
    static struct {
        bool valid, shadeOnly;
        u32 w0, w1, type, prim, env;
        int cycles;
        LookupEquation equations[2][4];
    } cache;
    u32 type = (rdp->otherMode.hi >> 20) & 3;
    if (type == 2 || (!rdp->combiner.w0 && !rdp->combiner.w1)) {
        memset(output, 255, 4); return;
    }
    if (!cache.valid || cache.w0 != rdp->combiner.w0 || cache.w1 != rdp->combiner.w1 ||
        cache.type != type || cache.prim != rdp->primColor || cache.env != rdp->envColor) {
        cache.valid = true; cache.w0 = rdp->combiner.w0; cache.w1 = rdp->combiner.w1;
        cache.type = type; cache.prim = rdp->primColor; cache.env = rdp->envColor;
        cache.cycles = type == 1 ? 2 : 1;
        const u8 white[4] = {255,255,255,255}, zero[4] = {0}, markers[4] = {1,2,3,4};
        for (int cycle = 0; cycle < cache.cycles; cycle++) for (int component = 0; component < 4; component++) {
            LookupEquation* equation = &cache.equations[cycle][component];
            u8 selectors[4];
            combiner_sources(rdp, cycle, component == 3, selectors);
            for (int slot = 0; slot < 4; slot++) {
                int base = combiner_input(rdp, selectors[slot], slot, component == 3, component, white, zero, zero);
                int variable = combiner_input(rdp, selectors[slot], slot, component == 3, component, white, markers, zero);
                int combined = cycle ? combiner_input(rdp, selectors[slot], slot, component == 3, component, white, zero, markers) : base;
                equation->source[slot] = variable != base ? 255 + variable : combined != base ? 259 + combined : base;
            }
            equation->operation = equation->source[0] == equation->source[1] || equation->source[2] == 0 ? 1 :
                                  equation->source[2] == 255 ? 2 : 0;
            if (equation->source[0] < 256 && equation->source[1] < 256 &&
                equation->source[2] < 256 && equation->source[3] < 256) {
                int result = equation->source[3] + ((int)equation->source[0] - equation->source[1]) * equation->source[2] / 255;
                equation->source[3] = result < 0 ? 0 : result > 255 ? 255 : result;
                equation->operation = 1;
            }
        }
        cache.shadeOnly = true;
        for (int component = 0; component < 4; component++) {
            const LookupEquation* equation = &cache.equations[cache.cycles - 1][component];
            if (equation->operation != 1 || equation->source[3] != 256 + component) cache.shadeOnly = false;
        }
    }
    if (cache.shadeOnly) { memcpy(output, shade, 4); return; }
    u8 combined[4] = {0};
    for (int cycle = 0; cycle < cache.cycles; cycle++) {
        u8 next[4];
        for (int component = 0; component < 4; component++) {
            const LookupEquation* equation = &cache.equations[cycle][component];
            int result = vertex_color_value(equation->source[3], shade, combined);
            if (equation->operation != 1) {
                int difference = vertex_color_value(equation->source[0], shade, combined) -
                                 vertex_color_value(equation->source[1], shade, combined);
                result += equation->operation == 2 ? difference :
                          difference * vertex_color_value(equation->source[2], shade, combined) / 255;
            }
            next[component] = result < 0 ? 0 : result > 255 ? 255 : result;
        }
        memcpy(combined, next, 4);
    }
    memcpy(output, combined, 4);
}

static int lookup_value(u16 source, int value, const u8 combined[4]) {
    return source < 256 ? source : source == 256 ? value : combined[source - 257];
}
#ifdef PS2_RENDERER_SMOKE_CAPTURE
unsigned ps2_combiner_test_folded_passes;
unsigned ps2_combiner_test_retained_passes;
#endif
static bool lookup_copies_combined(const LookupEquation* equation, unsigned component) {
    unsigned previous = 257u + component;
    return equation->operation == 1 ? equation->source[3] == previous :
        equation->operation == 2 && equation->source[0] == previous &&
        equation->source[1] == 0 && equation->source[3] == 0;
}
void ps2_combiner_build_lookup_channels(const RDPState* rdp, u8 values[4][256], unsigned channelMask) {
    channelMask &= 15u;
    if (!channelMask) return;
    const CombinerAnalysis* analysis = combiner_analysis(rdp);
    bool bypass = analysis->type == 2 || (!analysis->w0 && !analysis->w1);
    if (bypass) {
        for (int value = 0; value < 256; value++) {
            for (int component = 0; component < 3; component++)
                if (channelMask & (1u << component)) values[component][value] = value;
            if (channelMask & 8u) values[3][value] = value * 128u / 255;
        }
        return;
    }
    const u8 fullShade[4] = {255,255,255,255};
    const u8 zero[4] = {0}, previous[4] = {1,2,3,4};
    int cycles = analysis->type == 1 ? 2 : 1;
    for (int alphaPass = 0; alphaPass < 2; alphaPass++) {
        unsigned requested = alphaPass ? channelMask & 8u : channelMask & 7u;
        if (!requested) continue;
        /* RGB equations can use the previous cycle's alpha. Compute that
         * intermediate even when the final alpha channel is being reused. */
        unsigned evaluated = alphaPass ? 8u : requested | 8u;
        if (alphaPass && analysis->dynamicAlpha) {
            for (int value = 0; value < 256; value++) values[3][value] = value * 128u / 255;
            continue;
        }
        LookupEquation equations[2][4];
        u8 texel0[4] = {0,0,0,255}, texel1[4] = {1,1,1,255};
        int firstComponent = alphaPass ? 3 : 0;
        if (alphaPass) {
            texel0[0] = texel0[1] = texel0[2] = 255; texel0[3] = 0;
            texel1[0] = texel1[1] = texel1[2] = 255; texel1[3] = 1;
        }
        for (int cycle = 0; cycle < cycles; cycle++) for (int component = firstComponent; component < 4; component++) {
            if (!(evaluated & (1u << component))) continue;
            LookupEquation* equation = &equations[cycle][component];
            const u8* selectors = analysis->sources[cycle][component == 3];
            for (int slot = 0; slot < 4; slot++) {
                int base = combiner_input(rdp, selectors[slot], slot, component == 3, component, texel0, fullShade, zero);
                int variable = combiner_input(rdp, selectors[slot], slot, component == 3, component, texel1, fullShade, zero);
                int combined = combiner_input(rdp, selectors[slot], slot, component == 3, component, texel0, fullShade, previous);
                equation->source[slot] = variable != base ? 256 : combined != base ? 256 + combined : base;
            }
            equation->operation = equation->source[0] == equation->source[1] || equation->source[2] == 0 ? 1 :
                                  equation->source[2] == 255 ? 2 : 0;
        }
        int evaluatedCycles = cycles;
        if (cycles == 2) {
            bool copiesCombined = true;
            for (int component = firstComponent; component < 4; component++)
                if ((evaluated & (1u << component)) &&
                    !lookup_copies_combined(&equations[1][component], component)) copiesCombined = false;
            /* The first cycle has already clamped each channel to 0..255.
             * A final identity cannot change its value, even when RGB uses
             * that first cycle's alpha. Only fold when every evaluated
             * channel is an identity; other equations retain both cycles. */
            if (copiesCombined) evaluatedCycles = 1;
#ifdef PS2_RENDERER_SMOKE_CAPTURE
            if (copiesCombined) ps2_combiner_test_folded_passes++;
            else ps2_combiner_test_retained_passes++;
#endif
        }
        for (int value = 0; value < 256; value++) {
            u8 combined[4] = {0};
            for (int cycle = 0; cycle < evaluatedCycles; cycle++) {
                u8 next[4] = {0};
                for (int component = firstComponent; component < 4; component++) {
                    if (!(evaluated & (1u << component))) continue;
                    const LookupEquation* equation = &equations[cycle][component];
                    int result = lookup_value(equation->source[3], value, combined);
                    if (equation->operation != 1) {
                        int difference = lookup_value(equation->source[0], value, combined) -
                                         lookup_value(equation->source[1], value, combined);
                        result += equation->operation == 2 ? difference :
                                  difference * lookup_value(equation->source[2], value, combined) / 255;
                    }
                    next[component] = result < 0 ? 0 : result > 255 ? 255 : result;
                }
                memcpy(combined, next, 4);
            }
            if (alphaPass) values[3][value] = combined[3] * 128u / 255;
            else for (int component = 0; component < 3; component++)
                if (requested & (1u << component)) values[component][value] = combined[component];
        }
    }
}

void ps2_combiner_build_lookup(const RDPState* rdp, u8 values[4][256]) {
    ps2_combiner_build_lookup_channels(rdp, values, 15u);
}

/* ─────────────────────────────────────────────────────────── *
 *  G_RDPSETOTHERMODE — sets pipeline, cycle, blend formula
 *
 *  Cycle type is hi[21:20]; alpha compare is lo[1:0],
 *  depth source lo[2], FORCE_BL lo[14], and blender lo[31:16].
 * ─────────────────────────────────────────────────────────── */

/* Blender sources */
#define BL_CLR_IN   0   /* input color */
#define BL_CLR_MEM  1   /* framebuffer color */
#define BL_CLR_BL   2   /* blend color */
#define BL_CLR_FOG  3   /* fog color */
#define BL_A_IN     0   /* input alpha */
#define BL_A_FOG    1   /* fog alpha */
#define BL_A_SHADE  2   /* shade alpha */
#define BL_1MA      0   /* 1 - selected input alpha */
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
 *      P1[31:30] A1[27:26] M1[23:22] B1[19:18]
 *      P2[29:28] A2[25:24] M2[21:20] B2[17:16]
 * ─────────────────────────────────────────────────────────── */
void rdp_apply_othermode(RDPState* rdp) {
    GSGLOBAL* gs = rdp->gs;
    if (!gs) return;
    u32 profileStart = ps2_gfx_profile_enter();

    u32 lo = rdp->otherMode.lo;
    u32 hi = rdp->otherMode.hi;

    /* Tipo de ciclo: bits [21:20] de la palabra ALTA */
    u8 cycleType = (u8)((hi >> 20) & 0x3);

    /* Comparación de alfa: bits [1:0] de la palabra baja */
    u8 alphaCompare = (u8)(lo & 0x3);

    /* GBL_c1/c2 interleave their fields. In two-cycle mode the final
     * blender is cycle 2; the preceding fog stage needs separate emulation. */
    u32 cycleShift = cycleType == 1 ? 2 : 0;
    u8 P1 = (u8)((lo >> (30 - cycleShift)) & 0x3);
    u8 A1 = (u8)((lo >> (26 - cycleShift)) & 0x3);
    u8 M1 = (u8)((lo >> (22 - cycleShift)) & 0x3);
    u8 B1 = (u8)((lo >> (18 - cycleShift)) & 0x3);
    (void)A1;

    u64 gsAlpha;
    if (cycleType >= 2 || !(lo & 0x4000)) {
        /* Without FORCE_BL the N64 blender uses coverage for opaque
         * surfaces; source alpha must not make them disappear. */
        gsAlpha = GS_SETREG_ALPHA(0, 2, 2, 2, 0x80);
    } else if (P1 == BL_CLR_IN && M1 == BL_CLR_MEM && B1 == BL_1MA) {
        /* Mezcla alfa clásica: src*a + dst*(1-a) */
        gsAlpha = GS_SETREG_ALPHA(0, 1, 0, 1, 0x80);
    } else if (P1 == BL_CLR_IN && B1 == BL_0) {
        gsAlpha = GS_SETREG_ALPHA(0, 2, 2, 2, 0x80);
    } else if (P1 == BL_CLR_FOG) {
        gsAlpha = GS_SETREG_ALPHA(1, 0, 1, 0, 0x80);  /* niebla */
    } else {
        gsAlpha = GS_SETREG_ALPHA(0, 1, 0, 1, 0x80);
    }

    rdp->gs_alpha = gsAlpha;
    ps2_gs_set_alpha(rdp, gsAlpha);

    /* Test de alfa + test de profundidad, en UN solo registro TEST.
     * Antes se escribían por separado desde dos archivos distintos y el
     * segundo pisaba al primero. */
    bool zCompare = (lo & 0x0010) != 0;

    /* TEX_EDGE materials discard zero coverage even with G_AC_NONE.
     * GS has no RDP coverage buffer; approximate the alpha coverage edge
     * with a half-alpha cutout, preserving color and Z outside its holes. */
    bool alphaCoverage = (lo & 0x1000) != 0;
    int ate  = alphaCompare == 1 || alphaCoverage;
    int atst = 6;      /* GEQUAL */
    int aref = alphaCoverage ? 0x40 : ((rdp->blendColor & 255) * 128 + 254) / 255;
    if (alphaCompare == 1 && !aref) aref = 1;
    int zte  = 1;      /* el GS exige ZTE=1; para desactivar se usa ZTST=1 */
    int ztst = zCompare ? 2 : 1;   /* 2 = GEQUAL, 1 = ALWAYS */

    u64 test = GS_SETREG_TEST(ate, atst, aref, 0, 0, 0, zte, ztst);
    rdp->gs_test = test;
    gs->Test->ATE = ate;
    gs->Test->ATST = atst;
    gs->Test->AREF = aref;
    gs->Test->AFAIL = 0;
    gs->Test->DATE = 0;
    gs->Test->DATM = 0;
    gs->Test->ZTE = zte;
    gs->Test->ZTST = ztst;
    ps2_gs_set_test(rdp);
    ps2_zbuffer_set_writes(rdp, (lo & 0x20) != 0);
    ps2_gfx_profile_leave(GFX_PROFILE_STATE, profileStart);
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
 *    w0[15:8] = 32 - desplazamiento - longitud
 *    w0[ 7:0] = longitud - 1
 *    w1       = valor a insertar
 * ─────────────────────────────────────────────────────────── */
static u32 om_insert(u32 current, u32 w0, u32 w1) {
    u32 len   = ((w0 >> 0) & 0xFF) + 1;
    u32 encodedShift = ((w0 >> 8) & 0xFF);
    /* F3DEX2 guarda el shift "al revés": 32 - shift - len */
    if (len > 32 || encodedShift > 32 - len) return current;
    u32 shift = 32 - encodedShift - len;
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
        x0 = (u16)(x0 * rdp->gs->Width / 320);
        y0 = (u16)(y0 * rdp->gs->Height / 240);
        x1 = (u16)(x1 * rdp->gs->Width / 320);
        y1 = (u16)(y1 * rdp->gs->Height / 240);
        if (x1 > x0) --x1; /* GS bounds are inclusive. */
        if (y1 > y0) --y1;
        gsKit_set_scissor(rdp->gs, GS_SETREG_SCISSOR(x0, x1, y0, y1));
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

    if (((rdp->otherMode.hi >> 20) & 3) == 3) {
        x1 += 1; y1 += 1; /* Fill-cycle lower-right is inclusive. */
    }
    x0 *= rdp->gs->Width / 320.0f;
    x1 *= rdp->gs->Width / 320.0f;
    y0 *= rdp->gs->Height / 240.0f;
    y1 *= rdp->gs->Height / 240.0f;

    /* N64 clears depth by temporarily using the depth buffer as color image.
     * Write far depth while preserving the displayed color through Cd blending. */
    if (rdp->colorImage && rdp->colorImage == rdp->zImage) {
        GSGLOBAL* gs = rdp->gs;
        u8 oldZ = gs->Test->ZTST, oldAlpha = gs->Test->ATE;
        int oldBlend = gs->PrimAlphaEnable;
        bool oldWrite = rdp->zWriteEnabled;
        ps2_zbuffer_set_writes(rdp, true);
        gs->Test->ZTST = 1;
        gs->Test->ATE = 0;
        ps2_gs_set_test(rdp);
        gs->PrimAlphaEnable = GS_SETTING_ON;
        ps2_gs_set_alpha(rdp, GS_SETREG_ALPHA(0, 0, 2, 1, 0));
        gsKit_prim_sprite(gs, x0, y0, x1, y1, 0, GS_SETREG_RGBA(0, 0, 0, 0));
        ps2_zbuffer_set_writes(rdp, oldWrite);
        gs->PrimAlphaEnable = oldBlend;
        ps2_gs_set_alpha(rdp, rdp->gs_alpha);
        gs->Test->ZTST = oldZ;
        gs->Test->ATE = oldAlpha;
        ps2_gs_set_test(rdp);
        return;
    }

    /* One/two-cycle rectangles use the combiner, including primitive alpha
     * for screen fades. Using the fill register here covered every frame
     * with opaque black even when coverAlpha was zero. */
    if (((rdp->otherMode.hi >> 20) & 3) != 3) {
        const u8 white[4] = {255, 255, 255, 255};
        u8 rgba[4];
        ps2_combiner_color(rdp, white, white, rgba);
        gsKit_prim_sprite(rdp->gs, x0, y0, x1, y1, 1,
            GS_SETREG_RGBA(rgba[0], rgba[1], rgba[2], n64_alpha_to_gs(rgba[3])));
        return;
    }
    /* Fill cycle decodes fill color as RGBA16 (two packed pixels). */
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
 *    cmd 0: w0 = G_TEXRECT + x1/y1, w1 = tile + x0/y0
 *    cmd 1: G_RDPHALF_1, w1 = s/t
 *    cmd 2: G_RDPHALF_2, w1 = dS/dX, dT/dY
 * ─────────────────────────────────────────────────────────── */
void rdp_texrect(u8 op, u32 w0, u32 w1, const Gfx* pc, RDPState* rdp) {

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
    u32 c1w1 = GFX_READ_W1(pc + 1);
    float x0 = (float)((w1 >> 12) & 0xFFF) / 4.0f;
    float y0 = (float)((w1      ) & 0xFFF) / 4.0f;

    /* Command 2 (pc+2): texture S/T start + dS/dT */
    u32 c2w1 = GFX_READ_W1(pc + 2);
    float s0 = (float)(s16)((c1w1 >> 16) & 0xFFFF) / 32.0f;
    float t0 = (float)(s16)( c1w1        & 0xFFFF) / 32.0f;
    float ds = (float)(s16)((c2w1 >> 16) & 0xFFFF) / 1024.0f;
    float dt = (float)(s16)( c2w1        & 0xFFFF) / 1024.0f;
    if (((rdp->otherMode.hi >> 20) & 3) == 2) {
        ds *= 0.25f;
        x1 += 1; y1 += 1;
    }

    if (!rdp->gs) return;

    TileDescriptor* tile = &rdp->tile[tile_idx];
    if (!tile->gsTextureLoaded) {
        ps2_texture_upload(tile, rdp);
    }

    if (!tile->gsTextureLoaded) return;
    ps2_texture_apply_tile(tile, rdp);
    /* LOADTILE uploads only the selected subregion. Rectangle coordinates
     * are still relative to the complete source image, as with 3D vertices. */
    s0 -= tile->uls / 4.0f;
    t0 -= tile->ult / 4.0f;
    u8 alpha = ps2_combiner_dynamic_alpha(rdp) ? n64_alpha_to_gs(rdp->primColor & 255) : 128;
    /* TEXRECTFLIP exchanges screen axes for the texture derivatives. */
    float s1 = s0 + ((op == 0xE5) ? (y1 - y0) : (x1 - x0)) * ds;
    float t1 = t0 + ((op == 0xE5) ? (x1 - x0) : (y1 - y0)) * dt;
    x0 *= rdp->gs->Width / 320.0f;
    x1 *= rdp->gs->Width / 320.0f;
    y0 *= rdp->gs->Height / 240.0f;
    y1 *= rdp->gs->Height / 240.0f;

    if (op == 0xE5) {
        u64 color = GS_SETREG_RGBAQ(0x80, 0x80, 0x80, alpha, 1.0f);
        gsKit_prim_triangle_goraud_texture_3d(rdp->gs, &tile->gsTexture,
            x0, y0, 1, s0, t0, x1, y0, 1, s0, t1,
            x0, y1, 1, s1, t0, color, color, color);
        gsKit_prim_triangle_goraud_texture_3d(rdp->gs, &tile->gsTexture,
            x1, y0, 1, s0, t1, x1, y1, 1, s1, t1,
            x0, y1, 1, s1, t0, color, color, color);
        return;
    }
    gsKit_prim_sprite_texture(
        rdp->gs, &tile->gsTexture,
        x0, y0,   s0, t0,
        x1, y1,   s1, t1,
        1,
        GS_SETREG_RGBA(0x80, 0x80, 0x80, alpha));
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
