/* Compare native lighting reuse against the original per-vertex arithmetic. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "rdp_to_gs.h"
#include "ps2_lighting.h"

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

static void initialize_vertex(RSPVertex* vertex, u32 normal, unsigned alpha) {
    memset(vertex, 0xa5, sizeof(*vertex));
    vertex->cr = normal;
    vertex->cg = normal >> 8;
    vertex->cb = normal >> 16;
    vertex->ca = alpha;
    /* bool has only zero/one representations. A sentinel in this field
     * can be canonicalized differently by struct-return/assignment copies. */
    vertex->valid = true;
}

static RSPVertex vertex_from_normal(u32 normal, unsigned alpha) {
    RSPVertex vertex;
    initialize_vertex(&vertex, normal, alpha);
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

static unsigned rgb_cache_collision_smoke(void) {
    static RSPState state;
    unsigned failures=0;
    u32 collision=1;
    while ((collision*0x9e3779b1u)>>(32-PS2_LIGHT_RGB_CACHE_BITS)) collision++;
    const u32 normals[]={0,0,collision,collision,0,1,1,collision};
    ps2_light_test_normal_cache(false);
    memset(&state,0,sizeof(state));
    state.geometryMode=0x00020000u; state.numLights=1;
    state.lights[0]=(RSPLight){173,131,97,1.0f,0.25f,-0.5f};
    state.lights[1]=(RSPLight){13,27,31,0,0,0};
    ps2_light_prepare_batch(&state);
    for (unsigned i=0;i<sizeof(normals)/sizeof(normals[0]);i++) {
        RSPVertex expected, actual;
        initialize_vertex(&expected,normals[i],i*31);
        memcpy(&actual,&expected,sizeof(actual));
        lighting_reference(&expected,&state);
        ps2_light_apply_prepared_vertex(&actual,&state);
        failures+=memcmp(&expected,&actual,sizeof(actual))!=0;
    }
    PS2LightingCacheStats stats=ps2_light_cache_stats(false);
    failures+=stats.rgbHits!=3 || stats.rgbBuilds!=5 || stats.tableChanges!=1;
    failures+=stats.normalHits || stats.normalBuilds;
    failures+=stats.rgbEntries!=(1u<<PS2_LIGHT_RGB_CACHE_BITS) ||
        stats.rgbStorageBytes!=12u*(1u<<PS2_LIGHT_RGB_CACHE_BITS);
    printf("[PS2 RGB CACHE COLLISION TEST] bits=%u entries=%u bytes=%u collision=%06x highSlot=%u hits=%u builds=%u whole vertex/alpha and replacement failures=%u\n",
        (unsigned)PS2_LIGHT_RGB_CACHE_BITS,stats.rgbEntries,stats.rgbStorageBytes,collision,
        (1u*0x9e3779b1u)>>(32-PS2_LIGHT_RGB_CACHE_BITS),stats.rgbHits,stats.rgbBuilds,failures);
    ps2_light_test_normal_cache(true);
    return failures;
}

/* Replay the existing RGB cache with and without an independent normal
 * cache. Every light-table change discards RGB; normalized source vectors
 * must remain exact, including deliberate direct-map key collisions. */
static unsigned normal_cache_smoke(void) {
    static RSPState state;
    static RSPVertex reference[64];
    const u32 extremes[] = {
        0,0x7f,0x80,0xff,0x7f00,0x8000,0xff00,0x7f0000,0x800000,0xff0000,
        0x7f7f7f,0x808080,0xffffff,0x807f00,0x7f0080,0x00807f,0x010101,
        0x80ff7f,0xff7f80,0x7f80ff
    };
    unsigned failures=0, cases=0;
    u32 cycles[2], checksum[2]={0,0};
    PS2LightingCacheStats stats[2];
    /* Key zero occupies slot zero. Find a distinct full RGB24 key with
     * the same slot, then alternate them across table changes. */
    u32 collision=1;
    while ((collision*0x9e3779b1u)>>(32-PS2_LIGHT_RGB_CACHE_BITS)) collision++;
    /* A zero key in cleared BSS is not a cache hit. Then change only the
     * table: zero's exact cached vector must survive the RGB invalidation. */
    ps2_light_test_normal_cache(true);
    memset(&state,0,sizeof(state));
    state.geometryMode=0x00020000u; state.numLights=1;
    state.lights[0].dx=1.0f;
    state.lights[1].r=3; state.lights[1].g=7; state.lights[1].b=11;
    for (unsigned pass=0;pass<2;pass++) {
        state.lights[0].r=13+pass;
        ps2_light_prepare_batch(&state);
        RSPVertex expected, actual;
        initialize_vertex(&expected,0,0x81+pass);
        memcpy(&actual,&expected,sizeof(actual));
        lighting_reference(&expected,&state);
        ps2_light_apply_prepared_vertex(&actual,&state);
        failures+=memcmp(&expected,&actual,sizeof(actual))!=0;
        PS2LightingCacheStats zero=ps2_light_cache_stats(false);
        failures+=zero.normalBuilds!=1 || zero.normalHits!=pass;
        cases++;
    }
    for (unsigned path=0;path<2;path++) {
        ps2_light_test_normal_cache(path!=0);
        memset(&state,0,sizeof(state));
        state.geometryMode=0x00020000u;
        u32 start=count_cycles();
        for (unsigned batch=0;batch<256;batch++) {
            state.numLights=1+(batch%7);
            for (unsigned light=0;light<=state.numLights;light++) {
                state.lights[light].r=(batch*17+light*11)%256;
                state.lights[light].g=(batch*13+light*23)%256;
                state.lights[light].b=(batch*7+light*31)%256;
                state.lights[light].dx=((int)(batch%5)-2)*0.25f;
                state.lights[light].dy=((int)(light%5)-2)*0.25f;
                state.lights[light].dz=((int)((batch+light)%5)-2)*0.25f;
            }
            /* Lighting does not use these fields. Changing them must not
             * change the normal key, nor alter any matrix in the state. */
            state.modelview[0]=batch*0.03125f;
            state.mvp[15]=(int)batch-128;
            state.lookAt[0][2]=batch*0.015625f;
            RSPState before=state;
            ps2_light_prepare_batch(&state);
            for (unsigned i=0;i<64;i++) {
                u32 normal=i<4 ? (i&1 ? collision : 0) : extremes[(i-4)%20];
                RSPVertex input, expected, actual;
                initialize_vertex(&input,normal,batch+i*17);
                /* Preserve the same initialized padding as well as fields;
                 * C struct assignment need not copy its padding bytes. */
                memcpy(&expected,&input,sizeof(expected));
                memcpy(&actual,&input,sizeof(actual));
                lighting_reference(&expected,&state);
                ps2_light_apply_prepared_vertex(&actual,&state);
                if (memcmp(&expected,&actual,sizeof(actual))) {
                    if (!failures) {
                        const u8* e=(const u8*)&expected; const u8* a=(const u8*)&actual;
                        unsigned byte=0;
                        while (byte<sizeof(actual) && e[byte]==a[byte]) byte++;
                        printf("[PS2 NORMAL FIRST DIFF] path=%u batch=%u vertex=%u normal=%06x offset=%u expected=%02x actual=%02x rgba=%u,%u,%u,%u / %u,%u,%u,%u\n",
                            path,batch,i,normal,byte,e[byte],a[byte],
                            expected.cr,expected.cg,expected.cb,expected.ca,
                            actual.cr,actual.cg,actual.cb,actual.ca);
                    }
                    failures++;
                }
                if (batch==255) {
                    if (!path) memcpy(&reference[i],&actual,sizeof(actual));
                    else failures+=memcmp(&reference[i],&actual,sizeof(actual))!=0;
                }
                checksum[path]+=actual.cr+actual.cg+actual.cb+actual.ca;
                cases++;
            }
            failures+=memcmp(&before,&state,sizeof(state))!=0;
        }
        cycles[path]=count_cycles()-start;
        stats[path]=ps2_light_cache_stats(false);
        if (path) failures+=!stats[path].normalHits || !stats[path].normalBuilds;
        else failures+=stats[path].normalHits || stats[path].normalBuilds;
        failures+=!stats[path].rgbHits || !stats[path].rgbBuilds || stats[path].tableChanges!=256;
        /* These early-return branches must not touch either cache. */
        for (unsigned off=0;off<2;off++) {
            state.geometryMode=off ? 0 : 0x00020000u;
            state.numLights=off ? 7 : 0;
            PS2LightingCacheStats before=ps2_light_cache_stats(false);
            ps2_light_prepare_batch(&state);
            RSPVertex expected, actual;
            initialize_vertex(&expected,0x808080u,0xb7);
            memcpy(&actual,&expected,sizeof(actual));
            lighting_reference(&expected,&state);
            ps2_light_apply_prepared_vertex(&actual,&state);
            failures+=memcmp(&expected,&actual,sizeof(actual))!=0;
            PS2LightingCacheStats after=ps2_light_cache_stats(false);
            failures+=memcmp(&before,&after,sizeof(before))!=0;
            cases++;
        }
    }
    failures+=checksum[0]!=checksum[1] || stats[1].storageBytes!=4096;
    /* Both timed oracle paths include the independent scalar comparison;
     * frame timing remains the fixed-view benchmark's responsibility. */
    printf("[PS2 NORMAL CACHE TEST] cases=%u collision=%06x hits=%u builds=%u tableChanges=%u bytes=%u failures=%u\n",
        cases,collision,stats[1].normalHits,stats[1].normalBuilds,stats[1].tableChanges,
        stats[1].storageBytes,failures);
    printf("[PS2 NORMAL CACHE ORACLE] referenceCycles=%u reusedCycles=%u checksums=%u,%u zero/extremes, changing lights, collisions, alpha/matrices and off/ambient failures=%u\n",
        cycles[0],cycles[1],checksum[0],checksum[1],failures);
    ps2_light_test_normal_cache(true);
    return failures;
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
            /* More than the maximum configurable 1024 entries exercises
             * replacement/collisions without dropping the original cases. */
            for (unsigned i = 0; i < 1088; i++) {
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
    failures+=rgb_cache_collision_smoke();
    failures+=normal_cache_smoke();
    return failures;
}
