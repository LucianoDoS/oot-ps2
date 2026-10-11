/* Differential EE draw oracle for constant-only texture retention. */
#include <stdio.h>
#include <string.h>
#include "ps2_texture.h"
#include "f3dex2_ps2.h"

static RDPState s_savedRdp;
static RSPState s_savedRsp;
static u8 s_source[32] __attribute__((aligned(16)));
static GSPRIMSTQPOINT s_reference[6];

static unsigned point_alpha(const GSPRIMSTQPOINT* point) {
    u32 rgba;
    memcpy(&rgba,&point->rgbaq,sizeof(rgba));
    return rgba >> 24;
}

static void encode(unsigned cycle, const u8 rgb[4], const u8 alpha[4]) {
    const u8 rgb1[4] = {0,15,4,7}, alpha1[4] = {7,7,7,0};
    u32 w0 = 0xfc000000u | ((u32)rgb[0]<<20) | ((u32)rgb[2]<<15) |
        ((u32)alpha[0]<<12) | ((u32)alpha[2]<<9) | ((u32)rgb1[0]<<5) | rgb1[2];
    u32 w1 = ((u32)rgb[1]<<28) | ((u32)rgb1[1]<<24) | ((u32)alpha1[0]<<21) |
        ((u32)alpha1[2]<<18) | ((u32)rgb[3]<<15) | ((u32)alpha[1]<<12) |
        ((u32)alpha[3]<<9) | ((u32)rgb1[3]<<6) | ((u32)alpha1[1]<<3) | alpha1[3];
    rdp_setothermode((1u<<19)|(cycle<<20),0x30u,&g_rdpState);
    rdp_setcombine(w0,w1,&g_rdpState);
}

static void material(unsigned fixture) {
    const u8 shadeRgb[4] = {1,15,4,7}, shadeAlpha[4] = {1,7,4,7};
    const u8 dynamicAlpha[4] = {1,7,3,7}, primRgb[4] = {7,7,7,3};
    const u8 envRgb[4] = {7,7,7,5}, envAlpha[4] = {7,7,7,5};
    const u8 primAlphaRgb[4] = {1,15,10,7}, envAlphaRgb[4] = {1,15,12,7};
    const u8 mixedRgb[4] = {3,5,1,5};
    if (fixture == 2) encode(0,shadeRgb,dynamicAlpha);
    else if (fixture == 3 || fixture == 4) encode(0,primRgb,shadeAlpha);
    else if (fixture == 5 || fixture == 6) encode(0,envRgb,shadeAlpha);
    else if (fixture == 7) encode(0,primAlphaRgb,shadeAlpha);
    else if (fixture == 8) encode(0,envAlphaRgb,shadeAlpha);
    else if (fixture == 9) encode(0,shadeRgb,envAlpha);
    else if (fixture == 10 || fixture == 11) encode(1,mixedRgb,shadeAlpha);
    else encode(0,shadeRgb,shadeAlpha);
    if (fixture >= 19 && fixture <= 22) {
        static const u32 pairs[][2] = {
            {0xfc262a04u,0x1f5893f8u}, {0x00272c60u,0x350ce37fu},
            {0xfc171c60u,0x35fd6578u}, {0x00272c60u,0x350c937fu}
        };
        rdp_setothermode((1u<<19)|(1u<<20),0,&g_rdpState);
        rdp_setcombine(pairs[fixture-19][0],pairs[fixture-19][1],&g_rdpState);
    }
}

static void draw(void) {
    f3dex2_batch_begin(&g_rspState,&g_rdpState);
    f3dex2_tri1(0x05000204u,0,&g_rspState,&g_rdpState);
    f3dex2_batch_end(&g_rdpState);
}

static void mutate(unsigned fixture) {
    u32 prim = g_rdpState.primColor, env = g_rdpState.envColor;
    if (fixture == 13) {
        /* Restored state changes selectors without the usual SETCOMBINE
         * invalidation hook. The loaded cache entry must still be checked. */
        g_rdpState.combiner.w0 ^= 1u<<20;
    } else if (fixture == 14) {
        rdp_setothermode(g_rdpState.otherMode.hi | (1u<<20),g_rdpState.otherMode.lo,&g_rdpState);
    } else if (fixture == 15 || fixture == 16) {
        rdp_setothermode(g_rdpState.otherMode.hi,
            g_rdpState.otherMode.lo | (fixture == 15 ? 0x4000u : 1u),&g_rdpState);
    } else if (fixture == 17) g_rdpState.tile[0].cacheSlot = 0xffffu;
    else if (fixture == 18) g_rdpState.tile[0].gsTexture.Vram ^= 1024u;
    else if (fixture == 23) g_rdpState.tile[0].gsTextureLoaded = false;
    if (fixture == 24) rdp_setprimcolor(0,prim,&g_rdpState);
    else if (fixture == 12) rdp_setprimcolor(1,prim,&g_rdpState);
    else if (fixture == 1 || fixture == 5 || fixture == 6 || fixture == 8 || fixture == 9 || fixture == 11) {
        env ^= (fixture == 6 || fixture == 8 || fixture == 9) ? 0xffu : 0xffffff00u;
        rdp_setenvcolor(0,env,&g_rdpState);
    } else {
        prim ^= (fixture == 2 || fixture == 4 || fixture == 7) ? 0xffu : 0xffffff00u;
        rdp_setprimcolor(0,prim,&g_rdpState);
    }
}

int ps2_color_invalidation_smoke(void) {
    s_savedRdp = g_rdpState; s_savedRsp = g_rspState;
    unsigned failures = 0, compared = 0, retained = 0, invalidated = 0;
    for (unsigned i=0;i<sizeof(s_source);i++) s_source[i] = i*73u+19u;
    for (unsigned fixture=0;fixture<25;fixture++) {
        unsigned referenceCount = 0;
        GSTEXTURE referenceTexture;
        memset(&referenceTexture,0,sizeof(referenceTexture));
        for (unsigned path=0;path<2;path++) {
            g_rdpState=s_savedRdp; g_rspState=s_savedRsp;
            ps2_texture_test_color_invalidation_reference(path==0);
            ps2_gs_state_reset();
            memset(g_rdpState.tile,0,sizeof(g_rdpState.tile));
            material(fixture);
            rdp_setprimcolor(0,0x193d8fa7u,&g_rdpState);
            rdp_setenvcolor(0,0xd26b4753u,&g_rdpState);
            g_rdpState.textureTile=0;
            g_rdpState.tile[0]=(TileDescriptor){.addr=s_source,.width=4,.height=4,
                .fmt=G_IM_FMT_RGBA,.siz=G_IM_SIZ_16b,.masks=2,.maskt=2,.tmem=0};
            /* The four pair fixtures verify conservative invalidation of
             * their combiner class; actual pair conversion is covered by
             * the flame/token/jewel/path pixel oracles. Never inherit a
             * second tile/source from the preceding renderer test. */
            g_rdpState.tile[1].tmem=256;
            g_rdpState.textureLoads[0].source=s_source;
            g_rdpState.textureLoads[0].stride=0;
            g_rdpState.textureLoads[256].source=NULL;
            g_rspState.texEnabled=true; g_rspState.texTile=0;
            g_rspState.texScaleS=0.75f; g_rspState.texScaleT=0.5f; g_rspState.geometryMode=0;
            for (unsigned i=0;i<3;i++) g_rspState.vtx[i]=(RSPVertex){
                .valid=true,.cw=2,.cx=(int)i-1,.cy=i==1?0.5f:-0.5f,.cz=0.25f*i,
                .sz=0.25f*i,.sx=100+i*10,.sy=i==1?110:100,
                .tc_s=(int)i*321-71,.tc_t=(int)i*157-321,
                .cr=29+i*91,.cg=229-i*73,.cb=53+i*71,.ca=83+i*63};
            f3dex2_test_capture(true);
            draw();
            failures += !g_rdpState.tile[0].gsTextureLoaded;
            ps2_texture_color_invalidation_stats(true);
            mutate(fixture);
            bool shouldRetain = fixture==0 || fixture==1 || fixture==2 || fixture==4 || fixture==6 || fixture==24;
            if (path) {
                failures += g_rdpState.tile[0].gsTextureLoaded != shouldRetain;
                PS2TextureColorInvalidationStats stats=ps2_texture_color_invalidation_stats(false);
                retained += stats.retained; invalidated += stats.invalidated;
                if (fixture==23 || fixture==24) failures += stats.retained || stats.invalidated || stats.forced;
                else failures += stats.retained != shouldRetain || stats.invalidated != !shouldRetain;
            }
            draw();
            unsigned count;
            const GSPRIMSTQPOINT* points=f3dex2_test_points(&count);
            failures += count!=6 || !g_rdpState.tile[0].gsTextureLoaded;
            if (!path) {
                referenceCount=count;
                if (count==6) memcpy(s_reference,points,sizeof(s_reference));
                referenceTexture=g_rdpState.tile[0].gsTexture;
            } else {
                if (count==6 && referenceCount==6) failures += memcmp(s_reference,points,sizeof(s_reference))!=0;
                else failures++;
                failures += memcmp(&referenceTexture,&g_rdpState.tile[0].gsTexture,sizeof(referenceTexture))!=0;
                compared += count;
            }
            if (fixture==2 && count==6) failures += point_alpha(&points[0])==point_alpha(&points[3]);
            f3dex2_test_capture(false);
        }
    }
    ps2_texture_test_color_invalidation_reference(false);
    g_rdpState=s_savedRdp; g_rspState=s_savedRsp;
    /* The fixture issued real uploads, so restored descriptors require the
     * same revalidation as the next normal frame. */
    ps2_texture_end_frame(&g_rdpState);
    ps2_gs_state_reset(); rdp_apply_othermode(&g_rdpState);
    printf("[PS2 COLOR INVALIDATION TEST] cases=25 points=%u retained=%u invalidated=%u used/unused constants, dynamic alpha, LOD, four pairs, restored selectors/modes and stale slots failures=%u\n",
        compared,retained,invalidated,failures);
    return failures;
}
