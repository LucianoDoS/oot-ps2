/* Exercise the real frame-event predicate on the EE, including wraparound. */
#include "animation.h"
extern int printf(const char*, ...);
extern s32 Animation_OnFrameImpl(SkelAnime*, f32, f32);

int ps2_animation_smoke(void) {
    static const f32 speeds[] = {0.5f, 2.0f/3.0f, 1.0f, 1.5f, 2.0f, -0.5f, -1.0f, -2.0f};
    static const f32 rates[] = {0.5f, 1.0f, 1.5f, 2.0f};
    SkelAnime anim = {0};
    int failures=0, cases=0;
    /* Real missing-item case: the receive animation has 22 frames and
     * schedules its model on its last frame, 21, at adjusted speed. */
    anim.animLength=22.0f;
    anim.playSpeed=2.0f/3.0f;
    anim.curFrame=21.0f;
    if (!Animation_OnFrameImpl(&anim,21.0f,1.5f)) {
        printf("[PS2 ANIMATION EVENT] FAIL: get-item last-frame 21\n");
        failures++;
    }
    cases++;
    anim.animLength=40.0f;
    for (unsigned s=0;s<sizeof(speeds)/sizeof(speeds[0]);s++) {
        anim.playSpeed=speeds[s];
        for (unsigned r=0;r<sizeof(rates)/sizeof(rates[0]);r++) {
            f32 step=speeds[s]*rates[r];
            anim.curFrame=10.25f;
            for (int tick=0;tick<80;tick++) {
                f32 previous=anim.curFrame;
                f32 next=previous+step;
                anim.curFrame=next;
                if (anim.curFrame<0) anim.curFrame+=anim.animLength;
                else if (anim.curFrame>=anim.animLength) anim.curFrame-=anim.animLength;
                for (int event=0;event<40;event++) {
                    /* Match the original predicate's unwrapped interval:
                     * on forward wrap only frame zero is mapped to length.
                     * Do not change its game semantics through the oracle. */
                    f32 intervalStart=anim.curFrame-step;
                    f32 intervalEnd=anim.curFrame;
                    if (intervalStart<0) { intervalStart+=anim.animLength; intervalEnd+=anim.animLength; }
                    else if (intervalStart>=anim.animLength) { intervalStart-=anim.animLength; intervalEnd-=anim.animLength; }
                    f32 target=(event==0 && step>0) ? anim.animLength : (f32)event;
                    int expected=step>0 ? target>intervalStart && target<=intervalEnd :
                        target<intervalStart && target>=intervalEnd;
                    int actual=Animation_OnFrameImpl(&anim,(f32)event,rates[r]);
                    if (actual!=expected) {
                        if (failures<4) printf("[PS2 ANIMATION EVENT] previous=%.6f current=%.6f step=%.6f event=%d actual=%d expected=%d\n",
                                              previous,anim.curFrame,step,event,actual,expected);
                        failures++;
                    }
                    cases++;
                }
            }
        }
    }
    printf("[PS2 ANIMATION TEST] cases=%d failures=%d\n",cases,failures);
    return failures;
}
