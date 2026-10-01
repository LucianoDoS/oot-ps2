/* Real EE/GS checks for bugs observed in the title-screen capture. */
#include <gsKit.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ps2_types.h"
#include "rdp_to_gs.h"
#include "ps2_texture.h"
#include "ps2_background.h"
const u64 gspS2DEX2d_fifoTextStart[1] = {0};
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
    /* IA4 endpoints and nonzero CI4 palette placement. Use COPY so that
     * color-combiner state cannot hide incorrect decoder output. */
    rdp_setothermode(2u << 20, 0, &g_rdpState);
    static const u8 ia4[] = {0xf0};
    rdp_settile(0xf5600000, 0, &g_rdpState);
    rdp_settilesize(0xf2000000, 4u << 12, &g_rdpState);
    rdp_settimg(0xfd600001, (u32)ia4, &g_rdpState);
    rdp_loadtexture(0xf3, 0, 0, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    pixels = (u8*)g_rdpState.tile[0].gsTexture.Mem;
    if (pixels[0] != 255 || pixels[3] != 128 || pixels[4] != 0 || pixels[7] != 0) failures++;
    static const u8 palette0[] = {0xf8,0x01};
    static const u8 palette3[] = {0x07,0xc1};
    static const u8 ci4[] = {0};
    ps2_texture_load_tlut(palette0, 1);
    rdp_settile(0xf5100130, 7u << 24, &g_rdpState); /* TLUT entry 48. */
    rdp_settimg(0xfd100000, (u32)palette3, &g_rdpState);
    rdp_loadtlut(0xf0000000, 7u << 24, &g_rdpState);
    rdp_settile(0xf5400000, 3u << 20, &g_rdpState);
    rdp_settilesize(0xf2000000, 4u << 12, &g_rdpState);
    rdp_settimg(0xfd400001, (u32)ci4, &g_rdpState);
    rdp_loadtexture(0xf3, 0, 0, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    pixels = (u8*)g_rdpState.tile[0].gsTexture.Mem;
    if (pixels[0] != 0 || pixels[1] != 255 || pixels[2] != 0 || pixels[3] != 128) failures++;
    rdp_settile(0xf5400000, 0, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    if (pixels[0] != 255 || pixels[1] != 0) failures++;
    static u8 animated[] = {10,20,30,255, 10,20,30,255};
    rdp_settile(0xf5180000, 0, &g_rdpState);
    rdp_settimg(0xfd180001, (u32)animated, &g_rdpState);
    rdp_loadtexture(0xf3, 0, 0, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    ps2_texture_end_frame(&g_rdpState);
    animated[0] = 77;
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    pixels = (u8*)g_rdpState.tile[0].gsTexture.Mem;
    if (pixels[0] != 77 || pixels[1] != 20) failures++;
    u32 unchangedVram = g_rdpState.tile[0].gsTexture.Vram;
    rdp_setprimcolor(0, 0x12345678, &g_rdpState);
    rdp_setenvcolor(0, 0x87654321, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    if (g_rdpState.tile[0].gsTexture.Vram != unchangedVram) failures++;
    /* More tint states than LUT slots, then revisit old states with distinct
     * source pointers so a VRAM hit cannot hide a stale combiner lookup. */
    static u8 whites[48][8];
    memset(whites, 255, sizeof(whites));
    rdp_setothermode(0, 0, &g_rdpState);
    rdp_setcombine(0xfcffffff, 0xfffdf6fb, &g_rdpState); /* PRIMITIVE */
    for (int i = 0; i < 48; i++) {
        unsigned tint = 16 + i % 24;
        rdp_setprimcolor(0, (tint << 24) | 0x2030ff, &g_rdpState);
        rdp_settimg(0xfd180001, (u32)whites[i], &g_rdpState);
        rdp_loadtexture(0xf3, 0, 0, &g_rdpState);
        ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
        pixels = (u8*)g_rdpState.tile[0].gsTexture.Mem;
        if (pixels[0] != tint || pixels[1] != 32 || pixels[2] != 48 || pixels[3] != 128) failures++;
    }
    static u8 strips[32][4096];
    u32 stripVram[32];
    memset(strips, 255, sizeof(strips));
    rdp_setothermode(2u << 20, 0, &g_rdpState);
    rdp_settile(0xf5100000, 0, &g_rdpState);
    rdp_settilesize(0xf2000000, (124u << 12) | 252u, &g_rdpState);
    for (int pass = 0; pass < 2; pass++) for (int i = 0; i < 32; i++) {
        rdp_settimg(0xfd10001f, (u32)strips[i], &g_rdpState);
        rdp_loadtexture(0xf3, 0, 0, &g_rdpState);
        ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
        u32 address = g_rdpState.tile[0].gsTexture.Vram;
        if (!pass) stripVram[i] = address;
        else if (address != stripVram[i]) failures++;
    }
    /* A large upload may evict several strips, but must fit the same budget. */
    /* A keyboard needs more than 64 small glyph/color combinations. They
     * occupy 1 KiB each, rather than wasting a 4 KiB allocation per letter. */
    static u8 glyphs[128][128];
    u32 glyphVram[128];
    memset(glyphs, 255, sizeof(glyphs));
    rdp_settile(0xf5800000, 0, &g_rdpState); /* I4 */
    rdp_settilesize(0xf2000000, (60u << 12) | 60u, &g_rdpState);
    printf("[PS2 RENDER CHECK] glyph allocation=%u\n", (unsigned)gsKit_texture_size(16,16,GS_PSM_CT32));
    for (int pass = 0; pass < 2; pass++) for (int i = 0; i < 128; i++) {
        rdp_settimg(0xfd80000f, (u32)glyphs[i], &g_rdpState);
        rdp_loadtexture(0xf3, 0, 0, &g_rdpState);
        ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
        u32 address = g_rdpState.tile[0].gsTexture.Vram;
        if (!pass) glyphVram[i] = address;
        else if (address != glyphVram[i]) failures++;
    }
    rdp_settile(0xf5100000, 0, &g_rdpState);
    rdp_settilesize(0xf2000000, (1020u << 12) | 1020u, &g_rdpState);
    rdp_settimg(0xfd1000ff, (u32)strips, &g_rdpState);
    rdp_loadtexture(0xf3, 0, 0, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    if (g_rdpState.tile[0].gsTexture.Vram + 262144 > 0x400000) failures++;
    /* A logo fade changes vertex alpha while retaining its uploaded texels. */
    printf("[PS2 RENDER CHECK] failures before fade=%d\n", failures);
    rdp_setothermode(0, 0, &g_rdpState);
    rdp_setcombine(0xfc119623, 0xff2fffff, &g_rdpState);
    rdp_setprimcolor(0, 0xffffff80, &g_rdpState);
    if (!ps2_combiner_dynamic_alpha(&g_rdpState)) failures++;
    rdp_settile(0xf5180000, 0, &g_rdpState);
    rdp_settilesize(0xf2000000, 4u << 12, &g_rdpState);
    rdp_settimg(0xfd180001, (u32)animated, &g_rdpState);
    rdp_loadtexture(0xf3, 0, 0, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    pixels = (u8*)g_rdpState.tile[0].gsTexture.Mem;
    if (pixels[3] != 128) failures++;
    printf("[PS2 RENDER CHECK] fade dynamic=%d texalpha=%u\n", ps2_combiner_dynamic_alpha(&g_rdpState), pixels[3]);
    u32 fadeVram = g_rdpState.tile[0].gsTexture.Vram;
    RSPVertex fading = {.cw=1};
    GSPRIMSTQPOINT fadedPoint;
    ps2_texture_build_vertex(&g_rdpState.tile[0], &g_rspState, &g_rdpState,
        &fading, 0x80808080, 0, &fadedPoint);
    if (fadedPoint.rgbaq.color.components.a != 64) failures++;
    printf("[PS2 RENDER CHECK] fade vertex alpha=%u\n", fadedPoint.rgbaq.color.components.a);
    rdp_setprimcolor(0, 0xffffff00, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    if (g_rdpState.tile[0].gsTexture.Vram != fadeVram) failures++;
    ps2_texture_build_vertex(&g_rdpState.tile[0], &g_rspState, &g_rdpState,
        &fading, 0x80808080, 0, &fadedPoint);
    if (fadedPoint.rgbaq.color.components.a != 0) failures++;
    /* Primitive alpha used in RGB, or a second cycle, stays on the CPU path. */
    rdp_setcombine((0xfc119623 & ~(31u << 15)) | (10u << 15), 0xff2fffff, &g_rdpState);
    if (ps2_combiner_dynamic_alpha(&g_rdpState)) failures++;
    rdp_setcombine(0xfc119623, 0xff2fffff, &g_rdpState);
    rdp_setothermode(1u << 20, 0, &g_rdpState);
    if (ps2_combiner_dynamic_alpha(&g_rdpState)) failures++;
    rdp_setcombine(0, 0, &g_rdpState);
    printf("[PS2 RENDER CHECK] failures after fade=%d\n", failures);
    /* Runtime S2DEX rectangles have native layout and retain RDP command
     * dispatch. A 320x240 background must not exceed the conversion scratch. */
    static u8 roomImage[320 * 240 * 2];
    for (unsigned i = 0; i < sizeof(roomImage); i += 2) { roomImage[i] = 0xf8; roomImage[i + 1] = 1; }
    static PS2Background background = {.imageW=320*4, .imageH=240*4, .frameW=320*4, .frameH=240*4,
        .imagePtr=roomImage, .imageSiz=G_IM_SIZ_16b, .imageFmt=G_IM_FMT_RGBA, .scaleW=1024, .scaleH=1024};
    u32 backgroundList[][2] = {
        {0xdd00038f, (u32)gspS2DEX2d_fifoTextStart},
        {0xef200000, 0}, {0x0a000000, (u32)&background},
        {0xdd000fff, 0x123456}, {0xdf000000, 0}
    };
    u32 beforeDraw = g_rdpState.stats.submitted, beforeBackground = g_rdpState.stats.backgrounds;
    rdp_to_gs_exec((const Gfx*)backgroundList);
    printf("[PS2 RENDER CHECK] background delta=%u draws=%u source=%p\n",
           g_rdpState.stats.backgrounds - beforeBackground, g_rdpState.stats.submitted - beforeDraw, roomImage);
    if (g_rdpState.stats.backgrounds != beforeBackground + 1 ||
        g_rdpState.stats.submitted != beforeDraw + 8) failures++;
    pixels = (u8*)g_rdpState.tile[0].gsTexture.Mem;
    if (pixels[0] != 255 || pixels[1] != 0 || pixels[2] != 0 || pixels[3] != 128) failures++;
    TileDescriptor repeating = {.width=32, .height=16, .masks=5, .cmt=2};
    RSPVertex mapped = {.cw=2, .tc_s=4096, .tc_t=-512, .sx=10, .sy=20};
    GSPRIMSTQPOINT point;
    g_rspState.texScaleS = g_rspState.texScaleT = 1;
    rdp_setothermode(1u << 19, 0, &g_rdpState);
    ps2_texture_apply_tile(&repeating, &g_rdpState);
    ps2_texture_build_vertex(&repeating, &g_rspState, &g_rdpState, &mapped, 0x80706050, 123, &point);
    if (g_rdpState.gs->Clamp->WMS != 3 || g_rdpState.gs->Clamp->MINU != 31 ||
        g_rdpState.gs->Clamp->WMT != 2 || g_rdpState.gs->Clamp->MAXV != 15 ||
        point.rgbaq.color.q != 0.5f || point.stq.st.s != 2.0f || point.stq.st.t != -0.5f ||
        point.xyz2.xyz.z != 123 || point.rgbaq.color.components.r != 0x50) failures++;
    repeating.shifts = 2; repeating.shiftt = 11;
    ps2_texture_build_vertex(&repeating, &g_rspState, &g_rdpState, &mapped, 0, 0, &point);
    if (point.stq.st.s != 0.5f || point.stq.st.t != -16.0f) failures++;
    rdp_setothermode(0, 0, &g_rdpState);
    ps2_texture_build_vertex(&repeating, &g_rspState, &g_rdpState, &mapped, 0, 0, &point);
    if (point.rgbaq.color.q != 1.0f || point.stq.st.s != 1.0f) failures++;
    RSPState reflection = g_rspState;
    memset(reflection.modelview, 0, sizeof(reflection.modelview));
    reflection.modelview[0] = reflection.modelview[5] = reflection.modelview[10] = reflection.modelview[15] = 1;
    reflection.geometryMode = 0x00060000; /* lighting + spherical texture generation */
    static const struct { s16 position[3]; u16 flag; s16 uv[2]; u8 normal[4]; } normal =
        {{0,0,0}, 0, {123,456}, {127,0,0,255}};
    f3dex2_vtx(0x01001002, (u32)&normal, &reflection);
    if (reflection.vtx[0].tc_s != 32767 || reflection.vtx[0].tc_t != 16383) failures++;
    reflection.geometryMode |= 0x00080000; /* linear generation */
    f3dex2_vtx(0x01001002, (u32)&normal, &reflection);
    if (reflection.vtx[0].tc_s != 0 || abs(reflection.vtx[0].tc_t - 16383) > 1) failures++;
    reflection.geometryMode = 0x00060000;
    reflection.modelview[0] = reflection.modelview[5] = 0;
    reflection.modelview[1] = 1; reflection.modelview[4] = -1;
    f3dex2_vtx(0x01001002, (u32)&normal, &reflection);
    if (reflection.vtx[0].tc_s != 16383 || reflection.vtx[0].tc_t != 32767) failures++;
    static const u8 lookY[16] = {0,0,0,0,0,0,0,0,0,127,0};
    f3dex2_movemem(0xdc00000a, (u32)lookY, &reflection);
    f3dex2_vtx(0x01001002, (u32)&normal, &reflection);
    if (reflection.vtx[0].tc_s != 32767 || reflection.lookAt[0][1] != 1) failures++;
    RSPState fogState = g_rspState;
    fogState.geometryMode = 0x00010000;
    /* gSPFogPosition(500,1000): Z/W=0 starts fog, 0.5 is half, 1 is full. */
    f3dex2_moveword(0xdb080000, 256u << 16, &fogState);
    if (ps2_fog_calculate_factor(-1, &fogState) != 255 ||
        ps2_fog_calculate_factor(0, &fogState) != 255 ||
        ps2_fog_calculate_factor(0.5f, &fogState) != 127 ||
        ps2_fog_calculate_factor(1, &fogState) != 0) failures++;
    gs_xyz2 fogVertex = point.xyz2;
    u64 packedFog;
    ps2_fog_apply_xyz(&fogVertex, 0.5f, &fogState);
    memcpy(&packedFog, &fogVertex.xyz, 8);
    if (fogVertex.tag != GS_XYZF2 || (packedFog >> 56) != 127 ||
        (u32)(packedFog >> 32) % 0x1000000 != point.xyz2.xyz.z ||
        fogVertex.xyz.x != point.xyz2.xyz.x || fogVertex.xyz.y != point.xyz2.xyz.y) failures++;
    rdp_setothermode(1u << 20, 0xc8112078u, &g_rdpState); /* FOG_SHADE_A + opaque cycle 2 */
    if (!ps2_fog_enabled(&g_rdpState, &fogState)) failures++;
    fogState.geometryMode = 0;
    if (ps2_fog_enabled(&g_rdpState, &fogState) ||
        ps2_fog_calculate_factor(1, &fogState) != 255) failures++;
    fogState.geometryMode = 0x00010000;
    rdp_setothermode(0, 0xc8112078u, &g_rdpState);
    if (ps2_fog_enabled(&g_rdpState, &fogState)) failures++;
    /* A very narrow room fog range must not use the old generic 0.2..1 curve. */
    f3dex2_moveword(0xdb080000, (16000u << 16) | (u16)-15744, &fogState);
    if (ps2_fog_calculate_factor(0.98f, &fogState) != 255 ||
        ps2_fog_calculate_factor(1, &fogState) != 0) failures++;
    printf("[PS2 RENDER TEST] %s: matrices, viewport, othermode, textures and palettes (%d)\n",
           failures ? "FAIL" : "PASS", failures);
    return failures;
}
