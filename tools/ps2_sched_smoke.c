/* Exercise the production scheduler: real GS completion must not incur
 * dummy VI-buffer waits, but gameplay/menu caps and audio priority remain. */
#include "sched.h"
extern int printf(const char*, ...);
extern OSScTask* Sched_GfxTaskFramebufferValid(Scheduler*, OSScTask*);
extern s32 Sched_Schedule(Scheduler*, OSScTask**, OSScTask**, s32);

static int admit(Scheduler* sc, OSScTask* task, s32 freeUnits, int expected) {
    OSScTask *sp = NULL, *dp = NULL;
    sc->gfxListHead = sc->gfxListTail = task;
    s32 remaining = Sched_Schedule(sc, &sp, &dp, freeUnits);
    if (expected) {
        return remaining != 0 || sp != task || dp != task || sc->gfxListHead || sc->gfxListTail;
    }
    return remaining != freeUnits || sp || dp || sc->gfxListHead != task;
}

int ps2_sched_smoke(void) {
    static Scheduler sc;
    static CfbInfo cfb;
    static OSScTask gfx, audio, flush;
    int failures = 0;
    cfb.updateRate = 3;
    cfb.framebuffer = cfb.swapBuffer = (u16*)0x100000;
    gfx.list.t.type = M_GFXTASK;
    gfx.flags = gfx.state = OS_SC_SP | OS_SC_DP;
    gfx.framebuffer = &cfb;
    sc.pendingSwapBuf1 = sc.pendingSwapBuf2 = &cfb;
    if (Sched_GfxTaskFramebufferValid(&sc, NULL)) failures++;
    failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 1);
    for (int tick = 0; tick < 3; tick++) {
        sc.retraceCount = tick;
        if (Sched_GfxTaskFramebufferValid(&sc, &gfx)) failures++;
        failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 0);
    }
    /* Audio can run on SP while DP is busy and a graphics frame is paced. */
    audio.list.t.type = M_AUDTASK;
    sc.audioListHead = sc.audioListTail = &audio;
    sc.doAudio = 1;
    OSScTask *sp = NULL, *dp = NULL;
    if (Sched_Schedule(&sc, &sp, &dp, OS_SC_SP) || sp != &audio || dp ||
        sc.audioListHead || sc.audioListTail || sc.gfxListHead != &gfx) failures++;
    sc.retraceCount = 3;
    if (Sched_GfxTaskFramebufferValid(&sc, &gfx) != &gfx) failures++;
    failures += admit(&sc, &gfx, OS_SC_SP, 0); /* DP ownership still enforced */
    failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 1);
    sc.retraceCount = 10; /* late frame starts now, without a catch-up burst */
    failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 1);
    sc.retraceCount = 12;
    failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 0);
    sc.retraceCount = 13;
    failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 1);
    cfb.updateRate = 2; /* menu runs at 30 Hz */
    sc.retraceCount = 14;
    failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 0);
    sc.retraceCount = 15;
    failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 1);
    flush = gfx; flush.list.t.type = M_NULTASK;
    failures += admit(&sc, &flush, OS_SC_SP | OS_SC_DP, 1);
    failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 0);
    flush.framebuffer = NULL;
    failures += admit(&sc, &flush, OS_SC_SP | OS_SC_DP, 1);
    failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 0);
    sc.retraceCount = (s32)0xfffffffeu;
    failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 1);
    sc.retraceCount = (s32)0xffffffffu;
    failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 0);
    sc.retraceCount = 0;
    failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 1);
    cfb.updateRate = 1; /* File menus cannot exceed 30 Hz. */
    failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 0);
    sc.retraceCount = 1;
    failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 0);
    sc.retraceCount = 2;
    failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 1);
    cfb.updateRate = 0;
    sc.retraceCount = 3;
    failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 0);
    sc.retraceCount = 4;
    failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 1);
    Sched_SetConsoleLogoActive(1);
    cfb.updateRate = 1;
    failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 0); /* No same-VI burst. */
    for (int tick = 5; tick < 9; tick++) {
        sc.retraceCount = tick;
        failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 1);
    }
    cfb.updateRate = 3; /* The exception never overrides a slower request. */
    sc.retraceCount = 9;
    failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 0);
    Sched_SetConsoleLogoActive(0);
    cfb.updateRate = 1;
    failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 0);
    sc.retraceCount = 10;
    failures += admit(&sc, &gfx, OS_SC_SP | OS_SC_DP, 1);
    printf("[PS2 SCHED TEST] %s: 20/30/60 Hz, logo exit, late frames, audio priority, DP ownership, flush and wrap (%d)\n",
           failures ? "FAIL" : "PASS", failures);
    return failures;
}
