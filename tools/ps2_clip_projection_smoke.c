/* Differential EE checks for homogeneous clipping and deferred viewport work. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "f3dex2_ps2.h"
#include "ps2_gfx_profile.h"
#include "ps2_texture.h"

/* Keep this oracle's edge traversal and arithmetic identical to the original
 * renderer. In particular, do not share its distance cache or plane shortcuts. */
static __attribute__((noinline)) float reference_distance(const RSPVertex* v, int plane) {
    switch (plane) {
        case 0: return v->cw + v->cx;
        case 1: return v->cw - v->cx;
        case 2: return v->cw + v->cy;
        case 3: return v->cw - v->cy;
        case 4: return v->cw + v->cz;
        default: return v->cw - v->cz;
    }
}

static __attribute__((noinline)) int reference_clip(const RSPVertex* a,
    const RSPVertex* b, const RSPVertex* c, RSPVertex* output) {
    RSPVertex polygons[2][12];
    polygons[0][0] = *a; polygons[0][1] = *b; polygons[0][2] = *c;
    int count = 3, current = 0;
    for (int plane = 0; plane < 6 && count; plane++) {
        RSPVertex* src = polygons[current];
        RSPVertex* dst = polygons[current ^ 1];
        int nextCount = 0;
        for (int i = 0; i < count; i++) {
            const RSPVertex* first = &src[i];
            const RSPVertex* last = &src[(i + 1) % count];
            float d0 = reference_distance(first, plane), d1 = reference_distance(last, plane);
            if (d0 >= 0) dst[nextCount++] = *first;
            if ((d0 >= 0) != (d1 >= 0)) {
                float t = d0 / (d0 - d1);
                RSPVertex* v = &dst[nextCount++];
                *v = *first;
#define INTERPOLATE(field) v->field = first->field + (last->field - first->field) * t
                INTERPOLATE(cx); INTERPOLATE(cy); INTERPOLATE(cz); INTERPOLATE(cw);
                INTERPOLATE(tc_s); INTERPOLATE(tc_t);
                INTERPOLATE(cr); INTERPOLATE(cg); INTERPOLATE(cb); INTERPOLATE(ca);
#undef INTERPOLATE
                v->clipFlags = 0;
            }
        }
        count = nextCount;
        current ^= 1;
    }
    memcpy(output, polygons[current], count * sizeof(*output));
    return count;
}

static u32 random_word(u32* state) {
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static void random_vertex(RSPVertex* v, u32* random) {
    memset(v, 0xa5, sizeof(*v));
    v->ox = random_word(random); v->oy = random_word(random); v->oz = random_word(random);
    v->flag = random_word(random);
    v->tc_s = random_word(random); v->tc_t = random_word(random);
    v->cr = random_word(random) >> 24; v->cg = random_word(random) >> 24;
    v->cb = random_word(random) >> 24; v->ca = random_word(random) >> 24;
    v->cx = ((int)(random_word(random) % 4097) - 2048) / 256.0f;
    v->cy = ((int)(random_word(random) % 4097) - 2048) / 256.0f;
    v->cz = ((int)(random_word(random) % 4097) - 2048) / 256.0f;
    v->cw = ((int)(random_word(random) % 2305) - 256) / 256.0f;
    v->sx = 17; v->sy = -23; v->sz = 0.75f;
    v->clipFlags = random_word(random); v->valid = true;
}

static unsigned compare_clip(const RSPVertex* vertices, unsigned fixture) {
    RSPVertex expected[12], actual[12];
    memset(expected, 0x63, sizeof(expected)); memset(actual, 0x63, sizeof(actual));
    int expectedCount = reference_clip(vertices, vertices + 1, vertices + 2, expected);
    int actualCount = ps2_clip_triangle(vertices, vertices + 1, vertices + 2, actual);
    if (expectedCount != actualCount || memcmp(expected, actual, sizeof(actual))) {
        static unsigned differences;
        if (differences++ < 4) printf("[PS2 CLIP DIFFERENCE] fixture=%u counts=%d,%d\n",
            fixture, expectedCount, actualCount);
        return 1;
    }
    /* Inputs may alias the destination: both implementations snapshot the
     * three original vertices before writing the resulting polygon. */
    memcpy(actual, vertices, 3 * sizeof(*vertices));
    actualCount = ps2_clip_triangle(actual, actual + 1, actual + 2, actual);
    if (expectedCount != actualCount || memcmp(expected, actual, actualCount * sizeof(*actual))) return 1;
    return 0;
}

static u32 clip_benchmark(const RSPVertex fixtures[][3], bool original) {
    RSPVertex output[12];
    u32 checksum = 0;
    for (unsigned repeat = 0; repeat < 128; repeat++) {
        for (unsigned i = 0; i < 64; i++) {
            int count = original ? reference_clip(&fixtures[i][0], &fixtures[i][1], &fixtures[i][2], output) :
                ps2_clip_triangle(&fixtures[i][0], &fixtures[i][1], &fixtures[i][2], output);
            checksum += count;
            if (count) checksum += output[0].cr + output[count - 1].tc_s;
        }
    }
    return checksum;
}

#ifdef PS2_RENDERER_SMOKE_CAPTURE
static u8 rawVertices[64][16] __attribute__((aligned(128)));
static GSPRIMSTQPOINT expectedPoints[600];

static void store_half(u8* out, s16 value) {
    out[0] = (u16)value; out[1] = (u16)value >> 8;
}

static void raw_vertex(unsigned index, s16 x, s16 y, s16 z) {
    u8* raw = rawVertices[index];
    store_half(raw, x); store_half(raw + 2, y); store_half(raw + 4, z);
    store_half(raw + 6, index * 131); store_half(raw + 8, index * 353 - 1900);
    store_half(raw + 10, 2100 - index * 197);
    raw[12] = index * 37 + 7; raw[13] = index * 71 + 19;
    raw[14] = index * 113 + 53; raw[15] = index * 29 + 89;
}

static RSPState fixture_state(unsigned view, unsigned material, bool textured) {
    RSPState rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.mvp[0] = rsp.mvp[5] = rsp.mvp[10] = 1.0f / 64.0f;
    rsp.mvp[15] = 1;
    rsp.modelview[0] = rsp.modelview[5] = rsp.modelview[10] = rsp.modelview[15] = 1;
    rsp.projection[0] = rsp.projection[5] = rsp.projection[10] = rsp.projection[15] = 1;
    rsp.vpScaleX = 80; rsp.vpScaleY = -64; rsp.vpTransX = 100; rsp.vpTransY = 100;
    if (view == 1) { rsp.mvp[3] = 1.0f / 256.0f; rsp.mvp[11] = 1.0f / 128.0f; }
    if (view == 2) { rsp.mvp[14] = 0.9f; rsp.vpScaleY = 64; }
    if (view == 3) { rsp.mvp[15] = -1; rsp.mvp[3] = 1.0f / 64.0f; }
    if (view == 4) { rsp.mvp[15] = 0.0001f; rsp.mvp[11] = 1.0f / 64.0f; }
    if (view == 5) rsp.mvp[15] = -0.0001f;
    if (view == 6) { rsp.mvp[15] = 0; rsp.mvp[3] = 1.0f / 64.0f; }
    if (view == 7) { rsp.mvp[12] = -0.75f; rsp.mvp[13] = 0.6f; rsp.vpTransX = -7.25f; }
    rsp.geometryMode = material == 1 ? 0x10000u : material == 2 ? 0x400u :
        material == 3 ? 0x200u : material == 4 ? 0x60000u : material == 5 ? 0xe0000u : 0;
    rsp.texEnabled = textured; rsp.texScaleS = 0.5f; rsp.texScaleT = 0.75f;
    rsp.fogMul = 100; rsp.fogOff = 120;
    rsp.numLights = 1;
    rsp.lights[0].r = 81; rsp.lights[0].g = 143; rsp.lights[0].b = 59;
    rsp.lights[0].dx = 0.6f; rsp.lights[0].dy = -0.8f;
    rsp.lights[1].r = 11; rsp.lights[1].g = 29; rsp.lights[1].b = 47;
    rsp.lookAt[0][0] = 1; rsp.lookAt[1][1] = 1;
    return rsp;
}

static void draw_fixtures(RSPState* rsp, RDPState* rdp, bool batched) {
    if (batched) f3dex2_batch_begin(rsp, rdp);
    f3dex2_tri2(0x06000204, 0x0006080a, rsp, rdp);
    f3dex2_tri2(0x060c0e10, 0x00121416, rsp, rdp);
    f3dex2_tri1(0x05000402, 0, rsp, rdp); /* Opposite winding. */
    f3dex2_tri1(0x05000004, 0, rsp, rdp); /* Repeated index/zero area. */
    if (batched) f3dex2_batch_end(rdp);
}

static void primitive_stats(const RDPState* rdp, u32* stats) {
    stats[0] = rdp->stats.triangles; stats[1] = rdp->stats.clipped;
    stats[2] = rdp->stats.culled; stats[3] = rdp->stats.invalid;
    stats[4] = rdp->stats.textureFailed; stats[5] = rdp->stats.submitted;
}

static unsigned compare_points(unsigned expectedCount) {
    unsigned actualCount;
    const GSPRIMSTQPOINT* points = f3dex2_test_points(&actualCount);
    return expectedCount != actualCount || memcmp(expectedPoints, points, actualCount * sizeof(*points));
}

static unsigned primitive_checks(void) {
    static RSPState expectedVertices;
    unsigned failures = 0, cases = 0;
    const s16 positions[12][3] = {
        {-32,-32,0},{32,-32,16},{0,32,-16},
        {160,-32,0},{16,-16,0},{0,16,0},
        {-256,-32,0},{-192,-16,0},{-128,16,0},
        {0,0,0},{32,0,64},{0,32,-64}
    };
    for (unsigned i = 0; i < 12; i++) raw_vertex(i, positions[i][0], positions[i][1], positions[i][2]);
    static u8 texels[16] __attribute__((aligned(128))) = {
        0,32,64,128,255,192,160,96,16,48,80,112,144,176,208,240
    };
    RDPState* rdp = &g_rdpState;
    rdp_setothermode(0, 0x30u, rdp);
    rdp_setcombine(0xfc121800u, 0xf003fe00u, rdp);
    TileDescriptor* tile = &rdp->tile[0];
    memset(tile, 0, sizeof(*tile));
    tile->width = tile->height = 4; tile->fmt = G_IM_FMT_I; tile->siz = G_IM_SIZ_8b;
    tile->tmem = 511; tile->cms = tile->cmt = 2;
    rdp->textureLoads[511].source = texels; rdp->textureLoads[511].stride = 4;
    ps2_texture_upload(tile, rdp);
    if (!tile->gsTextureLoaded) {
        printf("[PS2 PROJECTION TEST] FAIL: fixture texture upload\n");
        return 1;
    }
    for (unsigned textured = 0; textured < 2; textured++) {
        for (unsigned view = 0; view < 8; view++) {
            for (unsigned material = 0; material < 6; material++) {
                rdp_setothermode(material == 1 ? 1u << 20 : 0,
                    material == 1 ? 0xc8000030u : view == 7 ? 0xc10u : 0x30u, rdp);
                for (unsigned batched = 0; batched < 2; batched++) {
                    RSPState initial = fixture_state(view, material, textured);
                    g_rspState = initial;
                    memset(&rdp->stats, 0, sizeof(rdp->stats));
                    f3dex2_test_geometry_reference(true);
                    f3dex2_vtx(0x0100c018, (u32)rawVertices, &g_rspState);
                    expectedVertices = g_rspState;
                    f3dex2_test_capture(true);
                    draw_fixtures(&g_rspState, rdp, batched);
                    unsigned expectedCount;
                    const GSPRIMSTQPOINT* points = f3dex2_test_points(&expectedCount);
                    memcpy(expectedPoints, points, expectedCount * sizeof(*points));
                    u32 expectedStats[6], actualStats[6];
                    primitive_stats(rdp, expectedStats);
                    if (!textured && !view && !material && (!expectedCount || !expectedStats[1])) failures++;
                    g_rspState = initial;
                    memset(&rdp->stats, 0, sizeof(rdp->stats));
                    f3dex2_test_geometry_reference(false);
                    f3dex2_vtx(0x0100c018, (u32)rawVertices, &g_rspState);
                    for (unsigned i = 0; i < 12; i++) {
                        const RSPVertex* a = &expectedVertices.vtx[i];
                        const RSPVertex* b = &g_rspState.vtx[i];
                        /* Object/clip coordinates, generated ST, lit RGBA and
                         * flags all remain exact, even for rejected vertices. */
                        if (memcmp(a, b, offsetof(RSPVertex, sx)) || a->clipFlags != b->clipFlags || a->valid != b->valid ||
                            ((!b->clipFlags || !(b->cw > 0.0001f || b->cw < -0.0001f)) &&
                             memcmp(&a->sx, &b->sx, 3 * sizeof(float)))) failures++;
                    }
                    f3dex2_test_capture(true);
                    draw_fixtures(&g_rspState, rdp, batched);
                    primitive_stats(rdp, actualStats);
                    if (compare_points(expectedCount) || memcmp(expectedStats, actualStats, sizeof(actualStats))) {
                        if (failures < 4) printf("[PS2 PRIMITIVE DIFFERENCE] tex=%u view=%u material=%u batch=%u\n",
                            textured, view, material, batched);
                        failures++;
                    }
                    if (material == 1 && expectedCount) {
                        points = f3dex2_test_points(&expectedCount);
                        for (unsigned i = 0; i < expectedCount; i++) if (points[i].xyz2.tag != GS_XYZF2) failures++;
                    }
                    cases++;
                }
            }
        }
    }
    f3dex2_test_capture(false);
    printf("[PS2 PROJECTION PRIMITIVE TEST] cases=%u complete GS points, lighting/texgen, cull, near W, fog, decal failures=%u\n",
        cases, failures);
    return failures;
}

static unsigned culldl_checks(void) {
    unsigned failures = 0;
    /* DL targets must be in segment-zero EE RAM, rather than the top-of-RAM
     * stack where a native address has the segmented-address high byte 1. */
    static u32 child[4][2] __attribute__((aligned(8))) = {
        {0x0100600c,0},{0x03000000,4},{0x0506080a,0},{0xdf000000,0}
    };
    static u32 parent[3][2] __attribute__((aligned(8))) = {
        {0xde000000,0},{0x0506080a,0},{0xdf000000,0}
    };
    child[0][1] = (u32)rawVertices; parent[0][1] = (u32)child;
    rdp_setothermode(0, 0x30u, &g_rdpState);
    for (unsigned outside = 0; outside < 2; outside++) {
        raw_vertex(0, outside ? -256 : -32, -32, 0);
        raw_vertex(1, outside ? -192 : 32, -32, 0);
        raw_vertex(2, outside ? -128 : 0, 32, 0);
        raw_vertex(3,-32,-32,0); raw_vertex(4,32,-32,0); raw_vertex(5,0,32,0);
        for (unsigned nested = 0; nested < 2; nested++) {
            unsigned expectedCount = 0;
            u32 expectedStats[6], actualStats[6];
            for (unsigned original = 1; ; original--) {
                g_rspState = fixture_state(0, 0, false);
                memset(&g_rdpState.stats, 0, sizeof(g_rdpState.stats));
                f3dex2_test_geometry_reference(original);
                f3dex2_test_capture(true);
                rdp_to_gs_exec((const Gfx*)(nested ? parent : child));
                unsigned count;
                const GSPRIMSTQPOINT* points = f3dex2_test_points(&count);
                unsigned wanted = nested ? (outside ? 3 : 6) : (outside ? 0 : 3);
                if (count != wanted) failures++;
                if (original) {
                    expectedCount = count; memcpy(expectedPoints, points, count * sizeof(*points));
                    primitive_stats(&g_rdpState, expectedStats);
                } else {
                    primitive_stats(&g_rdpState, actualStats);
                    if (compare_points(expectedCount) || memcmp(expectedStats, actualStats, sizeof(actualStats))) failures++;
                    break;
                }
            }
        }
    }
    f3dex2_test_capture(false);
    printf("[PS2 DEFERRED CULLDL TEST] rejected/visible root and nested DLs failures=%u\n", failures);
    return failures;
}

static unsigned projection_benchmark(void) {
    unsigned failures = 0;
    for (unsigned visible = 0; visible < 2; visible++) {
        for (unsigned i = 0; i < 64; i++) raw_vertex(i,
            !visible && (i & 3) ? 160 + i : (int)i - 32, (int)i / 2 - 16, (int)i - 32);
        RSPState rsp = fixture_state(0, 0, false);
        u32 cycles[2], checksum[2];
        for (unsigned original = 0; original < 2; original++) {
            f3dex2_test_geometry_reference(original);
            u32 total = 0, start = ps2_gfx_cycles();
            for (unsigned repeat = 0; repeat < 512; repeat++) {
                f3dex2_vtx(0x01040080, (u32)rawVertices, &rsp);
                total += rsp.vtx[repeat & 63].cr + rsp.vtx[(repeat + 1) & 63].clipFlags;
            }
            cycles[original] = ps2_gfx_cycles() - start; checksum[original] = total;
        }
        if (checksum[0] != checksum[1]) failures++;
        printf("[PS2 VIEWPORT BENCH] visible=%u originalCycles=%u deferredCycles=%u checksum=%08x failures=%u\n",
            visible, cycles[1], cycles[0], checksum[0], failures);
    }
    f3dex2_test_geometry_reference(false);
    return failures;
}
#endif

int ps2_clip_projection_smoke(void) {
    unsigned failures = 0, checks = 0;
    u32 random = 0x618bd730u;
    static RSPVertex fixtures[64][3];
    RSPVertex vertices[3];
    for (unsigned i = 0; i < 20000; i++) {
        for (unsigned j = 0; j < 3; j++) random_vertex(&vertices[j], &random);
        if (i < 64) memcpy(fixtures[i], vertices, sizeof(vertices));
        failures += compare_clip(vertices, checks++);
    }
    /* Test exact boundaries, one-ULP crossings, signed zero, degenerate
     * triangles and the W values at which projection marks every plane. */
    const float edges[] = {-1.00000011920928955078125f,-1,-0.0001f,-0.0f,0,0.0001f,1,1.00000011920928955078125f};
    const float widths[] = {-1,-0.00010001f,-0.0001f,-0.00009999f,0,0.00009999f,0.0001f,0.00010001f,1};
    for (unsigned plane = 0; plane < 6; plane++) {
        for (unsigned edge = 0; edge < sizeof(edges) / sizeof(edges[0]); edge++) {
            for (unsigned width = 0; width < sizeof(widths) / sizeof(widths[0]); width++) {
                for (unsigned j = 0; j < 3; j++) {
                    random_vertex(&vertices[j], &random);
                    vertices[j].cx = j == 0 ? 0 : j == 1 ? 0.5f : -0.5f;
                    vertices[j].cy = j == 0 ? -0.5f : 0.5f; vertices[j].cz = 0;
                    vertices[j].cw = widths[width];
                }
                if (plane < 2) vertices[0].cx = edges[edge];
                else if (plane < 4) vertices[0].cy = edges[edge];
                else vertices[0].cz = edges[edge];
                if (edge == 3) vertices[1] = vertices[2] = vertices[0];
                failures += compare_clip(vertices, checks++);
            }
        }
    }
    printf("[PS2 HOMOGENEOUS CLIP TEST] cases=%u exact vertex bytes/order, edges and aliasing failures=%u\n", checks, failures);
    u32 start = ps2_gfx_cycles();
    u32 originalChecksum = clip_benchmark(fixtures, true);
    u32 originalCycles = ps2_gfx_cycles() - start;
    start = ps2_gfx_cycles();
    u32 cachedChecksum = clip_benchmark(fixtures, false);
    u32 cachedCycles = ps2_gfx_cycles() - start;
    if (originalChecksum != cachedChecksum) failures++;
    printf("[PS2 CLIP BENCH] originalCycles=%u cachedCycles=%u checksum=%08x failures=%u\n",
        originalCycles, cachedCycles, cachedChecksum, failures);
#ifdef PS2_RENDERER_SMOKE_CAPTURE
    static RDPState savedRdp;
    static RSPState savedRsp;
    savedRdp = g_rdpState; savedRsp = g_rspState;
    bool oldProfile = gPS2GfxProfileActive;
    gPS2GfxProfileActive = false;
    failures += primitive_checks();
    failures += culldl_checks();
    gPS2GfxProfileActive = false;
    failures += projection_benchmark();
    f3dex2_test_geometry_reference(false);
    f3dex2_test_capture(false);
    g_rdpState = savedRdp; g_rspState = savedRsp;
    ps2_gs_state_reset();
    rdp_apply_othermode(&g_rdpState);
    gPS2GfxProfileActive = oldProfile;
#endif
    return failures;
}
