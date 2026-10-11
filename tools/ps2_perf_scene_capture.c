/* SDK-only timing/readback writer. Records are emitted after timed rendering. */
#include <stdio.h>
#include <string.h>
#include "rdp_to_gs.h"
#include "ps2_offscreen.h"
#include "ps2_gfx_profile.h"
#include "ps2_texture.h"
#include "ps2_perf_scene.h"
extern u64 osGetTime(void);
extern void __real_rdp_to_gs_exec(const Gfx*);
extern void __real_rdp_to_gs_submit(void);
extern void __real_rdp_to_gs_present(void);
extern void __real_ps2_gfx_profile_begin(void);
extern void __real_ps2_capture_frame(RDPState*, const void*);
static unsigned char sPixels[320 * 240 * 2] __attribute__((aligned(64)));
PS2RenderTarget gPerfCaptureTarget = { sPixels, 320, 240 };
volatile unsigned gPerfCaptureDone, gPerfViewComplete;
typedef struct {
    unsigned frame, profiled, translateUs, submitUs, presentUs, intervalUs;
    unsigned triangles, submitted, clipped, culled, invalid, textureFailed, uploads, states;
    unsigned colorRetained, colorInvalidated, colorForced;
#if PS2_PERF_SCENE_TITLE_CINEMATIC
    PS2PerfMotionTag motion;
#endif
} PerfFrame;
static PerfFrame sFrames[PS2_PERF_SCENE_FRAMES];
static PerfFrame sPending;
static PS2PerfTaskTag sTag;
static unsigned sCount, sProfiled;
static u64 sBegin, sTranslated, sSubmitted, sLastPresented;
static unsigned micros(u64 ticks) { return ticks * 1000000ull / 46875000ull; }
void ps2_perf_scene_profile_init(void) {
    ps2_gfx_profile_enable(true);
    ps2_gfx_profile_sample_every(0xffffffffu);
}
void __wrap_ps2_gfx_profile_begin(void) {
    /* Preserve the real global record counter while selecting exact fixture
     * frames. Startup/reload draw counts cannot shift the measured subset. */
    int selected = sTag.root && sTag.frame && sTag.frame % 60 == 0 &&
                   sTag.frame <= PS2_PERF_SCENE_WARMUP + PS2_PERF_SCENE_FRAMES;
    ps2_gfx_profile_sample_every(selected ? 1 : 0xffffffffu);
    __real_ps2_gfx_profile_begin();
    sProfiled = gPS2GfxProfileActive;
}
void __wrap_rdp_to_gs_exec(const Gfx* root) {
    sTag = (PS2PerfTaskTag){ 0 };
    for (unsigned i = 0; i < 2; i++) if (gPerfTaskTags[i].root == root) sTag = gPerfTaskTags[i];
    if (sTag.frame == 1) { sCount = 0; sLastPresented = 0; }
    sBegin = osGetTime();
    __real_rdp_to_gs_exec(root);
    sTranslated = osGetTime();
}
void __wrap_rdp_to_gs_submit(void) {
    /* submit clears the renderer counters after retiring GS work. Snapshot
     * the current task before that reset; presentation must not read them. */
    PS2TextureColorInvalidationStats color = ps2_texture_color_invalidation_stats(true);
    sPending = (PerfFrame){ .frame=sTag.frame, .profiled=sProfiled,
        .triangles=g_rdpState.stats.triangles, .submitted=g_rdpState.stats.submitted,
        .clipped=g_rdpState.stats.clipped, .culled=g_rdpState.stats.culled,
        .invalid=g_rdpState.stats.invalid, .textureFailed=g_rdpState.stats.textureFailed,
        .uploads=g_rdpState.stats.textureUploads, .states=g_rdpState.stats.stateWrites,
        .colorRetained=color.retained, .colorInvalidated=color.invalidated, .colorForced=color.forced };
#if PS2_PERF_SCENE_TITLE_CINEMATIC
    sPending.motion = sTag.motion;
#endif
    __real_rdp_to_gs_submit();
    sSubmitted = osGetTime();
}
void __wrap_rdp_to_gs_present(void) {
    __real_rdp_to_gs_present();
    u64 now = osGetTime();
    if (sTag.frame > PS2_PERF_SCENE_WARMUP &&
        sTag.frame <= PS2_PERF_SCENE_WARMUP + PS2_PERF_SCENE_FRAMES && sCount < PS2_PERF_SCENE_FRAMES) {
        sPending.translateUs = micros(sTranslated - sBegin);
        sPending.submitUs = micros(sSubmitted - sTranslated);
        sPending.presentUs = micros(now - sSubmitted);
        sPending.intervalUs = sLastPresented ? micros(now - sLastPresented) : 0;
        sFrames[sCount++] = sPending;
    }
    sLastPresented = now;
    if (sTag.capture) {
        for (unsigned i = 0; i < sCount; i++) {
            const PerfFrame* f = &sFrames[i];
            printf("[PS2 PERF SCENE FRAME] view=%u frame=%u profiled=%u translateUs=%u submitUs=%u presentUs=%u intervalUs=%u triangles=%u submitted=%u clipped=%u culled=%u invalid=%u textureFailed=%u uploads=%u states=%u colorRetained=%u colorInvalidated=%u colorForced=%u\n",
                sTag.view, f->frame, f->profiled, f->translateUs, f->submitUs, f->presentUs, f->intervalUs,
                f->triangles, f->submitted, f->clipped, f->culled, f->invalid, f->textureFailed, f->uploads, f->states,
                f->colorRetained, f->colorInvalidated, f->colorForced);
#if PS2_PERF_SCENE_TITLE_CINEMATIC
            const PS2PerfMotionTag* m = &f->motion;
            printf("[PS2 PERF SCENE MOTION] view=%u frame=%u scene=%u room=%u gameMode=%u linkAge=%u cutsceneState=%u cutsceneFrame=%u riding=%u horseDrawable=%u eyeX=%d eyeY=%d eyeZ=%d atX=%d atY=%d atZ=%d fovMilli=%d playerX=%d playerY=%d playerZ=%d\n",
                sTag.view, f->frame, m->scene, m->room, m->gameMode, m->linkAge, m->cutsceneState, m->cutsceneFrame,
                m->riding, m->horseDrawable, m->eyeX, m->eyeY, m->eyeZ, m->atX, m->atY, m->atZ,
                m->fovMilli, m->playerX, m->playerY, m->playerZ);
#endif
        }
        printf("[PS2 PERF SCENE MEASURED] %s view=%u count=%u expected=%u captureOutsideTiming=1\n",
            sCount == PS2_PERF_SCENE_FRAMES ? "PASS" : "FAIL", sTag.view, sCount, PS2_PERF_SCENE_FRAMES);
        gPerfViewComplete = 1;
    }
}
void __wrap_ps2_capture_frame(RDPState* state, const void* descriptor) {
    __real_ps2_capture_frame(state, descriptor);
    if (descriptor != &gPerfCaptureTarget) return;
    char path[80];
    snprintf(path, sizeof(path), "host:perf-view-%u.ppm", sTag.view);
    FILE* out = fopen(path, "wb");
    int ok = out && fprintf(out, "P6\n320 240\n255\n") > 0;
    unsigned char row[320 * 3];
    for (unsigned y = 0; y < 240 && ok; y++) {
        for (unsigned x = 0; x < 320; x++) {
            unsigned at = 2 * (y * 320 + x), pixel = sPixels[at] << 8 | sPixels[at + 1];
            row[x * 3] = ((pixel >> 11) & 31) * 255 / 31;
            row[x * 3 + 1] = ((pixel >> 6) & 31) * 255 / 31;
            row[x * 3 + 2] = ((pixel >> 1) & 31) * 255 / 31;
        }
        ok = fwrite(row, 1, sizeof(row), out) == sizeof(row);
    }
    if (out && fclose(out)) ok = 0;
    printf("[PS2 PERF SCENE CAPTURE] %s view=%u file=%s\n", ok ? "PASS" : "FAIL", sTag.view, path);
    gPerfCaptureDone = ok ? 1 : 2;
}
