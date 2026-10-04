#include <stdio.h>
#include <string.h>
#include "ps2_texture.h"
#include "ps2_gfx_profile.h"
extern unsigned ps2_combiner_test_folded_passes;
extern unsigned ps2_combiner_test_retained_passes;

#define CHECK(expr) do { if (!(expr)) { \
    printf("[PS2 COMBINER CHANNEL TEST] FAIL line=%u: %s\n", (unsigned)__LINE__, #expr); return -1; \
} } while (0)

static void encode(RDPState* material, unsigned type,
                   const u8 rgb0[4], const u8 alpha0[4],
                   const u8 rgb1[4], const u8 alpha1[4]) {
    memset(material, 0, sizeof(*material));
    material->otherMode.hi = type << 20;
    material->combiner.w0 = 0xfc000000u | ((u32)rgb0[0] << 20) | ((u32)rgb0[2] << 15) |
        ((u32)alpha0[0] << 12) | ((u32)alpha0[2] << 9) | ((u32)rgb1[0] << 5) | rgb1[2];
    material->combiner.w1 = ((u32)rgb0[1] << 28) | ((u32)rgb1[1] << 24) |
        ((u32)alpha1[0] << 21) | ((u32)alpha1[2] << 18) | ((u32)rgb0[3] << 15) |
        ((u32)alpha0[1] << 12) | ((u32)alpha0[3] << 9) | ((u32)rgb1[3] << 6) |
        ((u32)alpha1[1] << 3) | alpha1[3];
}

/* This oracle uses the general scalar evaluator, including its separate
 * RGB/alpha texel inputs and the existing dynamic-alpha texture convention. */
static int scalar_reference(const RDPState* material, u8 values[4][256]) {
    const u8 shade[4] = {255,255,255,255};
    for (unsigned value = 0; value < 256; value++) {
        u8 texel[4] = {value,value,value,255}, output[4];
        ps2_combiner_color(material, texel, shade, output);
        for (unsigned channel = 0; channel < 3; channel++) values[channel][value] = output[channel];
        texel[0] = texel[1] = texel[2] = 255;
        texel[3] = value;
        ps2_combiner_color(material, texel, shade, output);
        values[3][value] = ps2_combiner_dynamic_alpha(material) ? value*128u/255 : output[3]*128u/255;
    }
    return 0;
}

static int packed_matches(const RDPState* material, const u8 values[4][256]) {
    u32 packed[4][32];
    ps2_texture_test_combiner_rgba16(material, packed);
    for (unsigned channel = 0; channel < 3; channel++) for (unsigned value = 0; value < 32; value++) {
        unsigned expanded = (value << 3) | (value >> 2);
        CHECK(packed[channel][value] == (u32)values[channel][expanded] << (channel*8));
    }
    CHECK(packed[3][0] == (u32)values[3][0] << 24);
    CHECK(packed[3][1] == (u32)values[3][255] << 24);
    return 0;
}

int ps2_combiner_channel_smoke(void) {
    unsigned foldedBefore = ps2_combiner_test_folded_passes;
    unsigned retainedBefore = ps2_combiner_test_retained_passes;
    const u8 shadeRgb[4] = {1,15,4,7}, shadeAlpha[4] = {1,7,4,7};
    const u8 envAlpha[4] = {1,7,5,7}, dynamicAlpha[4] = {1,7,3,7};
    const u8 passRgb[4] = {0,15,4,7}, passAlpha[4] = {7,7,7,0};
    const u8 primRgb[4] = {7,7,7,3}, constantEnvAlpha[4] = {7,7,7,5};
    const u8 mixedRgb[4] = {3,5,1,5}, mixedAlpha[4] = {3,7,5,7};
    static RDPState materials[32];
    unsigned count = 0, checks = 0;
    memset(&materials[count++], 0, sizeof(materials[0]));
    encode(&materials[count++], 2, mixedRgb, mixedAlpha, passRgb, passAlpha);
    encode(&materials[count++], 0, shadeRgb, shadeAlpha, passRgb, passAlpha);
    encode(&materials[count++], 0, shadeRgb, dynamicAlpha, passRgb, passAlpha);
    encode(&materials[count++], 0, primRgb, constantEnvAlpha, passRgb, passAlpha);
    encode(&materials[count++], 1, mixedRgb, envAlpha, passRgb, passAlpha);
    encode(&materials[count++], 3, mixedRgb, mixedAlpha, passRgb, passAlpha);
    /* C selectors 7..12 cover previous alpha, texel alpha, primitive alpha,
     * shade alpha and environment alpha, in both cycles. */
    for (unsigned selector = 7; selector <= 12; selector++) {
        u8 selected[4] = {1,15,selector,7};
        encode(&materials[count++], 0, selected, mixedAlpha, passRgb, passAlpha);
        encode(&materials[count++], 1, mixedRgb, mixedAlpha, selected, passAlpha);
    }
    const u8 combinedRgb[4] = {0,15,3,7}, zeroAlphaFactor[4] = {3,7,0,5};
    encode(&materials[count++], 1, mixedRgb, mixedAlpha, combinedRgb, zeroAlphaFactor);
    /* First-cycle products can be quadratic, negative, or cross zero.
     * Compare every texel value and partial mask with the scalar evaluator. */
    const u8 quadraticRgb[4]={1,5,1,3}, quadraticAlpha[4]={1,5,1,3};
    const u8 negativeRgb[4]={5,1,1,7}, negativeAlpha[4]={5,1,1,7};
    encode(&materials[count++],0,quadraticRgb,quadraticAlpha,passRgb,passAlpha);
    encode(&materials[count++],0,negativeRgb,negativeAlpha,passRgb,passAlpha);
    encode(&materials[count++],1,quadraticRgb,quadraticAlpha,passRgb,passAlpha);
    const u8 previousAlphaRgb[4]={0,5,7,3}, previousAlphaAlpha[4]={0,5,0,3};
    encode(&materials[count++],1,quadraticRgb,quadraticAlpha,previousAlphaRgb,previousAlphaAlpha);
    encode(&materials[count++],1,negativeRgb,negativeAlpha,previousAlphaRgb,previousAlphaAlpha);
    const u8 glowRgb0[4]={2,1,12,1}, glowAlpha0[4]={2,1,5,1};
    const u8 glowRgb1[4]={3,5,0,5}, glowAlpha1[4]={0,7,3,7};
    encode(&materials[count++],1,glowRgb0,glowAlpha0,glowRgb1,glowAlpha1);
    const u8 alphaToRgb[4] = {1,15,7,7};
    static RDPState propagated;
    encode(&propagated, 1, mixedRgb, mixedAlpha, alphaToRgb, passAlpha);
    u32 primMasks[4], envMasks[4];
    ps2_combiner_lookup_dependencies(&propagated, primMasks, envMasks);
    CHECK((primMasks[0] & 255u) == 255u && (envMasks[0] & 255u) == 255u);
    CHECK((primMasks[1] & 255u) == 255u && (envMasks[2] & 255u) == 255u);
    static u8 full[4][256], reference[4][256], partial[4][256], cached[4][256], previous[4][256];
    static RDPState base, material;
    for (unsigned fixture = 0; fixture < count; fixture++) {
        base = materials[fixture];
        base.primColor = 0x193d8fa7u;
        base.envColor = 0xd26b4753u;
        scalar_reference(&base, previous);
        ps2_combiner_lookup_dependencies(&base, primMasks, envMasks);
        for (unsigned change = 0; change < 9; change++) {
            material = base;
            if (change && change <= 4) material.primColor ^= 255u << ((change-1)*8);
            else if (change) material.envColor ^= 255u << ((change-5)*8);
            scalar_reference(&material, reference);
            ps2_combiner_build_lookup(&material, full);
            CHECK(!memcmp(full, reference, sizeof(full)));
            for (unsigned channel = 0; channel < 4; channel++) {
                if (!((material.primColor ^ base.primColor) & primMasks[channel]) &&
                    !((material.envColor ^ base.envColor) & envMasks[channel]))
                    CHECK(!memcmp(reference[channel], previous[channel], 256));
            }
            for (unsigned mask = 0; mask < 16; mask++) {
                memset(partial, 0xa5, sizeof(partial));
                ps2_combiner_build_lookup_channels(&material, partial, mask);
                for (unsigned channel = 0; channel < 4; channel++) {
                    if (mask & (1u << channel)) CHECK(!memcmp(partial[channel], reference[channel], 256));
                    else for (unsigned value = 0; value < 256; value++) CHECK(partial[channel][value] == 0xa5);
                }
                checks++;
            }
            ps2_texture_test_combiner_lookup(&material, cached);
            CHECK(!memcmp(cached, reference, sizeof(cached)));
            CHECK(!packed_matches(&material, reference));
        }
    }
    /* Fill all 64 LUT slots with distinct RGB constants. The oldest entry
     * is the only possible RGB donor for the next alpha-only change and is
     * also the entry selected for replacement. */
    encode(&material, 0, primRgb, constantEnvAlpha, passRgb, passAlpha);
    for (unsigned i = 0; i < 64; i++) {
        material.primColor = ((i+10u) << 24) | 0x2030ffu;
        material.envColor = 77;
        ps2_texture_test_combiner_lookup(&material, cached);
    }
    material.primColor = 0x0a2030ffu;
    material.envColor = 78;
    unsigned generated = ps2_texture_test_combiner_lookup(&material, cached);
    CHECK(generated == 8u);
    scalar_reference(&material, reference);
    CHECK(!memcmp(cached, reference, sizeof(cached)));
    CHECK(!packed_matches(&material, reference));
    CHECK(ps2_texture_test_combiner_lookup(&material, cached) == 0);

    /* Reproduce seven RGB tints with changing alpha over more states than
     * the full-material cache can retain. Include full-output copy cost in
     * the cached benchmark, so it cannot overstate production's savings. */
    volatile u32 fullChecksum = 0, cachedChecksum = 0;
    unsigned generatedChannels = 0;
    encode(&material, 1, mixedRgb, envAlpha, passRgb, passAlpha);
    u32 start = ps2_gfx_cycles();
    for (unsigned frame = 0; frame < 32; frame++) for (unsigned tint = 0; tint < 7; tint++) {
        material.primColor = 0xf0ffe0ffu;
        material.envColor = ((tint*19u) << 24) | ((tint*23u) << 16) | ((255u-tint*17u) << 8) |
            ((frame*7u+tint*29u+3u)&255u);
        ps2_combiner_build_lookup(&material, full);
        for (unsigned channel = 0; channel < 4; channel++) fullChecksum += full[channel][(frame*11u+tint*31u)&255u];
    }
    u32 fullCycles = ps2_gfx_cycles()-start;
    start = ps2_gfx_cycles();
    for (unsigned frame = 0; frame < 32; frame++) for (unsigned tint = 0; tint < 7; tint++) {
        material.primColor = 0xf0ffe0ffu;
        material.envColor = ((tint*19u) << 24) | ((tint*23u) << 16) | ((255u-tint*17u) << 8) |
            ((frame*7u+tint*29u+3u)&255u);
        unsigned mask = ps2_texture_test_combiner_lookup(&material, cached);
        for (unsigned channel = 0; channel < 4; channel++) {
            generatedChannels += !!(mask & (1u << channel));
            cachedChecksum += cached[channel][(frame*11u+tint*31u)&255u];
        }
    }
    u32 cachedCycles = ps2_gfx_cycles()-start;
    CHECK(fullChecksum == cachedChecksum);
    CHECK(generatedChannels < 32u*7u*2u);
    CHECK(ps2_combiner_test_folded_passes > foldedBefore);
    CHECK(ps2_combiner_test_retained_passes > retainedBefore);
    printf("[PS2 COMBINER CHANNEL TEST] PASS: fixtures=%u partialChecks=%u masks, scalar, packed RGBA16 and LRU self-donor\n",count,checks);
    printf("[PS2 COMBINER CHANNEL BENCH] fullCycles=%u cachedCycles=%u generatedChannels=%u/%u checksum=%u\n",
        (unsigned)fullCycles,(unsigned)cachedCycles,generatedChannels,32u*7u*4u,(unsigned)cachedChecksum);
    printf("[PS2 COMBINER IDENTITY TEST] foldedPasses=%u retainedPasses=%u failures=0\n",
        ps2_combiner_test_folded_passes-foldedBefore, ps2_combiner_test_retained_passes-retainedBefore);
    /* Genuine two-cycle material with cross-channel COMBINED_ALPHA. */
    encode(&material,1,mixedRgb,mixedAlpha,previousAlphaRgb,previousAlphaAlpha);
    volatile u32 twoCycleChecksum=0;
    start=ps2_gfx_cycles();
    for (unsigned frame=0;frame<32;frame++) for (unsigned tint=0;tint<7;tint++) {
        material.primColor=0xf0ffe0ffu;
        material.envColor=((tint*19u)<<24)|((tint*23u)<<16)|((255u-tint*17u)<<8)|
            ((frame*7u+tint*29u+3u)&255u);
        ps2_combiner_build_lookup(&material,full);
        for (unsigned channel=0;channel<4;channel++)
            twoCycleChecksum+=full[channel][(frame*11u+tint*31u)&255u];
    }
    printf("[PS2 COMBINER TWO CYCLE BENCH] cycles=%u checksum=%u\n",
        (unsigned)(ps2_gfx_cycles()-start),(unsigned)twoCycleChecksum);
    return 0;
}

/* Run after the descriptor-pressure test, which requires an empty texture
 * cache. LUT-only checks above do not allocate textures or GS VRAM. */
int ps2_glow_dependency_smoke(void) {
    /* Actual gGlowCircleSmallDL material: the generic one-input path's
     * TEXEL1-TEXEL0 is exactly zero. ENV_ALPHA must not invalidate RGB or
     * alpha, but a tint change or a changed source byte still must upload. */
    static RDPState glow;
    static u8 glowSource[2048] __attribute__((aligned(16))), glowPixels[8192];
    static u8 reference[4][256], cached[4][256];
    const u8 glowRgb0[4]={2,1,12,1}, glowAlpha0[4]={2,1,5,1};
    const u8 glowRgb1[4]={3,5,0,5}, glowAlpha1[4]={0,7,3,7};
    encode(&glow,1,glowRgb0,glowAlpha0,glowRgb1,glowAlpha1);
    glow.gs=g_rdpState.gs; glow.primColor=0xffffffffu; glow.envColor=0xe5005a00u;
    u32 usedPrim,usedEnv;
    ps2_combiner_constants(&glow,&usedPrim,&usedEnv);
    CHECK((usedEnv&255u)==0 && (usedPrim&255u)==255u);
    CHECK(!ps2_combiner_texture_lerp(&glow) && !ps2_combiner_path_lerp(&glow));
    for (unsigned i=0;i<sizeof(glowSource);i++) glowSource[i]=(i*37u+11u)&255u;
    TileDescriptor* glowTile=&glow.tile[0];
    *glowTile=(TileDescriptor){.fmt=G_IM_FMT_I,.siz=G_IM_SIZ_8b,.width=32,.height=64,
        .masks=5,.maskt=6,.addr=glowSource};
    ps2_texture_upload(glowTile,&glow);
    CHECK(glowTile->gsTextureLoaded);
    memcpy(glowPixels,glowTile->gsTexture.Mem,sizeof(glowPixels));
    u32 glowUploads=glow.stats.textureUploads, glowVram=glowTile->gsTexture.Vram;
    for (unsigned alpha=1;alpha<256;alpha++) {
        rdp_setenvcolor(0,0xe5005a00u|alpha,&glow);
        ps2_texture_upload(glowTile,&glow);
        CHECK(glow.stats.textureUploads==glowUploads && glowTile->gsTexture.Vram==glowVram);
        CHECK(!memcmp(glowPixels,glowTile->gsTexture.Mem,sizeof(glowPixels)));
        scalar_reference(&glow,reference);
        ps2_texture_test_combiner_lookup(&glow,cached);
        CHECK(!memcmp(cached,reference,sizeof(cached)));
    }
    rdp_setenvcolor(0,0xe6005affu,&glow);
    ps2_texture_upload(glowTile,&glow);
    CHECK(glow.stats.textureUploads==glowUploads+1);
    glowSource[1000]^=1; glowTile->gsTextureLoaded=false;
    ps2_texture_upload(glowTile,&glow);
    CHECK(glow.stats.textureUploads==glowUploads+2);
    printf("[PS2 GLOW DEPENDENCY TEST] PASS: 255 alpha changes reuse pixels/VRAM; tint and RAM mutations upload\n");
    return 0;
}
