/* Generate real game display lists for the native framebuffer strip path. */
#include "gfx.h"
#include "prerender.h"
extern int printf(const char*, ...);

static u8 image[320 * 240 * 2] __attribute__((aligned(128)));
static u8 coverage[320 * 240] __attribute__((aligned(128)));
static Gfx commands[1024];
static int failures;

const void* ps2_prerender_fixture(int variant) {
    PreRender pre = {0};
    Gfx* end = commands;
    for (int i = 0; i < 320 * 240; i++) {
        image[2*i] = ((i / 320) & 0xf8) ^ (variant << 5);
        image[2*i+1] = ((i % 320) & 0x3e) | 1;
        coverage[i] = 0xff;
    }
    PreRender_SetValues(&pre, 320, 240, image, NULL);
    PreRender_SetValuesSave(&pre, 320, 240, image, NULL, coverage);
    if (variant == 0) PreRender_RestoreFramebuffer(&pre, &end);
    if (variant == 1) func_800C213C(&pre, &end);
    if (variant == 2) func_800C170C(&pre, &end, image, image, 128, 192, 255, 255);
    if (variant == 3) PreRender_CopyImageRegion(&pre, &end);
    int loads = 0, rects = 0, nextRow = 0;
    for (Gfx* cmd = commands; cmd < end; cmd++) {
        u32 op = cmd->words.w0 >> 24;
        if (op == G_LOADTILE) {
            int first = (cmd->words.w0 & 0xfff) / 4;
            int last = (cmd->words.w1 & 0xfff) / 4;
            if (first < 0 || last >= 240 || last - first + 1 > 32) failures++;
            loads++;
        }
        if (op == G_TEXRECT) {
            int first = (cmd->words.w1 & 0xfff) / 4;
            int last = (cmd->words.w0 & 0xfff) / 4;
            if (variant == 0 || variant == 3) last++;
            if (first != nextRow || last <= first || last > 240) failures++;
            nextRow = last;
            rects++;
        }
    }
    if (rects != 8 || nextRow != 240 || loads != (variant == 1 ? 16 : 8)) failures++;
    printf("[PS2 PRERENDER DL TEST] variant=%d rects=%d loads=%d rows=%d failures=%d\n",
           variant, rects, loads, nextRow, failures);
    gSPEndDisplayList(end++);
    return commands;
}

void ps2_prerender_mutate(void) { image[0] ^= 0x80; }
int ps2_prerender_failures(void) { return failures; }
