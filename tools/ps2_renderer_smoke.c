/* Real EE/GS checks for bugs observed in the title-screen capture. */
#include <gsKit.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ps2_types.h"
#include "rdp_to_gs.h"
#include "ps2_texture.h"
#include "f3dex2_ps2.h"

static void matrix(short* out, float scale, float translate) {
    memset(out, 0, 64);
    for (int i = 0; i < 16; i++) {
        float value = (i == 0 || i == 5 || i == 10) ? scale : (i == 15 ? 1 : 0);
        if (i == 12) value = translate;
        int fixed = (int)(value * 65536.0f);
        out[i] = fixed >> 16;
        out[i + 16] = fixed & 0xffff;
    }
}

int ps2_renderer_smoke(void) {
    int failures = 0;
    const u8 texel[4] = {128, 64, 32, 200}, shade[4] = {80, 100, 120, 140};
    u8 combined[4];
    rdp_setcombine(0xfcffffff, 0xfffdf6fb, &g_rdpState); /* PRIMITIVE */
    rdp_setprimcolor(0, 0x10203000, &g_rdpState);
    ps2_combiner_color(&g_rdpState, texel, shade, combined);
    if (combined[0] != 16 || combined[1] != 32 || combined[2] != 48 || combined[3] != 0) failures++;
    rdp_setprimcolor(0, 0x10203080, &g_rdpState);
    ps2_combiner_color(&g_rdpState, texel, shade, combined);
    if (combined[3] != 128) failures++;
    rdp_setcombine(0xfcffffff, 0xfffe793c, &g_rdpState); /* SHADE */
    ps2_combiner_color(&g_rdpState, texel, shade, combined);
    if (memcmp(combined, shade, 4)) failures++;
    rdp_setothermode(2u << 20, 0, &g_rdpState); /* COPY bypass */
    ps2_combiner_color(&g_rdpState, texel, shade, combined);
    if (memcmp(combined, texel, 4)) failures++;
    rdp_setcombine(0, 0, &g_rdpState);
    rdp_setothermode(0, 0, &g_rdpState);
    static short projection[32] __attribute__((aligned(16)));
    static short view[32] __attribute__((aligned(16)));
    static short model[32] __attribute__((aligned(16)));
    static short viewport[8] = {640, 480, 0, 0, 640, 480, 0, 0};
    static short vertex[8] = {1, 1, 0, 0, 0, 0, -1, -1};
    matrix(projection, 0.125f, 0);
    matrix(view, 1, 2);
    matrix(model, 1, 3);
    f3dex2_mtx(0xDA380007, (u32)projection, &g_rspState);
    f3dex2_mtx(0xDA380005, (u32)view, &g_rspState);
    f3dex2_mtx(0xDA380003, (u32)model, &g_rspState);
    f3dex2_movemem(0xDC080008, (u32)viewport, &g_rspState);
    f3dex2_vtx(0x01001002, (u32)vertex, &g_rspState);
    RSPVertex* v = &g_rspState.vtx[0];
    printf("[PS2 RENDER CHECK] clip=%f,%f screen=%f,%f\n", v->cx, v->cy, v->sx, v->sy);
    if (fabsf(v->cx - 0.75f) > 0.001f || fabsf(v->cy - 0.125f) > 0.001f ||
        fabsf(v->sx - 560.0f) > 0.01f || fabsf(v->sy - 196.0f) > 0.01f) failures++;
    RSPVertex a = {.cx=-0.5f,.cy=-0.5f,.cw=1,.ca=255};
    RSPVertex b = {.cx=0.5f,.cy=-0.5f,.cw=1,.ca=255};
    RSPVertex c = {.cx=0,.cy=0.5f,.cz=-2,.cw=1,.ca=255};
    RSPVertex clipped[12];
    int count = ps2_clip_triangle(&a, &b, &c, clipped);
    if (count != 4) failures++;
    for (int i = 0; i < count; i++)
        if (clipped[i].cw <= 0 || fabsf(clipped[i].cz) > clipped[i].cw + 0.0001f ||
            clipped[i].ca != 255) failures++;
    c.cz = -3; a.cz = b.cz = -2;
    if (ps2_clip_triangle(&a, &b, &c, clipped) != 0) failures++;
    rdp_setothermode_h(0xE3000A01, 3u << 20, &g_rdpState);
    if ((g_rdpState.otherMode.hi & (3u << 20)) != (3u << 20)) failures++;
    rdp_setothermode_l(0xE200001C, 0x10, &g_rdpState);
    printf("[PS2 RENDER CHECK] mode=%08x,%08x z=%u\n", g_rdpState.otherMode.hi,
           g_rdpState.otherMode.lo, g_rdpState.gs->Test->ZTST);
    if (g_rdpState.otherMode.lo != 0x10 || g_rdpState.gs->Test->ZTST != 2) failures++;
    /* Real GBL_c1/c2 encodings: opaque coverage must preserve source RGB,
     * and a two-cycle fog+translucent mode must use the final blender. */
    rdp_setothermode(0, (1u << 22) | (1u << 18) | 0x30, &g_rdpState);
    printf("[PS2 RENDER CHECK] opaque alpha=%016llx expected=%016llx\n",
        (unsigned long long)g_rdpState.gs_alpha,
        (unsigned long long)GS_SETREG_ALPHA(0, 2, 2, 2, 0x80));
    if (g_rdpState.gs_alpha != (GS_SETREG_ALPHA(0, 2, 2, 2, 0x80))) failures++;
    if (!g_rdpState.zWriteEnabled) failures++;
    rdp_setothermode(1u << 20, (3u << 30) | (2u << 26) | (1u << 20) | 0x4000,
                     &g_rdpState);
    printf("[PS2 RENDER CHECK] two-cycle alpha=%016llx expected=%016llx\n",
        (unsigned long long)g_rdpState.gs_alpha,
        (unsigned long long)GS_SETREG_ALPHA(0, 1, 0, 1, 0x80));
    if (g_rdpState.gs_alpha != (GS_SETREG_ALPHA(0, 1, 0, 1, 0x80))) failures++;
    if (g_rdpState.zWriteEnabled) failures++;
    static const u8 texture[] = {255,255, 255,255, 255,255, 255,255};
    for (int i = 0; i < 2; i++) {
        TileDescriptor* tile = &g_rdpState.tile[i];
        tile->addr = (void*)texture;
        tile->width = tile->height = 2;
        tile->fmt = G_IM_FMT_RGBA;
        tile->siz = G_IM_SIZ_16b;
        ps2_texture_upload(tile, &g_rdpState);
        printf("[PS2 RENDER CHECK] tile=%d loaded=%d vram=%08x floor=%08x\n",
               i, tile->gsTextureLoaded, tile->gsTexture.Vram, g_rdpState.gs->TexturePointer);
        if (!tile->gsTextureLoaded || tile->gsTexture.Vram < g_rdpState.gs->TexturePointer ||
            tile->gsTexture.Vram == 0) failures++;
    }
    if (!g_rdpState.tile[0].gsTextureLoaded ||
        g_rdpState.tile[0].gsTexture.Vram != g_rdpState.tile[1].gsTexture.Vram ||
        g_rdpState.tile[1].gsTexture.TBW != 1) failures++;
    ps2_texture_end_frame(&g_rdpState);
    if (g_rdpState.tile[0].gsTextureLoaded) failures++;
    static const u8 rows[] = {0,0,0,255, 1,0,0,255, 2,0,0,255, 3,0,0,255,
                             4,0,0,255, 5,0,0,255, 6,0,0,255, 7,0,0,255};
    static const u8 nextRows[] = {20,0,0,255, 21,0,0,255, 22,0,0,255, 23,0,0,255,
                                 24,0,0,255, 25,0,0,255, 26,0,0,255, 27,0,0,255};
    rdp_settile(0xF5180000, 0, &g_rdpState);
    rdp_settile(0xF5180000, 7u << 24, &g_rdpState);
    rdp_settilesize(0xF2000000, (4u << 12) | 4, &g_rdpState);
    rdp_settimg(0xFD180003, (u32)rows, &g_rdpState);
    rdp_loadtexture(0xF4, 0xF4004000, 7u << 24, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    u8* pixels = (u8*)g_rdpState.tile[0].gsTexture.Mem;
    if (pixels[0] != 1 || pixels[8] != 5) failures++;
    rdp_settimg(0xFD180003, (u32)nextRows, &g_rdpState);
    rdp_loadtexture(0xF4, 0xF4004000, 7u << 24, &g_rdpState);
    if (g_rdpState.tile[0].gsTextureLoaded) failures++;
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    if (pixels[0] != 21 || pixels[8] != 25) failures++;
    rdp_settile(0xF5180100, 7u << 24, &g_rdpState);
    rdp_settimg(0xFD180003, (u32)rows, &g_rdpState);
    rdp_loadtexture(0xF3, 0, 7u << 24, &g_rdpState);
    if (g_rdpState.textureLoads[0].source != nextRows + 4 ||
        g_rdpState.textureLoads[256].source != rows) failures++;
    printf("[PS2 RENDER TEST] %s: matrices, viewport, othermode, texture VRAM (%d)\n",
           failures ? "FAIL" : "PASS", failures);
    return failures;
}
