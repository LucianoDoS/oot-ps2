#include <stdio.h>
#include <string.h>
#include "ps2_fps_meter.h"
#include "ps2_fps_overlay.h"
#include "ps2_offscreen.h"
#include "ps2_segment.h"
#include "ps2_gfx_profile.h"

static RDPState saved;
static u8 image[320 * 240 * 2] __attribute__((aligned(128)));
int ps2_fps_smoke(void) {
    int failures = 0;
    const unsigned rates[] = {60,30,20,12,8};
    for (unsigned test = 0; test < sizeof(rates)/sizeof(*rates); test++) {
        PS2FpsMeter meter = {0}; unsigned rate = rates[test];
        ps2_fps_meter_frame(&meter, 0, 0);
        for (unsigned frame = 1; frame <= rate; frame++)
            ps2_fps_meter_frame(&meter, frame * PS2_FPS_TICKS_PER_SECOND / rate, frame * 60 / rate);
        if (meter.fps10 != rate * 10 || meter.fallback) failures++;
        /* A long loading stall belongs to the elapsed time, not 20 FPS. */
        ps2_fps_meter_frame(&meter, 6 * PS2_FPS_TICKS_PER_SECOND, 360);
        if (meter.fps10 != 2) failures++;
    }
    PS2FpsMeter stopped = {0};
    ps2_fps_meter_frame(&stopped, 0, ~0u - 59);
    for (unsigned i = 1; i <= 20; i++) ps2_fps_meter_frame(&stopped, 0, (~0u - 59) + i * 3);
    if (stopped.fps10 != 200 || !stopped.fallback) failures++;

    saved = g_rdpState;
    GSGLOBAL* gs = g_rdpState.gs;
    GSTEST savedTest = *gs->Test;
    PS2RenderTarget target = {image,320,240};
    const u32 clear[][2] = {
        {0xed000000,(320u<<14)|(240u<<2)}, {0xef300000,0},
        {0xff10013f,0x500000}, {0xf7000000,0x003f003f},
        {0xf6000000|(319u<<14)|(239u<<2),0}, {0xdf000000,0}
    };
    rdp_to_gs_exec((const Gfx*)clear);
    gs->Test->ATE = 1; gs->Test->ATST = 0;
    gs->Test->ZTE = 1; gs->Test->ZTST = 2;
    gsKit_set_test(gs,0);
    GSTEST rejectTest = *gs->Test;
    void* before = gs->CurQueue->pool_cur;
    g_rdpState.renderTargetActive = true;
    ps2_fps_overlay_draw(&g_rdpState,200,false);
    if (before != gs->CurQueue->pool_cur) failures++;
    g_rdpState.renderTargetActive = false;
    u32 start = ps2_gfx_cycles();
    ps2_fps_overlay_draw(&g_rdpState,200,false);
    u32 cycles = ps2_gfx_cycles() - start;
    unsigned packetBytes = (u8*)gs->CurQueue->pool_cur - (u8*)before;
    if (memcmp(gs->Test,&rejectTest,sizeof(rejectTest))) failures++;
    /* The overlay must restore the rejecting alpha test: this red sprite
     * disappears. It must itself remain visible over the depth-tested game. */
    gsKit_prim_sprite(gs,100*gs->Width/320.0f,100*gs->Height/240.0f,
        110*gs->Width/320.0f,110*gs->Height/240.0f,0,GS_SETREG_RGBA(255,0,0,128));
    const u32 capture[][2] = {{PS2_GFX_CAPTURE_FRAME,(u32)&target},{0xdf000000,0}};
    rdp_to_gs_exec((const Gfx*)capture);
    u16 foreground = read_be16(image+2*(10*320+12));
    u16 background = read_be16(image+2*(9*320+11));
    u16 after = read_be16(image+2*(105*320+105));
    if (foreground != 0xffff || background != 0x0001 || after != 0x003f) failures++;
    *gs->Test = savedTest; g_rdpState = saved;
    ps2_gs_state_reset(); rdp_apply_othermode(&g_rdpState);
    printf("[PS2 FPS HUD TEST] failures=%d glyph=%04x background=%04x restoredTest=%04x bytes=%u cycles=%u\n",
        failures,foreground,background,after,packetBytes,cycles);
    return failures;
}
