/* Hardware diagnostic using the port's actual F3DEX2 renderer. No ROM needed. */
#include <kernel.h>
#include <sifrpc.h>
#include <dmaKit.h>
#include <gsKit.h>
#include <stdio.h>
#include "ps2_types.h"
#include "rdp_to_gs.h"
#include "ps2_segment.h"

uintptr_t gSegments[16];
extern int ps2_os_smoke(void);
extern int ps2_queue_heap_smoke(void);
extern int ps2_math_smoke(void);
extern int ps2_printf_smoke(void);
extern int ps2_rom_smoke(void);
extern int ps2_rom_threads_smoke(void);
extern int ps2_renderer_smoke(void);
extern int ps2_audio_smoke(void);
extern int ps2_audio_driver_smoke(void);
extern int ps2_audio_heap_smoke(void);
extern int ps2_sram_smoke(void);
extern int ps2_font_smoke(void);
extern int ps2_jpeg_smoke(void);

typedef struct {
    s16 x, y, z;
    u16 flag;
    s16 s, t;
    u8 r, g, b, a;
} SmokeVertex;
_Static_assert(sizeof(SmokeVertex) == 16, "N64 vertex layout");

static const SmokeVertex vertices[] __attribute__((aligned(16))) = {
    { -1, -1, 0, 0, 0, 0, 255, 32, 32, 255 },
    {  1, -1, 0, 0, 0, 0, 32, 255, 32, 255 },
    {  0,  1, 0, 0, 0, 0, 32, 64, 255, 255 },
};

int main(void) {
    printf("[PS2 SMOKE] Entered main\n");
    fflush(stdout);
    SifInitRpc(0);
    if (ps2_jpeg_smoke()) {
        fflush(stdout);
        SleepThread();
        return 1;
    }
    if (ps2_font_smoke()) {
        fflush(stdout);
        SleepThread();
        return 1;
    }
    if (ps2_sram_smoke()) {
        fflush(stdout);
        SleepThread();
        return 1;
    }
    if (ps2_audio_heap_smoke()) {
        fflush(stdout);
        SleepThread();
        return 1;
    }
    if (ps2_audio_driver_smoke()) { SleepThread(); return 1; }
    if (ps2_audio_smoke()) {
        fflush(stdout);
        SleepThread();
        return 1;
    }
    int mathResult = ps2_math_smoke();
    printf("[PS2 MATH TEST] %s: native matrix layout and trigonometry (%d)\n",
           mathResult == 0 ? "PASS" : "FAIL", mathResult);
    fflush(stdout);
    if (mathResult != 0) {
        SleepThread();
        return 1;
    }
    int printfResult = ps2_printf_smoke();
    printf("[PS2 PRINTF TEST] %s: game varargs and callback (%d)\n",
           printfResult == 0 ? "PASS" : "FAIL", printfResult);
    fflush(stdout);
    if (printfResult != 0) {
        SleepThread();
        return 1;
    }
    if (ps2_os_smoke() != 0) {
        SleepThread();
        return 1;
    }
    if (ps2_queue_heap_smoke() != 0) {
        SleepThread();
        return 1;
    }
    int romResult = ps2_rom_smoke();
    printf("[PS2 ROM TEST] %s: file DMA, boundaries and endian (%d)\n",
           romResult == 0 ? "PASS" : "FAIL", romResult);
    fflush(stdout);
    if (romResult != 0) {
        SleepThread();
        return 1;
    }
    int threadsResult = ps2_rom_threads_smoke();
    printf("[PS2 ROM THREAD TEST] %s: two concurrent readers (%d)\n",
           threadsResult == 0 ? "PASS" : "FAIL", threadsResult);
    fflush(stdout);
    if (threadsResult != 0) {
        SleepThread();
        return 1;
    }
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    GSGLOBAL* gs = gsKit_init_global();
    if (!gs) return 1;
    gs->Mode = GS_MODE_NTSC;
    gs->Interlace = GS_INTERLACED;
    gs->Field = GS_FIELD;
    gs->Width = 640;
    gs->Height = 448;
    gs->PSM = GS_PSM_CT24;
    gs->PSMZ = GS_PSMZ_24;
    gs->ZBuffering = GS_SETTING_ON;
    gs->DoubleBuffering = GS_SETTING_ON;
    gsKit_init_screen(gs);
    printf("[PS2 SMOKE] GS initialized\n");
    fflush(stdout);
    gsKit_mode_switch(gs, GS_ONESHOT);
    rdp_to_gs_init(gs);
    if (ps2_renderer_smoke()) {
        fflush(stdout);
        SleepThread();
    }
    rdp_to_gs_init(gs);
    gSegments[6] = 0x123456; /* CPU mapping must survive RSP commands. */
    g_rspState.vpScaleX = 180;
    g_rspState.vpScaleY = -160;
    g_rspState.vpTransX = 320;
    g_rspState.vpTransY = 224;
    /* Native words, same layout as a runtime-generated game display list. */
    const u32 commands[][2] __attribute__((aligned(8))) = {
        { 0xDB060018, (u32)(uintptr_t)vertices }, /* RSP segment 6 */
        { 0xD9FFFFFF, 0x00000004 }, /* G_SHADE */
        { 0x01003006, 0x06000000 }, /* G_VTX: three vertices, segment 6 */
        { 0x05000204, 0 },          /* G_TRI1: indices 0, 1, 2 */
        { 0xDF000000, 0 },          /* G_ENDDL */
    };
    printf("[PS2 SMOKE] Starting real F3DEX2 display-list test\n");
    fflush(stdout);
    for (unsigned frame = 0; ; ++frame) {
        gsKit_clear(gs, GS_SETREG_RGBAQ(12, 18, 30, 128, 0));
        rdp_to_gs_exec((const Gfx*)commands);
        if (gSegments[6] != 0x123456 || gRspSegments[6] != (uintptr_t)vertices) {
            printf("[PS2 SMOKE] FAIL: independent CPU/RSP segment tables\n");
            fflush(stdout);
            SleepThread();
        }
        /* A moving marker makes a frozen frame distinguishable. */
        float x = 20 + (frame % 560);
        gsKit_prim_sprite(gs, x, 410, x + 16, 426, 1,
                          GS_SETREG_RGBAQ(255, 255, 255, 128, 0));
        rdp_to_gs_flip();
        if (frame == 119) {
            printf("[PS2 SEGMENT TEST] PASS: independent CPU/RSP mappings\n");
            printf("[PS2 SMOKE] PASS: 120 frames submitted\n");
            fflush(stdout);
        }
    }
}
