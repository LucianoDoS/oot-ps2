/* Private, opt-in sampling; no timer reads during normal game sessions.
 * COP0 Count advances once per EE cycle (294.912 MHz). Unsigned subtraction
 * permits wrap within a frame. DSP preemption is excluded; audio preparation
 * and other interrupts/threads remain included in these elapsed times. */
#include <stdio.h>
#include <string.h>
#include "ps2_gfx_profile.h"
#include "ps2_texture.h"
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
        sWallStart = ps2_gfx_cycles();
        sStart = ps2_gfx_profile_enter();
        sMissCount = 0;
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
    for (unsigned i=0;i<sMissCount;i++) {
        const TextureMiss* miss=&sMisses[i];
        printf("[PS2 TEXTURE MISS] frame=%u source=%08x size=%ux%u fmt=%u siz=%u reason=%u paired=%u prim=%08x env=%08x\n",
            sFrame,(unsigned)miss->source,miss->width,miss->height,miss->fmt,miss->siz,
            miss->reason,miss->paired,(unsigned)miss->prim,(unsigned)miss->env);
    }
}
