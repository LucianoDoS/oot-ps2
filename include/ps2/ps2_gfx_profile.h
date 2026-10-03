#ifndef PS2_GFX_PROFILE_H
#define PS2_GFX_PROFILE_H
#include "ps2_types.h"
enum { GFX_PROFILE_VTX, GFX_PROFILE_TRI, GFX_PROFILE_RECT,
       GFX_PROFILE_LOOKUP, GFX_PROFILE_CONVERT, GFX_PROFILE_UPLOAD,
       GFX_PROFILE_MATRIX, GFX_PROFILE_TRANSFORM, GFX_PROFILE_LIGHT,
       GFX_PROFILE_TEXGEN, GFX_PROFILE_CLIP, GFX_PROFILE_SUBMIT,
       GFX_PROFILE_VERTEX_COLOR, GFX_PROFILE_MATERIAL, GFX_PROFILE_POINT,
       GFX_PROFILE_STATE,
       GFX_PROFILE_COUNT };
extern bool gPS2GfxProfileActive;
extern u32 gPS2GfxProfileCycles[GFX_PROFILE_COUNT];
extern u32 gPS2GfxProfileCalls[GFX_PROFILE_COUNT];
extern volatile u32 gPS2DspCyclesTotal;
static inline u32 ps2_gfx_cycles(void) {
    u32 cycles;
    __asm__ volatile("mfc0 %0, $9" : "=r"(cycles) :: "memory");
    return cycles;
}
static inline u32 ps2_gfx_profile_enter(void) {
    if (!gPS2GfxProfileActive) return 0;
    u32 before, cycles, after;
    do {
        before = gPS2DspCyclesTotal;
        cycles = ps2_gfx_cycles();
        after = gPS2DspCyclesTotal;
    } while (before != after);
    return cycles - before;
}
static inline void ps2_gfx_profile_leave(unsigned category, u32 start) {
    if (gPS2GfxProfileActive) {
        gPS2GfxProfileCycles[category] += ps2_gfx_profile_enter() - start;
        gPS2GfxProfileCalls[category]++;
    }
}
void ps2_gfx_profile_begin(void);
void ps2_gfx_profile_end(void);
void ps2_gfx_profile_texture_miss(u32 source, u32 prim, u32 env,
    unsigned width, unsigned height, unsigned fmt, unsigned siz,
    unsigned reason, bool paired);
#endif
