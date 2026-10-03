/* Compare native lighting reuse against the original per-vertex arithmetic. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "rdp_to_gs.h"

static __attribute__((noinline)) void lighting_reference(RSPVertex* v, const RSPState* rsp) {
    if (!(rsp->geometryMode & 0x00020000u)) return;
    u32 ambient = rsp->numLights;
    float r = rsp->lights[ambient].r, g = rsp->lights[ambient].g, b = rsp->lights[ambient].b;
    float nx = (s8)v->cr, ny = (s8)v->cg, nz = (s8)v->cb;
    float length = sqrtf(nx*nx + ny*ny + nz*nz);
    if (length > 0.0f) { nx /= length; ny /= length; nz /= length; }
    for (u32 i = 0; i < rsp->numLights; i++) {
        const RSPLight* light = &rsp->lights[i];
        float dot = nx*light->dx + ny*light->dy + nz*light->dz;
        if (dot > 0.0f) {
            r += (float)light->r*dot;
            g += (float)light->g*dot;
            b += (float)light->b*dot;
        }
    }
    v->cr = r > 255.0f ? 255 : (u8)r;
    v->cg = g > 255.0f ? 255 : (u8)g;
    v->cb = b > 255.0f ? 255 : (u8)b;
}

static u32 random_word(u32* state) {
    *state = *state*1664525u + 1013904223u;
    return *state;
}

static RSPVertex vertex_from_normal(u32 normal, unsigned alpha) {
    RSPVertex vertex;
    memset(&vertex, 0xa5, sizeof(vertex));
    vertex.cr = normal;
    vertex.cg = normal >> 8;
    vertex.cb = normal >> 16;
    vertex.ca = alpha;
    return vertex;
}

static unsigned lighting_compare(const RSPState* rsp, u32 normal, unsigned alpha) {
    unsigned failures = 0;
    RSPVertex input = vertex_from_normal(normal, alpha), expected = input, actual = input;
    lighting_reference(&expected, rsp);
    ps2_light_apply_vertex(&actual, rsp);
    if (memcmp(&expected, &actual, sizeof(actual))) failures++;
    actual = input;
    ps2_light_apply_prepared_vertex(&actual, rsp);
    if (memcmp(&expected, &actual, sizeof(actual))) failures++;
    /* Repeated normals share RGB, but never the different alpha/other fields. */
    expected = actual = vertex_from_normal(normal, alpha ^ 0x7c);
    lighting_reference(&expected, rsp);
    ps2_light_apply_prepared_vertex(&actual, rsp);
    if (memcmp(&expected, &actual, sizeof(actual))) failures++;
    return failures;
}

static u32 count_cycles(void) {
    u32 cycles;
    __asm__ volatile("mfc0 %0, $9" : "=r"(cycles) :: "memory");
    return cycles;
}

int ps2_lighting_smoke(void) {
    static RSPState rsp, alternate;
    unsigned failures = 0, checks = 0;
    u32 random = 0x65010ab7u;
    const u32 extremes[] = {
        0,0x7f,0x80,0xff,0x7f00,0x8000,0xff00,0x7f0000,0x800000,0xff0000,
        0x7f7f7f,0x808080,0xffffff,0x807f00,0x7f0080,0x00807f,0x010101,
        0x80ff7f,0xff7f80,0x7f80ff
    };
    memset(&rsp, 0, sizeof(rsp));
    rsp.geometryMode = 0x00020000u;
    for (unsigned table = 0; table < 16; table++) {
        for (unsigned i = 0; i < 8; i++) {
            u8 source[16] = {0};
            source[0] = table == 0 ? 255 : random_word(&random) % 96;
            source[1] = table == 0 ? 255 : random_word(&random) % 96;
            source[2] = table == 0 ? 255 : random_word(&random) % 96;
            source[8] = random_word(&random) >> 24;
            source[9] = random_word(&random) >> 24;
            source[10] = random_word(&random) >> 24;
            if (table == 1) source[8] = source[9] = source[10] = 0;
            ps2_light_load(i, source, &rsp);
        }
        for (unsigned lightCount = 0; lightCount < 8; lightCount++) {
            ps2_light_set_count(lightCount, &rsp);
            ps2_light_prepare_batch(&rsp);
            for (unsigned i = 0; i < sizeof(extremes)/sizeof(extremes[0]); i++) {
                failures += lighting_compare(&rsp, extremes[i], i*13);
                checks++;
            }
            /* More than 256 different keys exercises replacement/collisions. */
            for (unsigned i = 0; i < 1024; i++) {
                u32 normal = random_word(&random) & 0xffffffu;
                failures += lighting_compare(&rsp, normal, random_word(&random) >> 24);
                checks++;
            }
        }
    }
    /* Model direct G_MW_LIGHTCOL/state writes, light loads, count changes,
     * state copies, and transitions through unlit geometry. */
    memset(&rsp, 0, sizeof(rsp));
    rsp.geometryMode = 0x00020000u;
    rsp.numLights = 1;
    rsp.lights[0].r = 101; rsp.lights[0].g = 63; rsp.lights[0].b = 25;
    rsp.lights[0].dx = 1.0f;
    rsp.lights[1].r = 3; rsp.lights[1].g = 7; rsp.lights[1].b = 11;
    for (unsigned change = 0; change < 9; change++) {
        if (change == 1) rsp.lights[0].r = 41;
        if (change == 2) rsp.lights[1].b = 19;
        if (change == 3) rsp.lights[0].dx = -1.0f;
        if (change == 4) {
            const u8 light[16] = {55,33,17,0,0,0,0,0,127,0,0,0,0,0,0,0};
            ps2_light_load(0, light, &rsp);
        }
        if (change == 5) ps2_light_set_count(0, &rsp);
        if (change == 6) rsp.geometryMode = 0;
        if (change == 7) {
            rsp.geometryMode = 0x00020000u;
            rsp.numLights = 1;
        }
        if (change == 8) rsp.lights[0].g = 12;
        ps2_light_prepare_batch(&rsp);
        failures += lighting_compare(&rsp, 0x7f, 0x35 + change);
        checks++;
        alternate = rsp;
        ps2_light_prepare_batch(&alternate);
        failures += lighting_compare(&alternate, 0x7f, 0xb1 - change);
        checks++;
    }
    printf("[PS2 LIGHTING TEST] %s: %u cases, normals, clamp, 0-7 lights, direct table writes, collisions, unlit and alpha (%u)\n",
        failures ? "FAIL" : "PASS", checks, failures);

    /* Identical light/normal order; prepare is included once per 64 vertices. */
    static RSPVertex reference[64], prepared[64];
    u32 referenceChecksum = 0, preparedChecksum = 0;
    u32 start = count_cycles();
    for (unsigned batch = 0; batch < 256; batch++) {
        rsp.numLights = batch & 7;
        rsp.lights[0].g = batch & 63;
        for (unsigned i = 0; i < 64; i++) {
            u32 normal = extremes[i % 20];
            reference[i] = vertex_from_normal(normal, i + batch);
            lighting_reference(&reference[i], &rsp);
            referenceChecksum += reference[i].cr + reference[i].cg + reference[i].cb + reference[i].ca;
        }
    }
    u32 scalarCycles = count_cycles() - start;
    start = count_cycles();
    for (unsigned batch = 0; batch < 256; batch++) {
        rsp.numLights = batch & 7;
        rsp.lights[0].g = batch & 63;
        ps2_light_prepare_batch(&rsp);
        for (unsigned i = 0; i < 64; i++) {
            u32 normal = extremes[i % 20];
            prepared[i] = vertex_from_normal(normal, i + batch);
            ps2_light_apply_prepared_vertex(&prepared[i], &rsp);
            preparedChecksum += prepared[i].cr + prepared[i].cg + prepared[i].cb + prepared[i].ca;
        }
    }
    u32 cachedCycles = count_cycles() - start;
    if (referenceChecksum != preparedChecksum || memcmp(reference, prepared, sizeof(reference))) failures++;
    printf("[PS2 LIGHTING BENCH] scalarCycles=%u cachedCycles=%u checksums=%u,%u failures=%u\n",
        scalarCycles, cachedCycles, referenceChecksum, preparedChecksum, failures);
    return failures;
}
