#include <gsKit.h>
#include <gsInline.h>
#include <stdio.h>
#include <string.h>
#include "ps2_fps_overlay.h"
#include "ps2_fps_meter.h"

/* Small diagnostic font made of solid sprites. No asset reads or texture
 * cache pressure; horizontal runs reduce the number of rectangles. */
static const u8 glyphs[][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14}, {31,16,16,30,16,16,16}, /* F */
    {30,17,17,30,16,16,16}, /* P */ {15,16,16,14,1,1,30}, /* S */
    {0,0,0,31,0,0,0}, {0,0,0,0,0,12,12}, {0,21,14,31,14,21,0}
};
typedef struct { float x1, y1, x2, y2; } Rect;
static int glyph_index(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    switch (c) {
        case 'F': return 10; case 'P': return 11; case 'S': return 12;
        case '-': return 13; case '.': return 14; case '*': return 15;
        default: return -1;
    }
}
static void state(GSGLOBAL* gs, u64 test, u64 zbuf) {
    u64* p = gsKit_heap_alloc(gs, 2, 32, GIF_AD);
    p[0] = GIF_TAG_AD(2); p[1] = GIF_AD;
    p[2] = test; p[3] = GS_TEST_1 + gs->PrimContext;
    p[4] = zbuf; p[5] = GS_ZBUF_1 + gs->PrimContext;
}
static void sprites(GSGLOBAL* gs, const Rect* rects, unsigned count, u64 color) {
    if (!count) return;
    unsigned words = count * 3, qsize = 2 + (words + 1) / 2;
    u64* p = gsKit_heap_alloc(gs, qsize, qsize * 16, GIF_AD);
    p[0] = GIF_TAG_AD(1); p[1] = GIF_AD;
    p[2] = GS_SETREG_PRIM(GS_PRIM_PRIM_SPRITE, 0, 0, 0, 0, 0, 1, gs->PrimContext, 0);
    p[3] = GS_PRIM;
    p[4] = GIF_TAG(count, 1, 0, 0, GSKIT_GIF_FLG_REGLIST, 3);
    p[5] = GS_RGBAQ | ((u64)GS_XYZ2 << 4) | ((u64)GS_XYZ2 << 8);
    p += 6;
    for (unsigned i = 0; i < count; i++, p += 3) {
        p[0] = color;
        gs_xyz2 first = vertex_to_XYZ2(gs, rects[i].x1, rects[i].y1, 0);
        gs_xyz2 last = vertex_to_XYZ2(gs, rects[i].x2, rects[i].y2, 0);
        memcpy(p + 1, &first.xyz, 8); memcpy(p + 2, &last.xyz, 8);
    }
    if (words & 1) p[0] = 0;
}
void ps2_fps_overlay_draw(RDPState* rdp, unsigned fps10, bool fallback) {
    GSGLOBAL* gs = rdp->gs;
    if (!gs || rdp->renderTargetActive || gs->Width <= 0 || gs->Height <= 0) return;
    char label[16];
    if (fps10 == PS2_FPS_UNKNOWN) strcpy(label, "FPS --.-");
    else {
        if (fps10 > 9999) fps10 = 9999;
        snprintf(label, sizeof(label), "FPS %u.%u%s", fps10 / 10, fps10 % 10, fallback ? "*" : "");
    }
    float sx = gs->Width / 320.0f, sy = gs->Height / 240.0f;
    unsigned originX = rdp->scissorX0 / 4 + 2, originY = rdp->scissorY0 / 4 + 2;
    if (originX < 12) originX = 12;
    if (originY < 10) originY = 10;
    Rect rects[160]; unsigned count = 0;
    for (unsigned ch = 0; label[ch]; ch++) {
        int glyph = glyph_index(label[ch]);
        if (glyph < 0) continue;
        for (unsigned y = 0; y < 7; y++) for (unsigned x = 0; x < 5;) {
            if (!(glyphs[glyph][y] & (16u >> x))) { x++; continue; }
            unsigned left = x++;
            while (x < 5 && (glyphs[glyph][y] & (16u >> x))) x++;
            rects[count++] = (Rect){(originX + ch * 6 + left) * sx, (originY + y) * sy,
                (originX + ch * 6 + x) * sx, (originY + y + 1) * sy};
        }
    }
    GSTEST* t = gs->Test;
    u64 restoreTest = GS_SETREG_TEST(t->ATE,t->ATST,t->AREF,t->AFAIL,t->DATE,t->DATM,t->ZTE,t->ZTST);
    u64 restoreZ = GS_SETREG_ZBUF(gs->ZBuffer / 8192, gs->PSMZ & 15, !rdp->zWriteEnabled);
    /* Keep the existing scissor, and place the label inside letterboxing. */
    state(gs, GS_SETREG_TEST(0,0,0,0,0,0,1,1),
        GS_SETREG_ZBUF(gs->ZBuffer / 8192, gs->PSMZ & 15, 1));
    Rect background = {(originX - 2) * sx, (originY - 2) * sy,
        (originX + 1 + strlen(label) * 6) * sx, (originY + 9) * sy};
    sprites(gs, &background, 1, GS_SETREG_RGBA(0,0,0,128));
    sprites(gs, rects, count, GS_SETREG_RGBA(255,255,255,128));
    state(gs, restoreTest, restoreZ);
    /* PRIM has no shadow cache. Other cached RDP register values are restored
     * exactly, so the next task and its offscreen portrait remain unchanged. */
}
