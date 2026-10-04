/* Real transition setters and fade display lists, including changing alpha. */
#include "color_packing.h"
#include "main.h"
#include "regs.h"
#include "save.h"
#include "transition_circle.h"
#include "transition_fade.h"
#include "transition_triforce.h"
#include "transition_wipe.h"
#include "transition_instances.h"
extern int printf(const char*, ...);
/* The original wipe implementation has this secondary setter without a header declaration. */
extern void TransitionWipe_SetUnkColor(void* thisx, u32 color);

SaveContext gSaveContext;
s32 gScreenWidth = 320, gScreenHeight = 240;
static RegEditor sTransitionRegisters;
RegEditor* gRegEditor = &sTransitionRegisters;

static const struct { u32 packed; u8 rgba[4]; } sColors[] = {
    {0xa0a0a0ff, {160,160,160,255}}, // Original white transition.
    {0x00000000, {0,0,0,0}},
    {0xffffffff, {255,255,255,255}},
    {0x46646eff, {70,100,110,255}},
    {0x8c8c64ff, {140,140,100,255}},
    {0x12345678, {18,52,86,120}},
    {0xa0a0a000, {160,160,160,0}},
    {0x000000ff, {0,0,0,255}},
};

static int correct_color(const Color_RGBA8_u32* color, const u8* expected) {
    return color->r == expected[0] && color->g == expected[1] &&
           color->b == expected[2] && color->a == expected[3];
}

int ps2_transition_smoke(void) {
    unsigned failures = 0, draws = 0, setters = 0;
    TransitionFade fade;
    TransitionWipe wipe;
    TransitionCircle circle;
    TransitionTriforce triforce;
    const int durations[] = {2,20,60,150}, rates[] = {1,2,3};
    for (unsigned color=0; color<sizeof(sColors)/sizeof(sColors[0]); color++) {
        TransitionFade_Init(&fade);
        TransitionFade_SetColor(&fade, sColors[color].packed);
        TransitionWipe_SetColor(&wipe, sColors[color].packed);
        TransitionWipe_SetUnkColor(&wipe, sColors[color].packed);
        TransitionCircle_SetColor(&circle, sColors[color].packed);
        TransitionCircle_SetUnkColor(&circle, sColors[color].packed);
        TransitionTriforce_SetColor(&triforce, sColors[color].packed);
        const Color_RGBA8_u32* values[] = {&fade.color,&wipe.color,&wipe.unkColor,
                                         &circle.color,&circle.unkColor,&triforce.color};
        for (unsigned i=0;i<sizeof(values)/sizeof(values[0]);i++) {
            setters++;
            failures += !correct_color(values[i],sColors[color].rgba);
            failures += Color_RGBA8_GetPacked(values[i]) != sColors[color].packed;
        }
        for (unsigned duration=0;duration<4;duration++) for (unsigned rate=0;rate<3;rate++) {
            for (int direction=0;direction<2;direction++) {
                TransitionFade_Init(&fade);
                TransitionFade_SetColor(&fade,sColors[color].packed);
                TransitionFade_SetType(&fade,direction?TRANS_INSTANCE_TYPE_FILL_OUT:TRANS_INSTANCE_TYPE_FILL_IN);
                gSaveContext.transFadeDuration=durations[duration];
                TransitionFade_Start(&fade);
                int timer=0;
                do {
                    int alpha=255*timer/durations[duration];
                    if (direction) alpha=255-alpha;
                    Gfx commands[6], *end=commands;
                    TransitionFade_Draw(&fade,&end);
                    draws++;
                    if (!alpha) failures += end != commands;
                    else {
                        u32 expected=(sColors[color].packed&0xffffff00)|(u32)alpha;
                        failures += end != commands+4;
                        failures += (commands[1].words.w0>>24) != G_SETPRIMCOLOR;
                        failures += commands[1].words.w1 != expected;
                        failures += (commands[2].words.w0>>24) != G_FILLRECT;
                    }
                    failures += fade.color.r!=sColors[color].rgba[0] || fade.color.g!=sColors[color].rgba[1] ||
                                fade.color.b!=sColors[color].rgba[2] || fade.color.a!=alpha;
                    if (timer==durations[duration]) break;
                    TransitionFade_Update(&fade,rates[rate]);
                    timer+=rates[rate];
                    if (timer>durations[duration]) timer=durations[duration];
                    failures += TransitionFade_IsDone(&fade) != (timer==durations[duration]);
                } while (1);
            }
        }
    }
    printf("[PS2 TRANSITION TEST] setters=%u fadeDraws=%u originalWhite=160,160,160 failures=%u\n",
           setters,draws,failures);
    return failures!=0;
}
