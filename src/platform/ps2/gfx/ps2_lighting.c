/**
 * ps2_lighting.c — F3DEX2 Hardware Lighting Emulation
 *
 * Emulates the N64 RSP's vertex lighting calculation.
 * In OoT, when G_LIGHTING is enabled in the geometry mode:
 *   - The RGB bytes of the vertex are treated as a Normal vector (Nx, Ny, Nz).
 *   - The RSP calculates diffuse lighting using dot products of the normal 
 *     against up to 7 directional lights, plus an ambient light.
 *   - The resulting color replaces the vertex color before rendering.
 */

#include <math.h>
#include <string.h>

#include "ps2_types.h"
#include "rdp_to_gs.h"
#include "ps2_lighting.h"
#include "ps2_gfx_profile.h"

static PS2LightingCacheStats s_cacheStats;
#if PS2_NORMAL_VECTOR_REUSE || defined(PS2_RENDERER_SMOKE_CAPTURE)
/* Normals depend only on the three signed source bytes, not the current
 * light table or model matrix. The high bit is a separate valid guard;
 * even the all-zero normal must pass a real key check after BSS clearing. */
typedef struct { u32 key; float x, y, z; } NormalVectorEntry;
static NormalVectorEntry s_normalVectors[256];
_Static_assert(sizeof(s_normalVectors) == 4096, "normal cache must fit 4 KiB");
#ifdef PS2_RENDERER_SMOKE_CAPTURE
static bool s_normalReuse = true;
#endif
#endif
#ifdef PS2_RENDERER_SMOKE_CAPTURE
#define LIGHT_STAT_INC(field) (++s_cacheStats.field)
#else
#define LIGHT_STAT_INC(field) do { if (gPS2GfxProfileActive) s_cacheStats.field++; } while (0)
#endif

PS2LightingCacheStats ps2_light_cache_stats(bool reset) {
    PS2LightingCacheStats result = s_cacheStats;
#if PS2_NORMAL_VECTOR_REUSE || defined(PS2_RENDERER_SMOKE_CAPTURE)
    result.storageBytes = sizeof(s_normalVectors);
#endif
    result.rgbEntries = 1u << PS2_LIGHT_RGB_CACHE_BITS;
    result.rgbStorageBytes = result.rgbEntries * 3u * sizeof(u32);
    if (reset) memset(&s_cacheStats, 0, sizeof(s_cacheStats));
    return result;
}

/* 
 * N64 Light structure format (16 bytes):
 * 0x00: col[3] (RGB diffuse color)
 * 0x03: pad
 * 0x04: colc[3] (RGB copy)
 * 0x07: pad
 * 0x08: dir[3] (XYZ direction, normalized to -128..127)
 * 0x0B: pad
 */

/* ─────────────────────────────────────────────────────────── *
 *  Load Lights into RSP state
 * ─────────────────────────────────────────────────────────── */
void ps2_light_load(u32 index, const u8* src, RSPState* rsp) {
    if (index >= 8) return; /* RSP supports max 8 lights */
    
    RSPLight* light = &rsp->lights[index];
    
    /* Read diffuse color */
    light->r = src[0];
    light->g = src[1];
    light->b = src[2];
    
    /* Read direction vector (s8, mapped from -128 to 127) */
    /* N64 vectors need to be normalized to float for PS2 math */
    s8 dx = (s8)src[8];
    s8 dy = (s8)src[9];
    s8 dz = (s8)src[10];
    
    /* Normalize direction vector */
    float len = sqrtf((float)(dx*dx + dy*dy + dz*dz));
    if (len > 0.0f) {
        light->dx = (float)dx / len;
        light->dy = (float)dy / len;
        light->dz = (float)dz / len;
    } else {
        light->dx = light->dy = light->dz = 0.0f;
    }
}

/* ─────────────────────────────────────────────────────────── *
 *  Set number of active lights
 * ─────────────────────────────────────────────────────────── */
void ps2_light_set_count(u32 numLights, RSPState* rsp) {
    /* N64 encodes this weirdly: numLights is (actual_lights - 1) * 32.
     * We just store the actual count. 
     * E.g., if numLights == 1, there is 1 directional + 1 ambient. */
    if (numLights > 7) numLights = 7;
    rsp->numLights = numLights;
}

/* ─────────────────────────────────────────────────────────── *
 *  Apply lighting to a single vertex
 * ─────────────────────────────────────────────────────────── */
static void light_apply_vertex(RSPVertex* v, const RSPState* rsp,
                               u32 normal, unsigned slot, bool reuseNormal) {
    /* If lighting is off, the vertex color is just the raw color. */
    if (!(rsp->geometryMode & 0x00020000 /* G_LIGHTING */)) {
        return;
    }

    /* Ambient light is always the first light in the F3DEX2 table (index 0 or numLights)
     * Actually, in F3DEX2, ambient is light 'numLights', and dir lights are 0 to numLights-1 */
    
    u32 ambientIdx = rsp->numLights;
    if (!ambientIdx) {
        v->cr = rsp->lights[0].r;
        v->cg = rsp->lights[0].g;
        v->cb = rsp->lights[0].b;
        return;
    }
    float r = (float)rsp->lights[ambientIdx].r;
    float g = (float)rsp->lights[ambientIdx].g;
    float b = (float)rsp->lights[ambientIdx].b;

    /* Extract normals from vertex color bytes (-128 to +127) */
    float nx = (float)((s8)v->cr);
    float ny = (float)((s8)v->cg);
    float nz = (float)((s8)v->cb);
    
    /* Normalize vertex normal */
#if PS2_NORMAL_VECTOR_REUSE || defined(PS2_RENDERER_SMOKE_CAPTURE)
#ifdef PS2_RENDERER_SMOKE_CAPTURE
    reuseNormal = reuseNormal && s_normalReuse;
#endif
    u32 key = normal | 0x80000000u;
    NormalVectorEntry* cached = &s_normalVectors[slot];
    if (reuseNormal && cached->key == key) {
        nx = cached->x; ny = cached->y; nz = cached->z;
        LIGHT_STAT_INC(normalHits);
    } else
#endif
    {
        /* Keep the original sqrt and three divisions, including zero.
         * A reciprocal or pre-scaled light changes rounding near byte edges. */
        float nlen = sqrtf(nx*nx + ny*ny + nz*nz);
        if (nlen > 0.0f) {
            nx /= nlen;
            ny /= nlen;
            nz /= nlen;
        }
#if PS2_NORMAL_VECTOR_REUSE || defined(PS2_RENDERER_SMOKE_CAPTURE)
        if (reuseNormal) {
            *cached = (NormalVectorEntry){key,nx,ny,nz};
            LIGHT_STAT_INC(normalBuilds);
        }
#endif
    }

    /* Add diffuse contributions from directional lights */
    for (u32 i = 0; i < rsp->numLights; i++) {
        const RSPLight* l = &rsp->lights[i];
        
        /* Dot product of normal and light direction */
        float dot = nx * l->dx + ny * l->dy + nz * l->dz;
        
        if (dot > 0.0f) {
            r += (float)l->r * dot;
            g += (float)l->g * dot;
            b += (float)l->b * dot;
        }
    }

    /* Clamp colors to 255 */
    /* Ambient and positive diffuse contributions are nonnegative. After
     * the upper clamp these conversions are in 0..255; use signed integer
     * conversion so the EE does not generate the unsigned 2^31 split. */
    v->cr = (r > 255.0f) ? 255 : (u8)(s32)r;
    v->cg = (g > 255.0f) ? 255 : (u8)(s32)g;
    v->cb = (b > 255.0f) ? 255 : (u8)(s32)b;
    /* Alpha remains unchanged (v->ca) */
}

/* The public scalar path remains an independent arithmetic reference. */
void ps2_light_apply_vertex(RSPVertex* v, const RSPState* rsp) {
    light_apply_vertex(v, rsp, 0, 0, false);
}

/* A G_VTX batch has one immutable light table. Verify its actual contents
 * once before loading vertices: light commands, direct color writes and
 * state copies then all invalidate this cache without separate hooks.
 * Normal RGB bytes are its only per-vertex input; alpha is never cached. */
static RSPLight s_batchLights[8];
static u32 s_batchLightCount;
static bool s_batchValid;
static u32 s_lightGeneration;
static struct {
    u32 normal;
    u32 rgb;
    u32 generation;
} s_normalColors[1u << PS2_LIGHT_RGB_CACHE_BITS];
_Static_assert(sizeof(s_normalColors) == (1u << PS2_LIGHT_RGB_CACHE_BITS) * 12u,
    "RGB lighting cache entries must remain 12 bytes");

#ifdef PS2_RENDERER_SMOKE_CAPTURE
void ps2_light_test_normal_cache(bool enabled) {
    s_normalReuse = enabled;
    memset(s_normalVectors, 0, sizeof(s_normalVectors));
    memset(s_normalColors, 0, sizeof(s_normalColors));
    s_batchValid = false;
    s_lightGeneration = 0;
    ps2_light_cache_stats(true);
}
#endif

void ps2_light_prepare_batch(const RSPState* rsp) {
    if (!(rsp->geometryMode & 0x00020000u) || !rsp->numLights) return;
    size_t bytes = (rsp->numLights + 1u) * sizeof(RSPLight);
    if (!s_batchValid || s_batchLightCount != rsp->numLights ||
        memcmp(s_batchLights, rsp->lights, bytes)) {
        memcpy(s_batchLights, rsp->lights, bytes);
        s_batchLightCount = rsp->numLights;
        /* Most material/light changes invalidate in O(1). A wrapped tag
         * requires clearing once, before generation one can be reused. */
        if (!++s_lightGeneration) {
            memset(s_normalColors, 0, sizeof(s_normalColors));
            s_lightGeneration = 1;
        }
        s_batchValid = true;
        LIGHT_STAT_INC(tableChanges);
    }
}

void ps2_light_apply_prepared_vertex(RSPVertex* v, const RSPState* rsp) {
    if (!(rsp->geometryMode & 0x00020000u)) return;
    if (!rsp->numLights) {
        v->cr = rsp->lights[0].r;
        v->cg = rsp->lights[0].g;
        v->cb = rsp->lights[0].b;
        return;
    }
    if (!s_batchValid) ps2_light_prepare_batch(rsp);
    u32 normal = (u32)v->cr | ((u32)v->cg << 8) | ((u32)v->cb << 16);
    u32 hash = normal * 0x9e3779b1u;
    unsigned slot = hash >> (32 - PS2_LIGHT_RGB_CACHE_BITS);
    if (s_normalColors[slot].generation == s_lightGeneration &&
        s_normalColors[slot].normal == normal) {
        u32 rgb = s_normalColors[slot].rgb;
        v->cr = (u8)rgb;
        v->cg = (u8)(rgb >> 8);
        v->cb = (u8)(rgb >> 16);
        LIGHT_STAT_INC(rgbHits);
        return;
    }
    LIGHT_STAT_INC(rgbBuilds);
    /* The experimental normal cache stays at 256 entries independently
     * of the RGB cache size. Sharing its wider slot would exceed 4 KiB. */
    light_apply_vertex(v, rsp, normal, hash >> 24, true);
    s_normalColors[slot].normal = normal;
    s_normalColors[slot].rgb = (u32)v->cr | ((u32)v->cg << 8) | ((u32)v->cb << 16);
    s_normalColors[slot].generation = s_lightGeneration;
}
