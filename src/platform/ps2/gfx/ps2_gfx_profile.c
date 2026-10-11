/* Private, opt-in sampling; no timer reads during normal game sessions.
 * COP0 Count advances once per EE cycle (294.912 MHz). Unsigned subtraction
 * permits wrap within a frame. DSP preemption is excluded; audio preparation
 * and other interrupts/threads remain included in these elapsed times. */
#include <stdio.h>
#include <string.h>
#include "ps2_gfx_profile.h"
#include "ps2_texture.h"
#include "ps2_lighting.h"
bool gPS2GfxProfileActive;
u32 gPS2GfxProfileCycles[GFX_PROFILE_COUNT];
u32 gPS2GfxProfileCalls[GFX_PROFILE_COUNT];
static unsigned sFrame;
static unsigned sInterval = 60;
void ps2_gfx_profile_sample_every(unsigned frames) { sInterval = frames ? frames : 60; }
static bool sChecked, sEnabled;
void ps2_gfx_profile_enable(bool enabled) {
    sChecked = true;
    sEnabled = enabled;
}
static u32 sStart;
static u32 sWallStart;
typedef struct {
    u32 source, prim, env;
    unsigned width, height, fmt, siz, reason, paired;
} TextureMiss;
static TextureMiss sMisses[24];
static unsigned sMissCount;
static unsigned sRunFences[4];
enum {
    FENCE_TEXTURE, FENCE_GEOMETRY, FENCE_MOVE_WORD, FENCE_MOVE_MEM,
    FENCE_TIMG, FENCE_TILE, FENCE_TILE_SIZE, FENCE_LOAD, FENCE_TLUT,
    FENCE_COMBINE, FENCE_PRIM, FENCE_ENV, FENCE_FOG, FENCE_BLEND,
    FENCE_FILL, FENCE_OTHER_MODE, FENCE_SYNC, FENCE_NOOP, FENCE_OTHER,
    FENCE_COUNT
};
static unsigned sOtherFences[FENCE_COUNT];
void ps2_gfx_profile_run_fence(u32 opcode) {
    if (!gPS2GfxProfileActive) return;
    /* Count command boundaries, including empty runs. No batching changes. */
    unsigned kind = opcode == 0x01 ? 0 : opcode == 0xde ? 1 : opcode == 0xdf ? 2 : 3;
    sRunFences[kind]++;
    if (kind != 3) return;
    /* Opcode values match the F3DEX2/RDP interpreter in rdp_to_gs.c. Each
     * otherFence belongs to exactly one category; VTX/DL/END stay separate. */
    unsigned category = FENCE_OTHER;
    switch (opcode) {
        case 0xd7: category = FENCE_TEXTURE; break;
        case 0xd9: category = FENCE_GEOMETRY; break;
        case 0xdb: category = FENCE_MOVE_WORD; break;
        case 0xdc: category = FENCE_MOVE_MEM; break;
        case 0xfd: category = FENCE_TIMG; break;
        case 0xf5: category = FENCE_TILE; break;
        case 0xf2: category = FENCE_TILE_SIZE; break;
        case 0xf3: case 0xf4: category = FENCE_LOAD; break;
        case 0xf0: category = FENCE_TLUT; break;
        case 0xfc: category = FENCE_COMBINE; break;
        case 0xfa: category = FENCE_PRIM; break;
        case 0xfb: category = FENCE_ENV; break;
        case 0xf8: category = FENCE_FOG; break;
        case 0xf9: category = FENCE_BLEND; break;
        case 0xf7: category = FENCE_FILL; break;
        case 0xef: case 0xe2: case 0xe3: category = FENCE_OTHER_MODE; break;
        case 0xe6: case 0xe7: case 0xe8: case 0xe9: category = FENCE_SYNC; break;
        case 0x00: category = FENCE_NOOP; break;
    }
    sOtherFences[category]++;
}
void ps2_gfx_profile_texture_miss(u32 source, u32 prim, u32 env,
    unsigned width, unsigned height, unsigned fmt, unsigned siz,
    unsigned reason, bool paired) {
    if (!gPS2GfxProfileActive || sMissCount >= 24) return;
    sMisses[sMissCount++] = (TextureMiss){source,prim,env,width,height,fmt,siz,reason,paired};
}
static u32 micros(u32 cycles) { return (u64)cycles * 1000000 / 294912000u; }
void ps2_gfx_profile_begin(void) {
    if (!sChecked) {
        FILE* flag = fopen("host:ps2-gfx-profile.enable", "rb");
        sEnabled = flag != NULL;
        if (flag) fclose(flag);
        sChecked = true;
    }
    gPS2GfxProfileActive = sEnabled && ++sFrame % sInterval == 0;
    if (gPS2GfxProfileActive) {
        memset(gPS2GfxProfileCycles, 0, sizeof(gPS2GfxProfileCycles));
        memset(gPS2GfxProfileCalls, 0, sizeof(gPS2GfxProfileCalls));
        ps2_combiner_analysis_reset_stats();
        ps2_light_cache_stats(true);
        ps2_texture_flame_noise_stats(true);
        sWallStart = ps2_gfx_cycles();
        sStart = ps2_gfx_profile_enter();
        sMissCount = 0;
        memset(sRunFences,0,sizeof(sRunFences));
        memset(sOtherFences,0,sizeof(sOtherFences));
    }
}
void ps2_gfx_profile_end(void) {
    if (!gPS2GfxProfileActive) return;
    u32 total = ps2_gfx_profile_enter() - sStart;
    u32 wall = ps2_gfx_cycles() - sWallStart;
    gPS2GfxProfileActive = false;
    printf("[PS2 PROFILE PREEMPTION] frame=%u wallUs=%u dspExcludedUs=%u (other preemption included)\n",
        sFrame,micros(wall),micros(wall-total));
    printf("[PS2 GFX PROFILE] frame=%u totalUs=%u vtxUs=%u triUs=%u rectUs=%u lookupUs=%u convertUs=%u uploadUs=%u calls=%u,%u,%u,%u,%u,%u (texture phases included in tri/rect)\n",
        sFrame, micros(total), micros(gPS2GfxProfileCycles[0]), micros(gPS2GfxProfileCycles[1]),
        micros(gPS2GfxProfileCycles[2]), micros(gPS2GfxProfileCycles[3]),
        micros(gPS2GfxProfileCycles[4]), micros(gPS2GfxProfileCycles[5]),
        gPS2GfxProfileCalls[0],gPS2GfxProfileCalls[1],gPS2GfxProfileCalls[2],
        gPS2GfxProfileCalls[3],gPS2GfxProfileCalls[4],gPS2GfxProfileCalls[5]);
    printf("[PS2 GEOMETRY PROFILE] frame=%u matrixUs=%u transformUs=%u lightUs=%u texgenUs=%u clipUs=%u submitUs=%u calls=%u,%u,%u,%u,%u,%u (submit includes textures, vertex phases included in vtx)\n",
        sFrame, micros(gPS2GfxProfileCycles[6]), micros(gPS2GfxProfileCycles[7]),
        micros(gPS2GfxProfileCycles[8]), micros(gPS2GfxProfileCycles[9]),
        micros(gPS2GfxProfileCycles[10]), micros(gPS2GfxProfileCycles[11]),
        gPS2GfxProfileCalls[6],gPS2GfxProfileCalls[7],gPS2GfxProfileCalls[8],
        gPS2GfxProfileCalls[9],gPS2GfxProfileCalls[10],gPS2GfxProfileCalls[11]);
    ps2_texture_profile_cache();
    PS2FlameNoiseStats flame = ps2_texture_flame_noise_stats(false);
    printf("[PS2 FLAME NOISE PROFILE] frame=%u hits=%u builds=%u comparedBytes=%u storageBytes=%u (included in convert)\n",
        sFrame,flame.hits,flame.builds,flame.comparedBytes,flame.storageBytes);
    PS2LightingCacheStats lighting = ps2_light_cache_stats(false);
    printf("[PS2 LIGHT CACHE PROFILE] frame=%u tableChanges=%u rgbHits=%u rgbBuilds=%u normalHits=%u normalBuilds=%u storageBytes=%u rgbEntries=%u rgbBytes=%u\n",
        sFrame,lighting.tableChanges,lighting.rgbHits,lighting.rgbBuilds,
        lighting.normalHits,lighting.normalBuilds,lighting.storageBytes,
        lighting.rgbEntries,lighting.rgbStorageBytes);
    PS2CombinerAnalysisStats analysis;
    ps2_combiner_analysis_get_stats(&analysis);
    printf("[PS2 COMBINER ANALYSIS PROFILE] frame=%u timeUs=%u lookups=%u lastHits=%u savedHits=%u rebuilds=%u entries=32 storageBytes=%u (included in state/lookup/submit)\n",
        sFrame,micros(gPS2GfxProfileCycles[GFX_PROFILE_ANALYSIS]),(unsigned)analysis.lookups,
        (unsigned)analysis.lastHits,(unsigned)analysis.savedHits,(unsigned)analysis.rebuilds,(unsigned)analysis.storageBytes);
    printf("[PS2 STATE PROFILE] frame=%u timeUs=%u calls=%u writes=%u reused=%u\n", sFrame,
        micros(gPS2GfxProfileCycles[GFX_PROFILE_STATE]),gPS2GfxProfileCalls[GFX_PROFILE_STATE],
        g_rdpState.stats.stateWrites,g_rdpState.stats.stateReuse);
    printf("[PS2 TEXTURE CONTENT PROFILE] frame=%u timeUs=%u calls=%u (included in lookup)\n",
        sFrame,micros(gPS2GfxProfileCycles[GFX_PROFILE_CONTENT]),gPS2GfxProfileCalls[GFX_PROFILE_CONTENT]);
    printf("[PS2 VERTEX COLOR PROFILE] frame=%u timeUs=%u calls=%u (included in submit)\n",
        sFrame,micros(gPS2GfxProfileCycles[GFX_PROFILE_VERTEX_COLOR]),
        gPS2GfxProfileCalls[GFX_PROFILE_VERTEX_COLOR]);
    printf("[PS2 MATERIAL POINT PROFILE] frame=%u materialUs=%u materialCalls=%u pointUs=%u pointCalls=%u (material included in convert, points in submit)\n",
        sFrame,micros(gPS2GfxProfileCycles[GFX_PROFILE_MATERIAL]),gPS2GfxProfileCalls[GFX_PROFILE_MATERIAL],
        micros(gPS2GfxProfileCycles[GFX_PROFILE_POINT]),gPS2GfxProfileCalls[GFX_PROFILE_POINT]);
    printf("[PS2 DRAW DETAIL PROFILE] frame=%u keyUs=%u keyCalls=%u packetUs=%u packetCalls=%u wrapUs=%u wrapCalls=%u (keys included in lookup; packets and wrap in submit)\n",
        (unsigned)sFrame,(unsigned)micros(gPS2GfxProfileCycles[GFX_PROFILE_KEY]),(unsigned)gPS2GfxProfileCalls[GFX_PROFILE_KEY],
        (unsigned)micros(gPS2GfxProfileCycles[GFX_PROFILE_PACKET]),(unsigned)gPS2GfxProfileCalls[GFX_PROFILE_PACKET],
        (unsigned)micros(gPS2GfxProfileCycles[GFX_PROFILE_WRAP]),(unsigned)gPS2GfxProfileCalls[GFX_PROFILE_WRAP]);
    printf("[PS2 DRAW SETUP PROFILE] frame=%u setupUs=%u setupCalls=%u tileUs=%u tileCalls=%u metadataUs=%u metadataCalls=%u (setup and tiles in submit; metadata in lookup)\n",
        (unsigned)sFrame,(unsigned)micros(gPS2GfxProfileCycles[GFX_PROFILE_DRAW_SETUP]),(unsigned)gPS2GfxProfileCalls[GFX_PROFILE_DRAW_SETUP],
        (unsigned)micros(gPS2GfxProfileCycles[GFX_PROFILE_TILE]),(unsigned)gPS2GfxProfileCalls[GFX_PROFILE_TILE],
        (unsigned)micros(gPS2GfxProfileCycles[GFX_PROFILE_METADATA]),(unsigned)gPS2GfxProfileCalls[GFX_PROFILE_METADATA]);
    PS2PaletteHashStats palette = ps2_texture_palette_hash_stats(false);
    printf("[PS2 PALETTE HASH PROFILE] frame=%u timeUs=%u calls=%u hits=%u builds=%u changedEntries=%u (included in metadata; TLUT updates outside this timer)\n",
        sFrame,micros(gPS2GfxProfileCycles[GFX_PROFILE_PALETTE]),gPS2GfxProfileCalls[GFX_PROFILE_PALETTE],
        palette.hits,palette.builds,palette.changedEntries);
    printf("[PS2 MATERIAL RUN PROFILE] frame=%u vtxFences=%u dlFences=%u endFences=%u otherFences=%u\n",
        sFrame,sRunFences[0],sRunFences[1],sRunFences[2],sRunFences[3]);
    /* Two complete records fit the 320-byte runtime logger even at UINT_MAX. */
    printf("[PS2 MATERIAL FENCE PROFILE] frame=%u texture=%u geometry=%u moveWord=%u moveMem=%u timg=%u tile=%u tileSize=%u load=%u tlut=%u\n",
        sFrame,sOtherFences[FENCE_TEXTURE],sOtherFences[FENCE_GEOMETRY],
        sOtherFences[FENCE_MOVE_WORD],sOtherFences[FENCE_MOVE_MEM],sOtherFences[FENCE_TIMG],
        sOtherFences[FENCE_TILE],sOtherFences[FENCE_TILE_SIZE],sOtherFences[FENCE_LOAD],
        sOtherFences[FENCE_TLUT]);
    printf("[PS2 MATERIAL FENCE STATE PROFILE] frame=%u combine=%u prim=%u env=%u fog=%u blend=%u fill=%u otherMode=%u sync=%u noop=%u other=%u\n",
        sFrame,sOtherFences[FENCE_COMBINE],sOtherFences[FENCE_PRIM],
        sOtherFences[FENCE_ENV],sOtherFences[FENCE_FOG],sOtherFences[FENCE_BLEND],
        sOtherFences[FENCE_FILL],sOtherFences[FENCE_OTHER_MODE],sOtherFences[FENCE_SYNC],
        sOtherFences[FENCE_NOOP],sOtherFences[FENCE_OTHER]);
    for (unsigned i=0;i<sMissCount;i++) {
        const TextureMiss* miss=&sMisses[i];
        printf("[PS2 TEXTURE MISS] frame=%u source=%08x size=%ux%u fmt=%u siz=%u reason=%u paired=%u prim=%08x env=%08x\n",
            sFrame,(unsigned)miss->source,miss->width,miss->height,miss->fmt,miss->siz,
            miss->reason,miss->paired,(unsigned)miss->prim,(unsigned)miss->env);
    }
}
