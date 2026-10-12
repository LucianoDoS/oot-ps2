/* SPDX-License-Identifier: GPL-2.0-or-later
 * EE differential oracle: a VTX batch must retain every output byte when
 * the non-instrumented loop omits repeated timer and texgen checks. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "f3dex2_ps2.h"
#include "ps2_gfx_profile.h"
#include "ps2_lighting.h"
#include "ps2_segment.h"

#ifndef PS2_SMOKE_VU0
#define PS2_SMOKE_VU0 0
#endif

static void write_half(u8* destination, u16 value) {
#if OOT_ASSETS_BIG_ENDIAN
    destination[0] = value >> 8; destination[1] = value;
#else
    memcpy(destination, &value, sizeof(value));
#endif
}

static void make_source(u8* source) {
    for (unsigned i = 0; i < 64; i++, source += 16) {
        write_half(source, (u16)(i * 1073u - 32768u));
        write_half(source + 2, (u16)(i * 2003u + 173u));
        write_half(source + 4, (u16)(i * 479u - 16000u));
        write_half(source + 6, i * 997u);
        write_half(source + 8, (u16)(i * 1301u - 32000u));
        write_half(source + 10, (u16)(i * 1511u + 30001u));
        source[12] = i * 73u; source[13] = i * 157u;
        source[14] = i * 239u; source[15] = i * 31u;
    }
}

static void set_float_bits(float* destination, u32 bits) {
    memcpy(destination, &bits, sizeof(bits));
}

static void make_state(RSPState* rsp, unsigned mode, unsigned matrix) {
    memset(rsp, 0, sizeof(*rsp));
    memset(rsp->vtx, 0xa5, sizeof(rsp->vtx));
    static const u32 modes[] = {0, 0x00020000u, 0x00060000u, 0x000e0000u};
    rsp->geometryMode = modes[mode];
    rsp->modelview[0] = 1; rsp->modelview[1] = 0.25f;
    rsp->modelview[4] = -0.25f; rsp->modelview[5] = 1;
    rsp->modelview[10] = rsp->modelview[15] = 1;
    rsp->lookAt[0][0] = rsp->lookAt[1][1] = 1;
    rsp->mvp[0] = 0.000025f; rsp->mvp[5] = -0.000035f;
    rsp->mvp[10] = 0.0000125f; rsp->mvp[12] = 0.125f;
    static const float ws[] = {1, -1, 0, 0.00009f, 0.00011f, INFINITY};
    rsp->mvp[15] = matrix < 6 ? ws[matrix] : 1;
    if (matrix == 6) {
        /* Rotation and perspective terms populate all four vector lanes. */
        static const float rotated[] = {
             0.0007071067f, 0.0001035533f, -0.0006830127f, -0.0007812345f,
             0.0003535533f, 0.0008535533f,  0.0005129876f,  0.0002634981f,
             0.0006123724f,-0.0006123724f,  0.0001339746f,  0.0003779644f,
             0.3125f, -0.1875f, 0.4375f, 1.625f
        };
        memcpy(rsp->mvp, rotated, sizeof(rotated));
    } else if (matrix == 7) {
        static const float fractions[] = {
             0.0035714285f, -0.0003333333f, 0.0029166666f,  0.0000909090f,
            -0.0015384615f,  0.0046153845f, 0.0007692307f, -0.0001111111f,
             0.0021052631f, -0.0036842104f, 0.0052631577f,  0.0001428571f,
             0.1428571428f, -0.1666666666f, 0.3333333333f,  2.3333333333f
        };
        memcpy(rsp->mvp, fractions, sizeof(fractions));
    } else if (matrix == 8) {
        /* Signed zero must survive the same sequence of independent sums. */
        for (unsigned i = 0; i < 16; i++) set_float_bits(&rsp->mvp[i], i & 1 ? 0x80000000u : 0);
        rsp->mvp[15] = 1;
    } else if (matrix == 9) set_float_bits(&rsp->mvp[3], 0x7fc12345u); /* NaN payload */
    else if (matrix == 10) set_float_bits(&rsp->mvp[0], 0xff800000u); /* -Infinity */
    else if (matrix == 11) set_float_bits(&rsp->mvp[5], 1); /* positive subnormal */
    else if (matrix == 12) set_float_bits(&rsp->mvp[14], 0x80000001u); /* negative subnormal */
    else if (matrix == 13) {
        rsp->mvp[0] = 1.0e15f; rsp->mvp[4] = -1.0e15f;
        rsp->mvp[8] = 7.125e14f; rsp->mvp[12] = -9.375e14f;
    } else if (matrix == 14) set_float_bits(&rsp->mvp[15], 0x7f7fffffu); /* largest IEEE normal */
    else if (matrix == 15) {
        rsp->mvp[0] = 63.999996f; rsp->mvp[4] = -64;
        rsp->mvp[8] = 0.000001f; rsp->mvp[12] = 0.0000007f;
        rsp->mvp[1] = -0.000025f; rsp->mvp[5] = 0.000025f;
        rsp->mvp[9] = -0.000025f; rsp->mvp[13] = -0.125f;
    }
    rsp->vpScaleX = 160; rsp->vpScaleY = -120;
    rsp->vpTransX = 160; rsp->vpTransY = 120;
    rsp->numLights = matrix % 3;
    rsp->lights[0] = (RSPLight){199, 71, 103, 1, 0, 0};
    rsp->lights[1] = (RSPLight){73, 183, 101, 0, 1, 0};
    rsp->lights[2] = (RSPLight){17, 31, 59, 0, 0, 0};
}

#if PS2_SMOKE_VU0
/* Diagnostic statistics never change the exact byte oracle's criterion.
 * The failed 2026-10-12 run remains evidence: 208/512 failed cases. */
typedef struct {
    u32 compared[7], changed[7], maxUlp[7];
    float maxAbsolute[7];
    u32 clipChanged, validChanged, otherChanged, fallbackFailed;
} VU0DifferenceStats;

static u32 ordered_float_bits(u32 bits) {
    return bits & 0x80000000u ? ~bits : bits ^ 0x80000000u;
}

static void vu0_vertex_differences(VU0DifferenceStats* stats, const RSPVertex* before,
                                 const RSPVertex* after, bool fallback) {
    for (unsigned field = 0; field < 7; field++) {
        size_t offset = offsetof(RSPVertex, cx) + field * sizeof(float);
        u32 a, b;
        memcpy(&a, (const u8*)before + offset, sizeof(a));
        memcpy(&b, (const u8*)after + offset, sizeof(b));
        stats->compared[field]++;
        if (a == b) continue;
        stats->changed[field]++;
        u32 oa = ordered_float_bits(a), ob = ordered_float_bits(b);
        u32 distance = oa > ob ? oa - ob : ob - oa;
        if (distance > stats->maxUlp[field]) stats->maxUlp[field] = distance;
        /* Special exponents are reported through raw bits/ULP only. Their
         * meanings differ on EE; don't turn them into an IEEE abs estimate. */
        if ((a & 0x7f800000u) != 0x7f800000u && (b & 0x7f800000u) != 0x7f800000u) {
            float fa, fb;
            memcpy(&fa, &a, sizeof(fa)); memcpy(&fb, &b, sizeof(fb));
            float absolute = fabsf(fa - fb);
            if (absolute > stats->maxAbsolute[field]) stats->maxAbsolute[field] = absolute;
        }
    }
    stats->clipChanged += before->clipFlags != after->clipFlags;
    stats->validChanged += before->valid != after->valid;
    stats->otherChanged += memcmp(before, after, offsetof(RSPVertex, cx)) != 0;
    size_t end = offsetof(RSPVertex, valid) + sizeof(before->valid);
    stats->otherChanged += memcmp((const u8*)before + end, (const u8*)after + end, sizeof(*before) - end) != 0;
    stats->fallbackFailed += fallback && memcmp(before, after, sizeof(*before)) != 0;
}

static unsigned vu0_checks(u8 source[1025]) {
    static RSPState original, candidate, fixture;
    static const unsigned counts[] = {1, 17, 32, 64};
    unsigned checks = 0, failures = 0, vectorBatches = 0, fallbackBatches = 0;
    VU0DifferenceStats differences = {0};
    for (unsigned alignment = 0; alignment < 2; alignment++) {
        make_source(source + alignment);
        for (unsigned mode = 0; mode < 4; mode++) for (unsigned matrix = 0; matrix < 16; matrix++) {
            make_state(&fixture, mode, matrix);
            for (unsigned size = 0; size < 4; size++) {
                unsigned count = counts[size], first = 64 - count;
                u32 command = 0x01000000u | (count << 12) | ((first + count) << 1);
                original = candidate = fixture;
                ps2_light_test_normal_cache(false);
                f3dex2_test_vertex_batch_reference(true);
                f3dex2_test_vu0_enabled(false);
                f3dex2_vtx(command, (u32)(uintptr_t)(source + alignment), &original);
                ps2_light_test_normal_cache(false);
                f3dex2_test_vertex_batch_reference(false);
                f3dex2_test_vu0_stats(true);
                f3dex2_test_vu0_enabled(true);
                f3dex2_vtx(command, (u32)(uintptr_t)(source + alignment), &candidate);
                F3DEX2VertexVU0TestStats stats = f3dex2_test_vu0_stats(false);
                bool fallback = matrix == 5 || (matrix >= 9 && matrix <= 12);
                failures += stats.vectorBatches != !fallback || stats.fallbackBatches != fallback ||
                            stats.vectorVertices != (fallback ? 0 : count);
                vectorBatches += stats.vectorBatches; fallbackBatches += stats.fallbackBatches;
                for (unsigned i = first; i < first + count; i++)
                    vu0_vertex_differences(&differences, &original.vtx[i], &candidate.vtx[i], fallback);
                if (memcmp(&original, &candidate, sizeof(original))) {
                    if (failures < 4) {
                        unsigned offset;
                        const u8* before = (const u8*)&original;
                        const u8* after = (const u8*)&candidate;
                        for (offset = 0; offset < sizeof(original); offset++) if (before[offset] != after[offset]) break;
                        u32 oldBits, newBits;
                        offset &= ~3u;
                        memcpy(&oldBits, before + offset, 4); memcpy(&newBits, after + offset, 4);
                        printf("[PS2 VTX VU0 MISMATCH] align=%u mode=%u matrix=%u count=%u offset=%u scalar=%08x vector=%08x\n",
                            alignment, mode, matrix, count, offset, oldBits, newBits);
                    }
                    failures++;
                }
                checks++;
            }
        }
    }
    f3dex2_test_vu0_enabled(false);
    f3dex2_test_vertex_batch_reference(false);
    ps2_light_test_normal_cache(true);
    printf("[PS2 VTX VU0 TEST] cases=%u vectorBatches=%u fallbackBatches=%u exact state bytes failures=%u\n",
        checks, vectorBatches, fallbackBatches, failures);
    static const char* names[] = {"cx", "cy", "cz", "cw", "sx", "sy", "sz"};
    for (unsigned i = 0; i < 7; i++)
        printf("[PS2 VTX VU0 FIELD] field=%s compared=%u changed=%u maxULP=%u maxAbs=%.9g\n",
            names[i], differences.compared[i], differences.changed[i], differences.maxUlp[i], differences.maxAbsolute[i]);
    printf("[PS2 VTX VU0 FLAGS] clipChanged=%u validChanged=%u otherChanged=%u fallbackFailed=%u\n",
        differences.clipChanged, differences.validChanged, differences.otherChanged, differences.fallbackFailed);
    return failures;
}
#endif

int ps2_vertex_batch_smoke(void) {
    static u8 source[1025] __attribute__((aligned(16)));
    static RSPState original, candidate, fixture;
    static const unsigned counts[] = {1, 17, 32, 64};
    unsigned checks = 0, failures = 0;
    bool previousProfile = gPS2GfxProfileActive;
    gPS2GfxProfileActive = false;
    f3dex2_test_vu0_enabled(false);
    for (unsigned alignment = 0; alignment < 2; alignment++) {
        make_source(source + alignment);
        for (unsigned mode = 0; mode < 4; mode++) for (unsigned matrix = 0; matrix < 6; matrix++) {
            make_state(&fixture, mode, matrix);
            for (unsigned size = 0; size < 4; size++) {
                unsigned count = counts[size], first = 64 - count;
                u32 command = 0x01000000u | (count << 12) | ((first + count) << 1);
                original = candidate = fixture;
                ps2_light_test_normal_cache(false);
                f3dex2_test_vertex_batch_reference(true);
                f3dex2_vtx(command, (u32)(uintptr_t)(source + alignment), &original);
                ps2_light_test_normal_cache(false);
                f3dex2_test_vertex_batch_reference(false);
                f3dex2_vtx(command, (u32)(uintptr_t)(source + alignment), &candidate);
                failures += memcmp(&original, &candidate, sizeof(original)) != 0;
                checks++;
            }
        }
    }
    f3dex2_test_vertex_batch_reference(false);
    ps2_light_test_normal_cache(true);
    printf("[PS2 VTX BATCH TEST] cases=%u exact state bytes, aligned/unaligned, texgen and W edges failures=%u\n",
        checks, failures);
#if PS2_SMOKE_VU0
    failures += vu0_checks(source);
#else
    printf("[PS2 VTX VU0 TEST] SKIP: experimental oracle requires PS2_SMOKE_VU0=1; production transform remains scalar\n");
#endif
    gPS2GfxProfileActive = previousProfile;
    return failures;
}
