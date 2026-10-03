/**
 * ps2_texture.c — N64 Texture Format Conversion & GS Upload
 *
 * N64 textures come in several formats, all Big-Endian:
 *   RGBA16 (5-5-5-1)   → most common in OoT
 *   RGBA32 (8-8-8-8)
 *   IA8    (4-4)       → intensity + alpha, 8 bits
 *   IA16   (8-8)
 *   I4     (4-bit intensity)
 *   I8     (8-bit intensity)
 *   CI4    (4-bit color index) → needs TLUT (palette)
 *   CI8    (8-bit color index) → needs TLUT
 *
 * PS2 GS uses different pixel formats:
 *   GS_PSM_CT32   (RGBA8888)
 *   GS_PSM_CT16   (RGBA5551)
 *   GS_PSM_T8     (8-bit indexed)
 *   GS_PSM_T4     (4-bit indexed)
 *
 * This file handles:
 *   1. Format detection from N64 tile descriptors
 *   2. Pixel-by-pixel format conversion (BE→LE, format remapping)
 *   3. VRAM allocation and upload via gsKit
 *   4. CLUT (palette) upload for CI textures
 */

#include <gsKit.h>
#include <gsTexture.h>
#include <dmaKit.h>
#include <gsInline.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "ps2_types.h"
#include "ps2_segment.h"
#include "rdp_to_gs.h"
#include "ps2_gfx_profile.h"
#include "ps2_texture.h"

/* ─────────────────────────────────────────────────────────── *
 *  Scratch conversion buffer (static to avoid heap alloc per frame)
 *  Max N64 texture size: 256×256 @ 32bpp = 262144 bytes
 * ─────────────────────────────────────────────────────────── */
static u8 s_convBuf[256 * 256 * 4] __attribute__((aligned(128)));
static u32 s_textureVram = GSKIT_ALLOC_ERROR;
static u8 s_sourceRows[sizeof(s_convBuf)] __attribute__((aligned(128)));
static u32 s_clut[256] __attribute__((aligned(128)));
typedef struct TextureCacheEntry {
    uintptr_t source;
    uintptr_t source2;
    u32 stride2;
    u8 lodFrac;
    u32 stride, combine0, combine1, prim, env, cycle, paletteHash, contentHash;
    u32 vram, size, used, frame;
    u16 width, height;
    u8 fmt, siz, palette, valid;
    u16 mirror;
} TextureCacheEntry;
/* Share the available VRAM budget between actual texture sizes. Fixed size
 * classes stranded most of VRAM while menu strips evicted one another. */
#define TEXTURE_CACHE_SLOTS 256
#define TEXTURE_CACHE_CHUNK 1024u
#define TEXTURE_CACHE_BYTES (1792u * 1024)
static u32 s_textureCacheBytes;
static TextureCacheEntry s_cache[TEXTURE_CACHE_SLOTS];
/* Bindings usually revisit a handful of entries with the same RAM address.
 * Index those candidates instead of comparing all 256 materials each time.
 * Evictions unlink descriptors before reusing their slot or VRAM. */
#define TEXTURE_CACHE_BUCKETS 64
#define TEXTURE_CACHE_END 0xffffu
static u16 s_cacheHeads[TEXTURE_CACHE_BUCKETS];
static u16 s_cacheNext[TEXTURE_CACHE_SLOTS];
static bool s_cacheIndexReady;
void ps2_texture_profile_cache(void) {
    unsigned count=0, bytes=0;
    for (unsigned i=0;i<TEXTURE_CACHE_SLOTS;i++) if (s_cache[i].valid) {
        count++; bytes += s_cache[i].size;
    }
    printf("[PS2 TEXTURE CACHE] entries=%u/%u bytes=%u/%u\n",
        count,TEXTURE_CACHE_SLOTS,bytes,(unsigned)s_textureCacheBytes);
}
static unsigned cache_bucket(uintptr_t source) {
    u32 bits = (u32)(source >> 4);
    return (bits ^ (bits >> 8) ^ (bits >> 16)) & (TEXTURE_CACHE_BUCKETS - 1);
}
static void cache_unlink(unsigned slot) {
    u16* link = &s_cacheHeads[cache_bucket(s_cache[slot].source)];
    while (*link != TEXTURE_CACHE_END) {
        if (*link == slot) { *link = s_cacheNext[slot]; return; }
        link = &s_cacheNext[*link];
    }
}
static u32 s_cacheClock;
/* A FINISH retires all accesses up to this clock, including other textures.
 * Remember that fence so subsequent uploads do not wait for the same draws. */
static u32 s_retiredClock;
static u32 s_textureFrame;
/* A linear sliding maximum selects the same oldest overlapping region as
 * the old scan over every entry for each of 704 possible starting chunks. */
static u32 s_chunkAge[TEXTURE_CACHE_BYTES / TEXTURE_CACHE_CHUNK];
static unsigned s_ageDeque[TEXTURE_CACHE_BYTES / TEXTURE_CACHE_CHUNK];
static u32 cache_region(u32 size, u32* costOut) {
    memset(s_chunkAge, 0, sizeof(s_chunkAge));
    for (int i = 0; i < TEXTURE_CACHE_SLOTS; i++) {
        const TextureCacheEntry* entry = &s_cache[i];
        if (!entry->valid) continue;
        unsigned first = (entry->vram - s_textureVram) / TEXTURE_CACHE_CHUNK;
        unsigned count = entry->size / TEXTURE_CACHE_CHUNK;
        for (unsigned j = first; j < first + count; j++) s_chunkAge[j] = entry->used;
    }
    unsigned span = size / TEXTURE_CACHE_CHUNK, head = 0, tail = 0, best = 0;
    u32 bestCost = ~0u;
    for (unsigned i = 0; i < s_textureCacheBytes / TEXTURE_CACHE_CHUNK; i++) {
        while (head < tail && s_ageDeque[head] + span <= i) head++;
        while (head < tail && s_chunkAge[s_ageDeque[tail - 1]] <= s_chunkAge[i]) tail--;
        s_ageDeque[tail++] = i;
        if (i + 1 < span) continue;
        u32 cost = s_chunkAge[s_ageDeque[head]];
        if (cost < bestCost) { bestCost = cost; best = i + 1 - span; }
        if (!cost) break;
    }
    *costOut = bestCost;
    return s_textureVram + best * TEXTURE_CACHE_CHUNK;
}
typedef struct CombinerLookup {
    u32 combine0, combine1, prim, env, cycle, used;
    u8 valid, values[4][256];
    u32 rgba16[4][32]; /* packed mapped channels of a 5-5-5-1 source */
} CombinerLookup;
#define COMBINER_LOOKUP_SLOTS 64
static CombinerLookup s_lookups[COMBINER_LOOKUP_SLOTS];
static u32 s_lookupClock;
extern volatile unsigned gPS2GfxTraceStage;

/* Many tiles share a combiner. Cache its compiled channel mappings rather
 * than resolving source selectors on every texture upload. */
static const CombinerLookup* combiner_lookup(const RDPState* rdp) {
    u32 cycle = (rdp->otherMode.hi >> 20) & 3;
    u32 prim, env;
    ps2_combiner_constants(rdp, &prim, &env);
    if (ps2_combiner_dynamic_alpha(rdp)) prim &= 0xffffff00u;
    int slot = 0;
    for (int i = 0; i < COMBINER_LOOKUP_SLOTS; i++) {
        CombinerLookup* entry = &s_lookups[i];
        if (entry->valid && entry->combine0 == rdp->combiner.w0 &&
            entry->combine1 == rdp->combiner.w1 && entry->prim == prim &&
            entry->env == env && entry->cycle == cycle) {
            entry->used = ++s_lookupClock;
            return entry;
        }
        if (!entry->valid || (s_lookups[slot].valid && entry->used < s_lookups[slot].used)) slot = i;
    }
    CombinerLookup* entry = &s_lookups[slot];
    entry->combine0 = rdp->combiner.w0; entry->combine1 = rdp->combiner.w1;
    entry->prim = prim; entry->env = env; entry->cycle = cycle;
    ps2_combiner_build_lookup(rdp, entry->values);
    for (unsigned channel = 0; channel < 3; channel++) for (unsigned value = 0; value < 32; value++)
        entry->rgba16[channel][value] = (u32)entry->values[channel][(value << 3) | (value >> 2)] << (channel*8);
    entry->rgba16[3][0] = (u32)entry->values[3][0] << 24;
    entry->rgba16[3][1] = (u32)entry->values[3][255] << 24;
    entry->used = ++s_lookupClock;
    entry->valid = 1;
    return entry;
}

/* The GS has region repeat but no mirrored repeat. Store one forward and
 * one reversed period in cached texels; interpolate coordinates normally. */
static unsigned mirrored_dimensions(const TileDescriptor* tile, int w, int h, int* outW, int* outH) {
    bool ms = tile->cms == 1 && tile->masks && tile->masks <= 9 && (1u << tile->masks) <= (unsigned)w;
    bool mt = tile->cmt == 1 && tile->maskt && tile->maskt <= 9 && (1u << tile->maskt) <= (unsigned)h;
    int mw = ms ? 2u << tile->masks : w;
    int mh = mt ? 2u << tile->maskt : h;
    if ((unsigned)mw * mh * 4 > sizeof(s_convBuf)) { mw = w; mh = h; ms = mt = false; }
    *outW = mw; *outH = mh;
    return (ms ? 1u | (tile->masks << 4) : 0) | (mt ? 2u | (tile->maskt << 8) : 0);
}

void ps2_texture_apply_tile(TileDescriptor* tile, RDPState* rdp) {
    GSGLOBAL* gs = rdp->gs;
    if (!gs) return;
    /* A still-loaded tile can be drawn again after a different upload has
     * retired earlier draws. Record each binding, including that reuse. */
    if (tile->gsTextureLoaded && tile->cacheSlot < TEXTURE_CACHE_SLOTS) {
        TextureCacheEntry* entry = &s_cache[tile->cacheSlot];
        if (entry->valid && entry->vram == tile->gsTexture.Vram) entry->used = ++s_cacheClock;
    }
    /* Region clamp also excludes uninitialized padding in non-power-of-two
     * strips. Region repeat uses the N64 mask without clamping CPU vertices. */
    unsigned modeS = (tile->cms & 2) || !tile->masks ? 2 : 3;
    unsigned modeT = (tile->cmt & 2) || !tile->maskt ? 2 : 3;
    unsigned minS = modeS == 3 ? (1u << (tile->masks > 10 ? 10 : tile->masks)) - 1 : 0;
    unsigned minT = modeT == 3 ? (1u << (tile->maskt > 10 ? 10 : tile->maskt)) - 1 : 0;
    int mw, mh;
    unsigned mirror = mirrored_dimensions(tile, tile->width, tile->height, &mw, &mh);
    if ((mirror & 1) && tile->gsTexture.Width == (unsigned)mw)
        minS = tile->gsTexture.Width - 1;
    if ((mirror & 2) && tile->gsTexture.Height == (unsigned)mh)
        minT = tile->gsTexture.Height - 1;
    unsigned maxS = modeS == 2 && tile->width ? tile->width - 1 : 0;
    unsigned maxT = modeT == 2 && tile->height ? tile->height - 1 : 0;
    gs->Clamp->WMS = modeS; gs->Clamp->WMT = modeT;
    gs->Clamp->MINU = minS; gs->Clamp->MAXU = maxS;
    gs->Clamp->MINV = minT; gs->Clamp->MAXV = maxT;
    u64 clamp = GS_SETREG_CLAMP(modeS, modeT, minS, maxS, minT, maxT);
    if (!rdp->gs_clamp_valid || rdp->gs_clamp != clamp || rdp->gs_clamp_context != gs->PrimContext) {
        u64* packet = gsKit_heap_alloc(gs, 1, 16, GIF_AD);
        packet[0] = GIF_TAG_AD(1); packet[1] = GIF_AD;
        packet[2] = clamp;
        packet[3] = GS_CLAMP_1 + gs->PrimContext;
        rdp->gs_clamp = clamp;
        rdp->gs_clamp_context = gs->PrimContext;
        rdp->gs_clamp_valid = true;
    }
    tile->gsTexture.Filter = ((rdp->otherMode.hi >> 12) & 3) ? GS_FILTER_LINEAR : GS_FILTER_NEAREST;
}

static float texture_shift(float coordinate, u8 shift) {
    /* Four-bit RDP shift: exact powers of two, with no per-corner division. */
    static const float scale[16] = {1,0.5f,0.25f,0.125f,0.0625f,0.03125f,
        0.015625f,0.0078125f,0.00390625f,0.001953125f,0.0009765625f,32,16,8,4,2};
    return coordinate * scale[shift & 15];
}

static float texture_extent_reciprocal(unsigned extent) {
    unsigned size = 1;
    while (size < extent) size <<= 1;
    return 1.0f / size;
}

void ps2_texture_build_vertex(const TileDescriptor* tile, const RSPState* rsp,
    const RDPState* rdp, const RSPVertex* vertex, u32 color, u32 depth,
    GSPRIMSTQPOINT* output) {
    float invWidth = tile->gsInvWidth ? tile->gsInvWidth :
        texture_extent_reciprocal(tile->gsTexture.Width ? tile->gsTexture.Width : tile->width);
    float invHeight = tile->gsInvHeight ? tile->gsInvHeight :
        texture_extent_reciprocal(tile->gsTexture.Height ? tile->gsTexture.Height : tile->height);
    float u = texture_shift(vertex->tc_s * rsp->texScaleS / 32.0f, tile->shifts) - tile->uls / 4.0f;
    float v = texture_shift(vertex->tc_t * rsp->texScaleT / 32.0f, tile->shiftt) - tile->ult / 4.0f;
    float q = (rdp->otherMode.hi & (1u << 19)) && vertex->cw > 0.0001f ? 1.0f / vertex->cw : 1.0f;
    if (ps2_combiner_dynamic_alpha(rdp)) {
        u32 alpha = (color >> 24) * (rdp->primColor & 255) / 255;
        color = (color & 0xffffffu) | (alpha << 24);
    }
    output->rgbaq = rgba_to_RGBAQ(color, q);
    output->stq = vertex_to_STQ(u * q * invWidth, v * q * invHeight);
    output->xyz2 = vertex_to_XYZ2(rdp->gs, vertex->sx, vertex->sy, depth);
}

void ps2_texture_end_frame(RDPState* rdp) {
    /* Recheck source bytes next frame: animated/reused RAM cannot retain an
     * old texture just because its address is unchanged. VRAM stays cached. */
    for (int i = 0; i < MAX_TILES; i++) rdp->tile[i].gsTextureLoaded = false;
    s_textureFrame++;
}

static void texture_descriptor(TileDescriptor* tile, int w, int h, u32 vram) {
    GSTEXTURE* gt = &tile->gsTexture;
    gt->Width = w; gt->Height = h; gt->PSM = GS_PSM_CT32;
    gt->Mem = (u32*)s_convBuf; gt->Filter = GS_FILTER_LINEAR;
    gt->Clut = NULL; gt->Vram = vram;
    /* Upload normally computes this; cache hits must also restore it. */
    gt->TBW = (w + 63) / 64;
    tile->gsInvWidth = texture_extent_reciprocal(w);
    tile->gsInvHeight = texture_extent_reciprocal(h);
}

static void convert_rgba16_mapped(const u8* source, u8* destination, int pixels,
                                  const CombinerLookup* lookup) {
    u32* output = (u32*)destination;
    for (int i = 0; i < pixels; i++) {
        u16 raw = read_be16(source + i*2);
        output[i] = lookup->rgba16[0][raw >> 11] | lookup->rgba16[1][(raw >> 6) & 31] |
                    lookup->rgba16[2][(raw >> 1) & 31] | lookup->rgba16[3][raw & 1];
    }
}

/* Intensity and palette formats have only 16 or 256 distinct inputs. Bake
 * the material once per input, then expand with one packed store per texel.
 * Keep the old GS-alpha quantization before the material lookup exactly. */
static bool convert_compact_mapped(const u8* source, u8* destination, int pixels,
                                   const TileDescriptor* tile, const CombinerLookup* mapping) {
    bool nibble = tile->siz == G_IM_SIZ_4b;
    if (tile->fmt != G_IM_FMT_I && tile->fmt != G_IM_FMT_IA && tile->fmt != G_IM_FMT_CI) return false;
    if (!nibble && tile->siz != G_IM_SIZ_8b &&
        !(tile->fmt == G_IM_FMT_IA && tile->siz == G_IM_SIZ_16b)) return false;
    const u8 (*lookup)[256] = mapping->values;
    u32 colors[256];
    u32* output = (u32*)destination;
    if (tile->fmt == G_IM_FMT_IA && tile->siz == G_IM_SIZ_16b) {
        u32 alphas[256];
        for (unsigned value = 0; value < 256; value++) {
            colors[value] = lookup[0][value] | ((u32)lookup[1][value] << 8) |
                            ((u32)lookup[2][value] << 16);
            unsigned alpha = value * 128 / 255;
            alphas[value] = (u32)lookup[3][alpha == 128 ? 255 : alpha * 2] << 24;
        }
        for (int i = 0; i < pixels; i++) output[i] = colors[source[i*2]] | alphas[source[i*2+1]];
        return true;
    }
    unsigned count = nibble ? 16 : 256;
    for (unsigned value = 0; value < count; value++) {
        unsigned r, g, b, alpha;
        if (tile->fmt == G_IM_FMT_CI) {
            u32 color = s_clut[nibble ? (tile->palette << 4) | value : value];
            r = color & 255; g = (color >> 8) & 255; b = (color >> 16) & 255; alpha = color >> 24;
        } else {
            unsigned intensity;
            if (tile->fmt == G_IM_FMT_I) {
                intensity = nibble ? value * 17 : value;
                alpha = intensity >> 1;
            } else if (nibble) {
                intensity = value >> 1;
                intensity = (intensity << 5) | (intensity << 2) | (intensity >> 1);
                alpha = (value & 1) ? 128 : 0;
            } else {
                intensity = (value >> 4) * 17;
                alpha = (value & 15) * 17 * 128 / 255;
            }
            r = g = b = intensity;
        }
        colors[value] = lookup[0][r] | ((u32)lookup[1][g] << 8) | ((u32)lookup[2][b] << 16) |
                        ((u32)lookup[3][alpha == 128 ? 255 : alpha * 2] << 24);
    }
    if (nibble) {
        int i = 0;
        for (; i + 1 < pixels; i += 2) {
            unsigned value = source[i / 2];
            output[i] = colors[value >> 4]; output[i+1] = colors[value & 15];
        }
        if (i < pixels) output[i] = colors[source[i / 2] >> 4];
    } else for (int i = 0; i < pixels; i++) output[i] = colors[source[i]];
    return true;
}

/* ─────────────────────────────────────────────────────────── *
 *  RGBA16 (N64 5-5-5-1) → RGBA8888 (PS2 CT32)
 *  N64 RGBA16: RRRRRGGGGGGBBBBBXXXXXXXX A (MSB→LSB: 5R 5G 5B 1A)
 *  PS2 CT32:   RGBA (little-endian u32)
 * ─────────────────────────────────────────────────────────── */
static void convert_rgba16_to_rgba32(const u8* src, u8* dst,
                                     int w, int h) {
    int pixels = w * h;
    for (int i = 0; i < pixels; i++) {
        u16 px = read_be16(src + i * 2);
        u8 r = (px >> 11) & 0x1F;  r = (r << 3) | (r >> 2);
        u8 g = (px >>  6) & 0x1F;  g = (g << 3) | (g >> 2);
        u8 b = (px >>  1) & 0x1F;  b = (b << 3) | (b >> 2);
        u8 a = (px & 1) ? 0x80 : 0;  /* PS2 GS: 0x80 = fully opaque */

        dst[i * 4 + 0] = r;
        dst[i * 4 + 1] = g;
        dst[i * 4 + 2] = b;
        dst[i * 4 + 3] = a;
    }
}

/* ─────────────────────────────────────────────────────────── *
 *  RGBA32 (N64) → RGBA32 (PS2)
 *  Just needs byte swap of each 32-bit word (BE→LE).
 * ─────────────────────────────────────────────────────────── */
static void convert_rgba32_to_rgba32(const u8* src, u8* dst,
                                     int w, int h) {
    int pixels = w * h;
    for (int i = 0; i < pixels; i++) {
        u32 px = read_be32(src + i * 4);
        /* N64: RRGGBBAA → PS2 CT32: RGBA little-endian */
        u8 r = (px >> 24) & 0xFF;
        u8 g = (px >> 16) & 0xFF;
        u8 b = (px >>  8) & 0xFF;
        u8 a = (px      ) & 0xFF;
        a = (u32)a * 128 / 255; /* 255 must map to fully opaque GS alpha. */

        dst[i * 4 + 0] = r;
        dst[i * 4 + 1] = g;
        dst[i * 4 + 2] = b;
        dst[i * 4 + 3] = a;
    }
}

/* ─────────────────────────────────────────────────────────── *
 *  IA8 (N64: 4-bit I + 4-bit A) → RGBA32
 * ─────────────────────────────────────────────────────────── */
static void convert_ia8_to_rgba32(const u8* src, u8* dst, int w, int h) {
    int pixels = w * h;
    for (int i = 0; i < pixels; i++) {
        u8 byte = src[i];
        u8 intensity = (byte >> 4) & 0x0F;  intensity = (intensity << 4) | intensity;
        u8 alpha     = (byte     ) & 0x0F;  alpha     = (alpha     << 4) | alpha;
        alpha = (u32)alpha * 128 / 255;
        dst[i*4+0] = intensity;
        dst[i*4+1] = intensity;
        dst[i*4+2] = intensity;
        dst[i*4+3] = alpha;
    }
}

/* IA4: three intensity bits and one alpha bit, two texels per byte. */
static void convert_ia4_to_rgba32(const u8* src, u8* dst, int w, int h) {
    for (int i = 0; i < w * h; i++) {
        u8 value = (i & 1) ? src[i / 2] & 15 : src[i / 2] >> 4;
        u8 intensity = value >> 1;
        intensity = (intensity << 5) | (intensity << 2) | (intensity >> 1);
        dst[i * 4] = dst[i * 4 + 1] = dst[i * 4 + 2] = intensity;
        dst[i * 4 + 3] = (value & 1) ? 128 : 0;
    }
}

/* ─────────────────────────────────────────────────────────── *
 *  IA16 (N64: 8-bit I + 8-bit A) → RGBA32
 * ─────────────────────────────────────────────────────────── */
static void convert_ia16_to_rgba32(const u8* src, u8* dst, int w, int h) {
    int pixels = w * h;
    for (int i = 0; i < pixels; i++) {
        u8 intensity = src[i * 2 + 0];
        u8 alpha     = (u32)src[i * 2 + 1] * 128 / 255;
        dst[i*4+0] = intensity;
        dst[i*4+1] = intensity;
        dst[i*4+2] = intensity;
        dst[i*4+3] = alpha;
    }
}

/* ─────────────────────────────────────────────────────────── *
 *  I8 (8-bit intensity) → RGBA32
 * ─────────────────────────────────────────────────────────── */
static void convert_i8_to_rgba32(const u8* src, u8* dst, int w, int h) {
    int pixels = w * h;
    for (int i = 0; i < pixels; i++) {
        u8 v = src[i];
        dst[i*4+0] = v;
        dst[i*4+1] = v;
        dst[i*4+2] = v;
        dst[i*4+3] = v >> 1; /* N64 intensity supplies alpha as well. */
    }
}

/* ─────────────────────────────────────────────────────────── *
 *  I4 (4-bit intensity, 2 texels per byte) → RGBA32
 * ─────────────────────────────────────────────────────────── */
static void convert_i4_to_rgba32(const u8* src, u8* dst, int w, int h) {
    int pixels = w * h;
    for (int i = 0; i < pixels; i++) {
        u8 byte = src[i / 2];
        u8 nibble = (i & 1) ? (byte & 0x0F) : (byte >> 4);
        u8 v = nibble | (nibble << 4);
        dst[i*4+0] = v;
        dst[i*4+1] = v;
        dst[i*4+2] = v;
        dst[i*4+3] = v >> 1;
    }
}

/* ─────────────────────────────────────────────────────────── *
 *  CI4/CI8 palette conversion (TLUT RGBA16 → RGBA32 CLUT)
 * ─────────────────────────────────────────────────────────── */
void ps2_texture_load_tlut_at(const u8* tlut_src, int first, int count) {
    if (!tlut_src || first < 0 || count < 0 || first > 256 || count > 256 - first) return;
    /* TLUT is RGBA16 on N64 */
    for (int i = 0; i < count; i++) {
        u16 px = read_be16(tlut_src + i * 2);
        u8 r = (px >> 11) & 0x1F;  r = (r << 3) | (r >> 2);
        u8 g = (px >>  6) & 0x1F;  g = (g << 3) | (g >> 2);
        u8 b = (px >>  1) & 0x1F;  b = (b << 3) | (b >> 2);
        u8 a = (px & 1) ? 0x80 : 0;
        s_clut[first + i] = ((u32)a << 24) | ((u32)b << 16) |
                    ((u32)g <<  8) |  (u32)r;
    }
}

void ps2_texture_load_tlut(const u8* tlut_src, int count) {
    ps2_texture_load_tlut_at(tlut_src, 0, count);
}

/* ─────────────────────────────────────────────────────────── *
 *  ps2_texture_upload() — convert N64 texture and upload to GS VRAM
 * ─────────────────────────────────────────────────────────── */
void ps2_texture_upload(TileDescriptor* tile, RDPState* rdp) {
    gPS2GfxTraceStage = 1;
    if (tile && rdp->textureLoads[tile->tmem].source) {
        tile->addr = (void*)rdp->textureLoads[tile->tmem].source;
    }
    if (!tile || !tile->addr) return;
    u32 lookupStart = ps2_gfx_profile_enter();

    const u8* src = (const u8*)tile->addr;
    int w = tile->width;
    int h = tile->height;

    if (w == 0 || h == 0) return;
    if (w * h * 4 > (int)sizeof(s_convBuf)) {
        /* Clamp to scratch buffer size */
        h = sizeof(s_convBuf) / (w * 4);
        if (h == 0) return;
    }
    u32 rowBytes = (w * (4u << tile->siz) + 7) / 8;
    int uploadW, uploadH;
    unsigned mirror = mirrored_dimensions(tile, w, h, &uploadW, &uploadH);
    bool mirrorS = mirror & 1, mirrorT = mirror & 2;
    static unsigned sceneUploads;
    int trace = rdp->stats.triangles > 96 && sceneUploads++ < 12;
    if (trace) printf("[PS2 TEXTURE TRACE] begin %dx%d fmt=%u siz=%u ptr=%p\n", w, h, tile->fmt, tile->siz, tile->addr);
    u32 sourceStride = rdp->textureLoads[tile->tmem].stride;
    TextureCacheEntry key = {0};
    key.source = (uintptr_t)tile->addr;
    key.stride = sourceStride;
    key.width = w; key.height = h; key.fmt = tile->fmt; key.siz = tile->siz;
    key.palette = tile->palette;
    key.mirror = mirror;
    key.combine0 = rdp->combiner.w0; key.combine1 = rdp->combiner.w1;
    ps2_combiner_constants(rdp, &key.prim, &key.env);
    if (ps2_combiner_dynamic_alpha(rdp)) key.prim &= 0xffffff00u;
    key.cycle = (rdp->otherMode.hi >> 20) & 3;
    const TileDescriptor* second = &rdp->tile[(rdp->textureTile + 1) & 7];
    const u8* secondSrc = rdp->textureLoads[second->tmem].source;
    if (!secondSrc) secondSrc = second->addr;
    bool pathPair = ps2_combiner_path_lerp(rdp) && tile == &rdp->tile[rdp->textureTile] &&
        tile->fmt == G_IM_FMT_RGBA && tile->siz == G_IM_SIZ_16b && secondSrc &&
        second->fmt == tile->fmt && second->siz == tile->siz &&
        second->width == w && second->height == h &&
        second->uls == tile->uls && second->ult == tile->ult &&
        second->shifts == tile->shifts && second->shiftt == tile->shiftt &&
        second->cms == tile->cms && second->cmt == tile->cmt &&
        second->masks == tile->masks && second->maskt == tile->maskt;
    if (pathPair) {
        key.source2 = (uintptr_t)secondSrc;
        key.stride2 = rdp->textureLoads[second->tmem].stride;
        key.lodFrac = rdp->primLodFrac;
    }
    if (tile->fmt == G_IM_FMT_CI) {
        unsigned first = tile->siz == G_IM_SIZ_4b ? tile->palette * 16 : 0;
        unsigned count = tile->siz == G_IM_SIZ_4b ? 16 : 256;
        key.paletteHash = 2166136261u;
        for (unsigned i = 0; i < count; i++) key.paletteHash = (key.paletteHash ^ s_clut[first + i]) * 16777619u;
    }
    /* Still inspect all texel bytes on every binding: pause backgrounds,
     * animated textures and either input of a ground blend may change in RAM. */
    u32 hash = 0;
    if (!sourceStride || sourceStride == rowBytes) hash = ps2_texture_hash(src, rowBytes*h, hash);
    else for (int y = 0; y < h; y++) hash = ps2_texture_hash(src+y*sourceStride, rowBytes, hash);
    if (pathPair) {
        if (!key.stride2 || key.stride2 == rowBytes) hash = ps2_texture_hash(secondSrc, rowBytes*h, hash);
        else for (int y = 0; y < h; y++) hash = ps2_texture_hash(secondSrc+y*key.stride2, rowBytes, hash);
    }
    key.contentHash = hash;
    if (!s_cacheIndexReady) {
        memset(s_cacheHeads, 0xff, sizeof(s_cacheHeads));
        s_cacheIndexReady = true;
    }
    int reusable = -1;
    int profileCandidate = -1;
    for (unsigned i = s_cacheHeads[cache_bucket(key.source)]; i != TEXTURE_CACHE_END; i = s_cacheNext[i]) {
        TextureCacheEntry* cached = &s_cache[i];
        /* Retain variants used together in this frame, but replace an older
         * frame's version of the same source/material instead of filling
         * VRAM with historical animation/tint values. Retire GS reads below. */
        bool sameMaterial = cached->valid && cached->source == key.source &&
            cached->source2 == key.source2 && cached->stride2 == key.stride2 &&
            cached->stride == key.stride && cached->width == w && cached->height == h &&
            cached->fmt == key.fmt && cached->siz == key.siz && cached->palette == key.palette && cached->mirror == key.mirror &&
            cached->combine0 == key.combine0 && cached->combine1 == key.combine1 && cached->cycle == key.cycle;
        if (sameMaterial && cached->frame != s_textureFrame &&
            (reusable < 0 || cached->used < s_cache[reusable].used)) reusable = i;
        if (gPS2GfxProfileActive && sameMaterial &&
            (profileCandidate < 0 || cached->used > s_cache[profileCandidate].used)) profileCandidate = i;
        if (cached->valid && cached->source == key.source &&
            cached->source2 == key.source2 && cached->stride2 == key.stride2 && cached->lodFrac == key.lodFrac &&
            cached->stride == key.stride && cached->width == w && cached->height == h &&
            cached->fmt == key.fmt && cached->siz == key.siz && cached->palette == key.palette && cached->mirror == key.mirror &&
            cached->combine0 == key.combine0 && cached->combine1 == key.combine1 &&
            cached->prim == key.prim && cached->env == key.env && cached->cycle == key.cycle &&
            cached->paletteHash == key.paletteHash && cached->contentHash == key.contentHash) {
            cached->used = ++s_cacheClock;
            cached->frame = s_textureFrame;
            texture_descriptor(tile, uploadW, uploadH, cached->vram);
            tile->cacheSlot = i;
            tile->gsTextureLoaded = true;
            rdp->stats.textureHits++;
            ps2_gfx_profile_leave(GFX_PROFILE_LOOKUP, lookupStart);
            return;
        }
    }
    ps2_gfx_profile_leave(GFX_PROFILE_LOOKUP, lookupStart);
    if (gPS2GfxProfileActive) {
        unsigned reason = profileCandidate < 0 ? 0 :
            s_cache[profileCandidate].contentHash != key.contentHash ||
            s_cache[profileCandidate].paletteHash != key.paletteHash ||
            s_cache[profileCandidate].lodFrac != key.lodFrac ? 1 : 2;
        ps2_gfx_profile_texture_miss((u32)key.source,key.prim,key.env,w,h,key.fmt,key.siz,reason,pathPair);
    }
    u32 convertStart = ps2_gfx_profile_enter();
    if (sourceStride && sourceStride != rowBytes) {
        for (int y = 0; y < h; y++)
            memcpy(s_sourceRows + y * rowBytes, src + y * sourceStride, rowBytes);
        src = s_sourceRows;
    }

    /* Map RGBA16 channels while decoding them, avoiding a full RGBA scratch
     * write/read pass. The two-source path retains its combined-alpha math. */
    const CombinerLookup* mapping = pathPair ? NULL : combiner_lookup(rdp);
    bool mappedRGBA16 = !pathPair && tile->fmt == G_IM_FMT_RGBA && tile->siz == G_IM_SIZ_16b;
    bool mappedCompact = !pathPair && convert_compact_mapped(src, s_convBuf, w*h, tile, mapping);
    /* ── Format conversion ──────────────────────────────── */
    if (!mappedCompact) switch (tile->fmt) {
        case G_IM_FMT_RGBA:
            if (mappedRGBA16)
                convert_rgba16_mapped(src, s_convBuf, w*h, mapping);
            else if (tile->siz == G_IM_SIZ_16b)
                convert_rgba16_to_rgba32(src, s_convBuf, w, h);
            else if (tile->siz == G_IM_SIZ_32b)
                convert_rgba32_to_rgba32(src, s_convBuf, w, h);
            else
                memset(s_convBuf, 0xFF, w * h * 4);
            break;

        case G_IM_FMT_IA:
            if (tile->siz == G_IM_SIZ_4b)
                convert_ia4_to_rgba32(src, s_convBuf, w, h);
            else if (tile->siz == G_IM_SIZ_8b)
                convert_ia8_to_rgba32(src, s_convBuf, w, h);
            else if (tile->siz == G_IM_SIZ_16b)
                convert_ia16_to_rgba32(src, s_convBuf, w, h);
            else
                memset(s_convBuf, 0xFF, w * h * 4);
            break;

        case G_IM_FMT_I:
            if (tile->siz == G_IM_SIZ_8b)
                convert_i8_to_rgba32(src, s_convBuf, w, h);
            else if (tile->siz == G_IM_SIZ_4b)
                convert_i4_to_rgba32(src, s_convBuf, w, h);
            else
                memset(s_convBuf, 0xFF, w * h * 4);
            break;

        case G_IM_FMT_CI:
            /* CI4/CI8: expand palette indices to RGBA32 */
            if (tile->siz == G_IM_SIZ_8b) {
                for (int i = 0; i < w * h; i++) {
                    u32 c = s_clut[src[i]];
                    memcpy(s_convBuf + i * 4, &c, 4);
                }
            } else if (tile->siz == G_IM_SIZ_4b) {
                for (int i = 0; i < w * h; i++) {
                    u8 idx = (i & 1) ? (src[i/2] & 0x0F)
                                     : (src[i/2] >> 4);
                    u32 c = s_clut[(tile->palette << 4) | (idx & 0x0F)];
                    memcpy(s_convBuf + i * 4, &c, 4);
                }
            }
            break;

        default:
            memset(s_convBuf, 0xAA, w * h * 4);
            break;
    }

    /* Bake constant RDP inputs into texels. Shade modulation remains on the
     * GS vertices; full multitexture and additive shade terms need more passes. */
    if (pathPair) {
        unsigned factor = rdp->envColor & 255;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            u8* pixel = s_convBuf + (y * w + x) * 4;
            u16 raw = read_be16(secondSrc + y * (key.stride2 ? key.stride2 : rowBytes) + x * 2);
            u8 texel[4] = {(raw >> 11) & 31, (raw >> 6) & 31, (raw >> 1) & 31, (raw & 1) ? 128 : 0};
            for (int c = 0; c < 3; c++) {
                unsigned value = (texel[c] << 3) | (texel[c] >> 2);
                pixel[c] += ((int)value - pixel[c]) * (int)factor / 255;
            }
            int alpha = pixel[3] + ((int)texel[3] - pixel[3]) * (int)factor / 255;
            alpha += texel[3] * key.lodFrac / 255;
            pixel[3] = alpha > 128 ? 128 : alpha;
        }
    } else if (!mappedRGBA16 && !mappedCompact) {
      const u8 (*lookup)[256] = mapping->values;
      for (int i = 0; i < w * h; i++) {
        u8* pixel = s_convBuf + i * 4;
        pixel[0] = lookup[0][pixel[0]];
        pixel[1] = lookup[1][pixel[1]];
        pixel[2] = lookup[2][pixel[2]];
        pixel[3] = lookup[3][pixel[3] == 128 ? 255 : pixel[3] * 2];
      }
    }
    static unsigned logoTrace;
    if (tile->fmt == G_IM_FMT_RGBA && tile->siz == G_IM_SIZ_32b && w == 160 && logoTrace++ < 3) {
        printf("[PS2 LOGO TEX] ptr=%p stride=%u size=%dx%d combine=%08x,%08x mode=%08x prim=%08x raw=%02x%02x%02x%02x pixel=%02x%02x%02x%02x\n",
            src, sourceStride, w, h, rdp->combiner.w0, rdp->combiner.w1, rdp->otherMode.hi,
            rdp->primColor, src[0], src[1], src[2], src[3], s_convBuf[0], s_convBuf[1], s_convBuf[2], s_convBuf[3]);
    }
    if (mirrorS || mirrorT) {
        for (int y = 0; y < uploadH; y++) {
            int sy = mirrorT && y >= uploadH / 2 ? uploadH - 1 - y : y;
            for (int x = 0; x < uploadW; x++) {
                int sx = mirrorS && x >= uploadW / 2 ? uploadW - 1 - x : x;
                memcpy(s_sourceRows + (y * uploadW + x) * 4,
                       s_convBuf + (sy * w + sx) * 4, 4);
            }
        }
        memcpy(s_convBuf, s_sourceRows, uploadW * uploadH * 4);
    }
    ps2_gfx_profile_leave(GFX_PROFILE_CONVERT, convertStart);
    u32 uploadStart = ps2_gfx_profile_enter();
    /* ── Upload to GS VRAM ──────────────────────────────── */
    GSTEXTURE* gt = &tile->gsTexture;
    texture_descriptor(tile, uploadW, uploadH, 0);

    /* A zero-initialized GSTEXTURE points at the first framebuffer in VRAM.
     * Reserve a texture-only region and retire draws before reusing it. */
    if (s_textureVram == GSKIT_ALLOC_ERROR) {
        /* Leave room for small GS allocations, and also support diagnostic
         * framebuffers that still use CT24 (704 KiB available there). */
        u32 available = rdp->gs->CurrentPointer < 0x400000 - 32768 ?
            0x400000 - 32768 - rdp->gs->CurrentPointer : 0;
        s_textureCacheBytes = available < TEXTURE_CACHE_BYTES ? available : TEXTURE_CACHE_BYTES;
        s_textureCacheBytes &= ~(TEXTURE_CACHE_CHUNK - 1);
        if (!s_textureCacheBytes) return;
        s_textureVram = gsKit_vram_alloc(rdp->gs, s_textureCacheBytes, GSKIT_ALLOC_USERBUFFER);
        if (s_textureVram == GSKIT_ALLOC_ERROR) return;
        printf("[PS2 TEXTURE] VRAM cache reserved at 0x%08x (%u bytes)\n",
               (unsigned)s_textureVram, (unsigned)s_textureCacheBytes);
    }
    u32 size = gsKit_texture_size(uploadW, uploadH, GS_PSM_CT32);
    if (size > sizeof(s_convBuf) || size > s_textureCacheBytes) return;
    size = (size + TEXTURE_CACHE_CHUNK - 1) & ~(TEXTURE_CACHE_CHUNK - 1);
    int slot = reusable >= 0 ? reusable : 0;
    for (int i = 0; reusable < 0 && i < TEXTURE_CACHE_SLOTS; i++) {
        if (!s_cache[i].valid || s_cache[i].used < s_cache[slot].used) {
            slot = i;
            if (!s_cache[i].valid) break;
        }
    }
    u32 bestCost, vram;
    if (reusable >= 0 && s_cache[slot].size == size) {
        vram = s_cache[slot].vram;
        bestCost = s_cache[slot].used;
    } else vram = cache_region(size, &bestCost);
    /* Only overlapping VRAM requires a fence; retiring a descriptor alone
     * cannot change pending GIF packets. One fence retires every older read. */
    bool pending = false;
    for (int i = 0; i < TEXTURE_CACHE_SLOTS; i++) {
        const TextureCacheEntry* entry = &s_cache[i];
        if (entry->valid && entry->used > s_retiredClock &&
            entry->vram < vram + size && vram < entry->vram + entry->size) {
            pending = true; break;
        }
    }
    if (pending) {
        rdp->stats.textureWaits++;
        gsKit_set_finish(rdp->gs);
        gPS2GfxTraceStage = 2;
        if (trace) printf("[PS2 TEXTURE TRACE] converted, retire queue\n");
        gsKit_queue_exec(rdp->gs);
        gPS2GfxTraceStage = 3;
        gsKit_finish();
        s_retiredClock = s_cacheClock;
        if (trace) printf("[PS2 TEXTURE TRACE] finish returned\n");
    }
    dmaKit_wait_fast();
    for (int i = 0; i < TEXTURE_CACHE_SLOTS; i++) {
        TextureCacheEntry* entry = &s_cache[i];
        if (entry->valid && (i == slot || (entry->vram < vram + size && vram < entry->vram + entry->size))) {
            for (int tileIndex = 0; tileIndex < MAX_TILES; tileIndex++)
                if (rdp->tile[tileIndex].gsTexture.Vram == entry->vram) rdp->tile[tileIndex].gsTextureLoaded = false;
            cache_unlink(i);
            entry->valid = 0;
        }
    }
    gt->Vram = vram;
    gPS2GfxTraceStage = 4;
    gsKit_texture_upload(rdp->gs, gt);
    gPS2GfxTraceStage = 5;
    dmaKit_wait_fast();
    gPS2GfxTraceStage = 6;
    key.vram = vram; key.size = size; key.used = ++s_cacheClock;
    key.frame = s_textureFrame; key.valid = 1;
    s_cache[slot] = key;
    unsigned bucket = cache_bucket(key.source);
    s_cacheNext[slot] = s_cacheHeads[bucket];
    s_cacheHeads[bucket] = slot;
    tile->cacheSlot = slot;
    static unsigned uploads;
    if (++uploads <= 3 || uploads % 1000 == 0)
        printf("[PS2 TEXTURE] uploads=%u size=%dx%d source=%p slot=%d\n", uploads, w, h, tile->addr, slot);
    tile->gsTextureLoaded = true;
    rdp->stats.textureUploads++;
    ps2_gfx_profile_leave(GFX_PROFILE_UPLOAD, uploadStart);
}

/* ─────────────────────────────────────────────────────────── *
 *  RDP command: G_SETTIMG — set texture image source pointer
 * ─────────────────────────────────────────────────────────── */
void rdp_settimg(u32 w0, u32 w1, RDPState* rdp) {
    rdp->textureImageFmt   = (N64TexFmt)((w0 >> 21) & 0x7);
    rdp->textureImageSiz   = (N64TexSiz)((w0 >> 19) & 0x3);
    rdp->textureImageWidth = (w0 & 0xFFF) + 1;
    /* CORREGIDO: antes se guardaba w1 crudo. Las texturas viven en
     * segmentos (el 6 para objetos, el 4 para escenas...), así que sin
     * traducir esto el puntero no apuntaba a ninguna parte. */
    rdp->textureImage      = ps2_segmented_to_virtual(w1);
}

/* ─────────────────────────────────────────────────────────── *
 *  RDP command: G_SETTILE
 * ─────────────────────────────────────────────────────────── */
void rdp_settile(u32 w0, u32 w1, RDPState* rdp) {
    u8 tile_idx = (w1 >> 24) & 0x7;
    TileDescriptor* t = &rdp->tile[tile_idx];

    t->fmt     = (N64TexFmt)((w0 >> 21) & 0x7);
    t->siz     = (N64TexSiz)((w0 >> 19) & 0x3);
    t->tmem    = w0 & 0x1FF;
    t->line    = (w0 >> 9) & 0x1FF;
    t->cms     = (w1 >> 8) & 0x3;
    t->cmt     = (w1 >> 18) & 0x3;
    t->masks   = (w1 >> 4) & 0xF;
    t->maskt   = (w1 >> 14) & 0xF;
    t->shifts  = w1 & 0xF;
    t->shiftt  = (w1 >> 10) & 0xF;
    t->palette = (w1 >> 20) & 0xF;
    t->gsTextureLoaded = false;  /* invalidate cached upload */
}

/* ─────────────────────────────────────────────────────────── *
 *  RDP command: G_SETTILESIZE
 * ─────────────────────────────────────────────────────────── */
void rdp_settilesize(u32 w0, u32 w1, RDPState* rdp) {
    u8 tile_idx = (w1 >> 24) & 0x7;
    TileDescriptor* t = &rdp->tile[tile_idx];

    t->uls = (w0 >> 12) & 0xFFF;
    t->ult = (w0      ) & 0xFFF;
    t->lrs = (w1 >> 12) & 0xFFF;
    t->lrt = (w1      ) & 0xFFF;

    /* Compute texture dimensions from tile extents (10.2 fixed) */
    t->width  = ((t->lrs >> 2) - (t->uls >> 2)) + 1;
    t->height = ((t->lrt >> 2) - (t->ult >> 2)) + 1;
    t->gsTextureLoaded = false;
}

/* ─────────────────────────────────────────────────────────── *
 *  RDP: G_LOADBLOCK / G_LOADTILE — actually load texture data
 * ─────────────────────────────────────────────────────────── */
void rdp_loadtexture(u8 op, u32 w0, u32 w1, RDPState* rdp) {
    TileDescriptor* loadTile = &rdp->tile[(w1 >> 24) & 7];
    u32 bits = 4u << rdp->textureImageSiz;
    u32 x = ((w0 >> 12) & 0xFFF) >> 2;
    u32 y = (w0 & 0xFFF) >> 2;
    u32 offset = (y * rdp->textureImageWidth + x) * bits / 8;
    u32 tmem = loadTile->tmem;
    rdp->textureLoads[tmem].source = rdp->textureImage ? (const u8*)rdp->textureImage + offset : NULL;
    rdp->textureLoads[tmem].stride = op == 0xF4 ? (rdp->textureImageWidth * bits + 7) / 8 : 0;
    /* Load and render descriptors often differ (7 versus 0). The shared
     * TMEM address connects them; SETTIMG alone must not change other tiles. */
    for (int i = 0; i < MAX_TILES; i++) {
        if (rdp->tile[i].tmem == tmem) {
            rdp->tile[i].addr = (void*)rdp->textureLoads[tmem].source;
            rdp->tile[i].gsTextureLoaded = false;
        }
    }
    if (ps2_combiner_path_lerp(rdp) && rdp->tile[(rdp->textureTile + 1) & 7].tmem == tmem)
        rdp->tile[rdp->textureTile].gsTextureLoaded = false;
}

/* ─────────────────────────────────────────────────────────── *
 *  RDP: G_LOADTLUT — load color lookup table (palette)
 * ─────────────────────────────────────────────────────────── */
void rdp_loadtlut(u32 w0, u32 w1, RDPState* rdp) {
    u8  tile_idx = (w1 >> 24) & 0x7;
    u16 count    = ((w1 >> 14) & 0x3FF) + 1;
    /* Each palette entry occupies one 64-bit TMEM word (replicated lanes).
     * CI4 palettes are loaded at 256 + palette * 16, not always at zero. */
    u16 tmem = rdp->tile[tile_idx].tmem;
    if (rdp->textureImage && tmem >= 256 && count <= 512 - tmem) {
        ps2_texture_load_tlut_at((const u8*)rdp->textureImage, tmem - 256, count);
        for (int i = 0; i < MAX_TILES; i++)
            if (rdp->tile[i].fmt == G_IM_FMT_CI) rdp->tile[i].gsTextureLoaded = false;
    }
}
