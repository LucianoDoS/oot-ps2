/* Differential primitive output against the old pointer-derived slots. */
#include <stdio.h>
#include <string.h>
#include "f3dex2_ps2.h"
#include "ps2_texture.h"
#include "ps2_gfx_profile.h"
#include "ps2_color_scale.h"

static volatile u8 s_colorScaleInput[256];
static volatile u32 s_colorScaleSink;

/* Volatile runtime inputs keep both isolated kernels from constant folding.
 * The same loads, loop count and checksum work surround each conversion. */
static __attribute__((noinline)) u32 color_scale_reference_cycles(unsigned repeats) {
    u32 checksum=0;
    for (unsigned repeat=0;repeat<repeats;repeat++)
        for (unsigned i=0;i<256;i++) {
            u8 value=s_colorScaleInput[i];
            checksum+=(u32)value*128u/255;
        }
    return checksum;
}
static __attribute__((noinline)) u32 color_scale_byte_cycles(unsigned repeats) {
    u32 checksum=0;
    for (unsigned repeat=0;repeat<repeats;repeat++)
        for (unsigned i=0;i<256;i++) {
            u8 value=s_colorScaleInput[i];
            checksum+=ps2_color_scale_byte(value);
        }
    return checksum;
}
static unsigned color_scale_smoke(void) {
    unsigned failures=0;
    unsigned offset=ps2_gfx_cycles()&255u;
    for (unsigned i=0;i<256;i++) s_colorScaleInput[i]=(u8)(i+offset);
    for (unsigned i=0;i<256;i++) {
        u8 value=s_colorScaleInput[i];
        failures+=ps2_color_scale_byte(value)!=(u32)value*128u/255;
    }
    u32 start=ps2_gfx_cycles();
    u32 referenceChecksum=color_scale_reference_cycles(32);
    u32 referenceCycles=ps2_gfx_cycles()-start;
    s_colorScaleSink=referenceChecksum;
    start=ps2_gfx_cycles();
    u32 byteChecksum=color_scale_byte_cycles(32);
    u32 byteCycles=ps2_gfx_cycles()-start;
    s_colorScaleSink=byteChecksum;
    failures+=referenceChecksum!=byteChecksum;
    printf("[PS2 COLOR SCALE TEST] bytes=256 failures=%u referenceCycles=%u byteCycles=%u checksum=%u operations=8192 (isolated byte scaling, not frame time)\n",
        failures,referenceCycles,byteCycles,(unsigned)s_colorScaleSink);
    return failures;
}

static u32 triangle_word(unsigned a, unsigned b, unsigned c) {
    return 0x05000000u | (a << 17) | (b << 9) | (c << 1);
}
int ps2_vertex_index_smoke(void) {
    static RSPState savedRsp;
    static RDPState savedRdp;
    static GSPRIMSTQPOINT reference[600];
    static const u8 texture[8] __attribute__((aligned(16))) = {0xf8,1,7,0xc1,0,0x3f,0xff,0xff};
    static const unsigned indices[][3] = {{63,37,2},{2,37,63},{37,37,2},
        {0,31,62},{62,31,0},{63,2,0},{63,64,2},{37,2,31}};
    savedRsp = g_rspState; savedRdp = g_rdpState;
    unsigned failures = color_scale_smoke(), pointsCompared = 0, referenceCycles = 0, directCycles = 0;
    for (unsigned fixture = 0; fixture < 8; fixture++) {
        g_rspState = savedRsp;
        g_rspState.texEnabled = !(fixture & 1); g_rspState.texTile = 0;
        g_rspState.texScaleS = 0.75f; g_rspState.texScaleT = 0.5f;
        g_rspState.geometryMode = fixture & 2 ? 0x10000u : 0;
        g_rspState.fogMul = 100; g_rspState.fogOff = 120;
        for (unsigned i = 0; i < 64; i++) g_rspState.vtx[i] = (RSPVertex){
            .valid=true,.cw=2,.cx=(int)(i%3)-1,.cy=i%3==1?0.5f:-0.5f,.cz=0.25f,
            .sz=0.25f,.sx=100+i*3,.sy=i%3==1?130:100,
            .tc_s=(int)i*321-71,.tc_t=(int)i*157-321,
            .cr=(i*91+29)&255,.cg=(i*73+53)&255,.cb=(i*71+53)&255,.ca=(i*63+83)&255};
        if (fixture & 4) { g_rspState.vtx[37].cx=-6; g_rspState.vtx[37].clipFlags=1; }
        rdp_setothermode(1u<<19,0xc8112078u,&g_rdpState);
        rdp_setcombine(0xfc119623u,0xff2fffffu,&g_rdpState);
        g_rdpState.tile[0] = (TileDescriptor){.fmt=G_IM_FMT_RGBA,.siz=G_IM_SIZ_16b,
            .width=2,.height=2,.masks=1,.maskt=1,.addr=(void*)texture};
        g_rdpState.textureLoads[0].source=texture; g_rdpState.textureLoads[0].stride=0;
        if (g_rspState.texEnabled) ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
        unsigned count[2], hits[2], invalid[2];
        for (unsigned path = 0; path < 2; path++) {
            f3dex2_test_index_reference(!path);
            f3dex2_test_capture(true);
            unsigned previousHits=g_rdpState.stats.textureVertexHits+g_rdpState.stats.colorVertexHits;
            unsigned previousInvalid=g_rdpState.stats.invalid;
            ps2_gs_state_reset(); rdp_apply_othermode(&g_rdpState);
            u32 start=ps2_gfx_cycles();
            f3dex2_batch_begin(&g_rspState,&g_rdpState);
            for (unsigned repeat = 0; repeat < 3; repeat++)
                for (unsigned command = 0; command < sizeof(indices)/sizeof(indices[0]); command++)
                    f3dex2_tri1(triangle_word(indices[command][0],indices[command][1],indices[command][2]),
                        0,&g_rspState,&g_rdpState);
            f3dex2_batch_end(&g_rdpState);
            if (!path) referenceCycles+=ps2_gfx_cycles()-start;
            else directCycles+=ps2_gfx_cycles()-start;
            const GSPRIMSTQPOINT* points=f3dex2_test_points(&count[path]);
            hits[path]=g_rdpState.stats.textureVertexHits+g_rdpState.stats.colorVertexHits-previousHits;
            invalid[path]=g_rdpState.stats.invalid-previousInvalid;
            if (!path) memcpy(reference,points,count[path]*sizeof(*points));
            else {
                failures+=count[0]!=count[1] || hits[0]!=hits[1] || invalid[0]!=invalid[1];
                failures+=memcmp(reference,points,count[1]*sizeof(*points))!=0;
                pointsCompared+=count[1];
            }
        }
    }
    f3dex2_test_index_reference(false); f3dex2_test_capture(false);
    g_rspState=savedRsp; g_rdpState=savedRdp;
    ps2_gs_state_reset(); rdp_apply_othermode(&g_rdpState);
    printf("[PS2 VERTEX INDEX TEST] sparse/high/repeated/invalid indices, clipped temporaries, textured/colored and fog points=%u failures=%u referenceCycles=%u directCycles=%u\n",
        pointsCompared,failures,referenceCycles,directCycles);
    return failures;
}
