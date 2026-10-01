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
    u32 stride, combine0, combine1, prim, env, cycle, paletteHash, contentHash;
    u32 vram, size, used;
    u16 width, height;
    u8 fmt, siz, palette, valid;
} TextureCacheEntry;
/* Share the entire 704 KiB budget between actual texture sizes. Fixed size
 * classes stranded most of VRAM while menu strips evicted one another. */
#define TEXTURE_CACHE_SLOTS 64
#define TEXTURE_CACHE_BYTES (704u * 1024)
static TextureCacheEntry s_cache[TEXTURE_CACHE_SLOTS];
static u32 s_cacheClock;
typedef struct CombinerLookup {
    u32 combine0, combine1, prim, env, cycle, used;
    u8 valid, values[4][256];
} CombinerLookup;
static CombinerLookup s_lookups[16];
static u32 s_lookupClock;
extern volatile unsigned gPS2GfxTraceStage;

/* Many tiles share a combiner. Build its 512 evaluations once rather than
 * repeating them on every texture upload. Preserve the exact old mapping. */
static const u8 (*combiner_lookup(const RDPState* rdp))[256] {
    u32 cycle = (rdp->otherMode.hi >> 20) & 3;
    u32 prim, env;
    ps2_combiner_constants(rdp, &prim, &env);
    int slot = 0;
    for (int i = 0; i < 16; i++) {
        CombinerLookup* entry = &s_lookups[i];
        if (entry->valid && entry->combine0 == rdp->combiner.w0 &&
            entry->combine1 == rdp->combiner.w1 && entry->prim == prim &&
            entry->env == env && entry->cycle == cycle) {
            entry->used = ++s_lookupClock;
            return entry->values;
        }
        if (!entry->valid || (s_lookups[slot].valid && entry->used < s_lookups[slot].used)) slot = i;
    }
    CombinerLookup* entry = &s_lookups[slot];
    entry->combine0 = rdp->combiner.w0; entry->combine1 = rdp->combiner.w1;
    entry->prim = prim; entry->env = env; entry->cycle = cycle;
    const u8 fullShade[4] = {255, 255, 255, 255};
    for (int value = 0; value < 256; value++) {
        u8 texel[4] = {value, value, value, 255}, color[4];
        ps2_combiner_color(rdp, texel, fullShade, color);
        for (int channel = 0; channel < 3; channel++) entry->values[channel][value] = color[channel];
        texel[0] = texel[1] = texel[2] = 255;
        texel[3] = value;
        ps2_combiner_color(rdp, texel, fullShade, color);
        entry->values[3][value] = color[3] * 128u / 255;
    }
    entry->used = ++s_lookupClock;
    entry->valid = 1;
    return entry->values;
}

void ps2_texture_apply_tile(TileDescriptor* tile, RDPState* rdp) {
    GSGLOBAL* gs = rdp->gs;
    if (!gs) return;
    /* Region clamp also excludes uninitialized padding in non-power-of-two
     * strips. Region repeat uses the N64 mask without clamping CPU vertices. */
    unsigned modeS = (tile->cms & 2) || !tile->masks ? 2 : 3;
    unsigned modeT = (tile->cmt & 2) || !tile->maskt ? 2 : 3;
    unsigned minS = modeS == 3 ? (1u << (tile->masks > 10 ? 10 : tile->masks)) - 1 : 0;
    unsigned minT = modeT == 3 ? (1u << (tile->maskt > 10 ? 10 : tile->maskt)) - 1 : 0;
    unsigned maxS = modeS == 2 && tile->width ? tile->width - 1 : 0;
    unsigned maxT = modeT == 2 && tile->height ? tile->height - 1 : 0;
    gs->Clamp->WMS = modeS; gs->Clamp->WMT = modeT;
    gs->Clamp->MINU = minS; gs->Clamp->MAXU = maxS;
    gs->Clamp->MINV = minT; gs->Clamp->MAXV = maxT;
    u64* packet = gsKit_heap_alloc(gs, 1, 16, GIF_AD);
    packet[0] = GIF_TAG_AD(1); packet[1] = GIF_AD;
    packet[2] = GS_SETREG_CLAMP(modeS, modeT, minS, maxS, minT, maxT);
    packet[3] = GS_CLAMP_1 + gs->PrimContext;
    tile->gsTexture.Filter = ((rdp->otherMode.hi >> 12) & 3) ? GS_FILTER_LINEAR : GS_FILTER_NEAREST;
}

static float texture_shift(float coordinate, u8 shift) {
    return shift <= 10 ? coordinate / (1u << shift) : coordinate * (1u << (16 - shift));
}

void ps2_texture_build_vertex(const TileDescriptor* tile, const RSPState* rsp,
    const RDPState* rdp, const RSPVertex* vertex, u32 color, u32 depth,
    GSPRIMSTQPOINT* output) {
    unsigned width = 1, height = 1;
    while (width < tile->width) width <<= 1;
    while (height < tile->height) height <<= 1;
    float u = texture_shift(vertex->tc_s * rsp->texScaleS / 32.0f, tile->shifts) - tile->uls / 4.0f;
    float v = texture_shift(vertex->tc_t * rsp->texScaleT / 32.0f, tile->shiftt) - tile->ult / 4.0f;
    float q = (rdp->otherMode.hi & (1u << 19)) && vertex->cw > 0.0001f ? 1.0f / vertex->cw : 1.0f;
    output->rgbaq = rgba_to_RGBAQ(color, q);
    output->stq = vertex_to_STQ(u * q / width, v * q / height);
    output->xyz2 = vertex_to_XYZ2(rdp->gs, vertex->sx, vertex->sy, depth);
}

void ps2_texture_end_frame(RDPState* rdp) {
    /* Recheck source bytes next frame: animated/reused RAM cannot retain an
     * old texture just because its address is unchanged. VRAM stays cached. */
    for (int i = 0; i < MAX_TILES; i++) rdp->tile[i].gsTextureLoaded = false;
}

static void texture_descriptor(TileDescriptor* tile, int w, int h, u32 vram) {
    GSTEXTURE* gt = &tile->gsTexture;
    gt->Width = w; gt->Height = h; gt->PSM = GS_PSM_CT32;
    gt->Mem = (u32*)s_convBuf; gt->Filter = GS_FILTER_LINEAR;
    gt->Clut = NULL; gt->Vram = vram;
    /* Upload normally computes this; cache hits must also restore it. */
    gt->TBW = (w + 63) / 64;
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
        a = (a >> 1);  /* PS2 GS uses 0-128 alpha (divide by 2) */

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
        alpha >>= 1;  /* GS alpha 0-128 */
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
        u8 alpha     = src[i * 2 + 1] >> 1;
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
    static unsigned sceneUploads;
    int trace = rdp->stats.triangles > 96 && sceneUploads++ < 12;
    if (trace) printf("[PS2 TEXTURE TRACE] begin %dx%d fmt=%u siz=%u ptr=%p\n", w, h, tile->fmt, tile->siz, tile->addr);
    u32 sourceStride = rdp->textureLoads[tile->tmem].stride;
    TextureCacheEntry key = {0};
    key.source = (uintptr_t)tile->addr;
    key.stride = sourceStride;
    key.width = w; key.height = h; key.fmt = tile->fmt; key.siz = tile->siz;
    key.palette = tile->palette;
    key.combine0 = rdp->combiner.w0; key.combine1 = rdp->combiner.w1;
    ps2_combiner_constants(rdp, &key.prim, &key.env);
    key.cycle = (rdp->otherMode.hi >> 20) & 3;
    if (tile->fmt == G_IM_FMT_CI) {
        unsigned first = tile->siz == G_IM_SIZ_4b ? tile->palette * 16 : 0;
        unsigned count = tile->siz == G_IM_SIZ_4b ? 16 : 256;
        key.paletteHash = 2166136261u;
        for (unsigned i = 0; i < count; i++) key.paletteHash = (key.paletteHash ^ s_clut[first + i]) * 16777619u;
    }
    u32 hash = 2166136261u;
    for (int y = 0; y < h; y++) {
        const u8* row = src + y * (sourceStride ? sourceStride : rowBytes);
        for (u32 x = 0; x < rowBytes; x++) hash = (hash ^ row[x]) * 16777619u;
    }
    key.contentHash = hash;
    for (int i = 0; i < TEXTURE_CACHE_SLOTS; i++) {
        TextureCacheEntry* cached = &s_cache[i];
        if (cached->valid && cached->source == key.source &&
            cached->stride == key.stride && cached->width == w && cached->height == h &&
            cached->fmt == key.fmt && cached->siz == key.siz && cached->palette == key.palette &&
            cached->combine0 == key.combine0 && cached->combine1 == key.combine1 &&
            cached->prim == key.prim && cached->env == key.env && cached->cycle == key.cycle &&
            cached->paletteHash == key.paletteHash && cached->contentHash == key.contentHash) {
            cached->used = ++s_cacheClock;
            texture_descriptor(tile, w, h, cached->vram);
            tile->gsTextureLoaded = true;
            return;
        }
    }
    if (sourceStride && sourceStride != rowBytes) {
        for (int y = 0; y < h; y++)
            memcpy(s_sourceRows + y * rowBytes, src + y * sourceStride, rowBytes);
        src = s_sourceRows;
    }

    /* ── Format conversion ──────────────────────────────── */
    switch (tile->fmt) {
        case G_IM_FMT_RGBA:
            if (tile->siz == G_IM_SIZ_16b)
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
    const u8 (*lookup)[256] = combiner_lookup(rdp);
    for (int i = 0; i < w * h; i++) {
        u8* pixel = s_convBuf + i * 4;
        pixel[0] = lookup[0][pixel[0]];
        pixel[1] = lookup[1][pixel[1]];
        pixel[2] = lookup[2][pixel[2]];
        pixel[3] = lookup[3][pixel[3] == 128 ? 255 : pixel[3] * 2];
    }
    static unsigned logoTrace;
    if (tile->fmt == G_IM_FMT_RGBA && tile->siz == G_IM_SIZ_32b && w == 160 && logoTrace++ < 3) {
        printf("[PS2 LOGO TEX] ptr=%p stride=%u size=%dx%d combine=%08x,%08x mode=%08x prim=%08x raw=%02x%02x%02x%02x pixel=%02x%02x%02x%02x\n",
            src, sourceStride, w, h, rdp->combiner.w0, rdp->combiner.w1, rdp->otherMode.hi,
            rdp->primColor, src[0], src[1], src[2], src[3], s_convBuf[0], s_convBuf[1], s_convBuf[2], s_convBuf[3]);
    }
    /* ── Upload to GS VRAM ──────────────────────────────── */
    GSTEXTURE* gt = &tile->gsTexture;
    gt->Width     = w;
    gt->Height    = h;
    gt->PSM       = GS_PSM_CT32;
    gt->Mem       = (void*)s_convBuf;
    gt->Filter    = GS_FILTER_LINEAR;
    gt->Clut      = NULL;

    /* A zero-initialized GSTEXTURE points at the first framebuffer in VRAM.
     * Reserve a texture-only region and retire draws before reusing it. */
    if (s_textureVram == GSKIT_ALLOC_ERROR) {
        s_textureVram = gsKit_vram_alloc(rdp->gs, TEXTURE_CACHE_BYTES, GSKIT_ALLOC_USERBUFFER);
        if (s_textureVram == GSKIT_ALLOC_ERROR) return;
        printf("[PS2 TEXTURE] VRAM cache reserved at 0x%08x (%u bytes)\n",
               (unsigned)s_textureVram, (unsigned)TEXTURE_CACHE_BYTES);
    }
    u32 size = gsKit_texture_size(w, h, GS_PSM_CT32);
    if (size > sizeof(s_convBuf)) return;
    size = (size + 4095) & ~4095u;
    int slot = 0;
    for (int i = 0; i < TEXTURE_CACHE_SLOTS; i++) {
        if (!s_cache[i].valid || s_cache[i].used < s_cache[slot].used) {
            slot = i;
            if (!s_cache[i].valid) break;
        }
    }
    u32 vram = s_textureVram, bestCost = ~0u;
    for (u32 offset = 0; offset + size <= TEXTURE_CACHE_BYTES; offset += 4096) {
        u32 start = s_textureVram + offset, cost = 0;
        for (int i = 0; i < TEXTURE_CACHE_SLOTS; i++) {
            TextureCacheEntry* entry = &s_cache[i];
            if (entry->valid && entry->vram < start + size && start < entry->vram + entry->size && entry->used > cost)
                cost = entry->used;
        }
        if (cost < bestCost) { bestCost = cost; vram = start; }
        if (!cost) break;
    }
    gsKit_set_finish(rdp->gs);
    gPS2GfxTraceStage = 2;
    if (trace) printf("[PS2 TEXTURE TRACE] converted, retire queue\n");
    gsKit_queue_exec(rdp->gs);
    gPS2GfxTraceStage = 3;
    gsKit_finish();
    if (trace) printf("[PS2 TEXTURE TRACE] finish returned\n");
    dmaKit_wait_fast();
    for (int i = 0; i < TEXTURE_CACHE_SLOTS; i++) {
        TextureCacheEntry* entry = &s_cache[i];
        if (entry->valid && (i == slot || (entry->vram < vram + size && vram < entry->vram + entry->size))) {
            for (int tileIndex = 0; tileIndex < MAX_TILES; tileIndex++)
                if (rdp->tile[tileIndex].gsTexture.Vram == entry->vram) rdp->tile[tileIndex].gsTextureLoaded = false;
            entry->valid = 0;
        }
    }
    gt->Vram = vram;
    gPS2GfxTraceStage = 4;
    gsKit_texture_upload(rdp->gs, gt);
    gPS2GfxTraceStage = 5;
    dmaKit_wait_fast();
    gPS2GfxTraceStage = 6;
    key.vram = vram; key.size = size; key.used = ++s_cacheClock; key.valid = 1;
    s_cache[slot] = key;
    static unsigned uploads;
    if (++uploads <= 3 || uploads % 1000 == 0)
        printf("[PS2 TEXTURE] uploads=%u size=%dx%d source=%p slot=%d\n", uploads, w, h, tile->addr, slot);
    tile->gsTextureLoaded = true;
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
