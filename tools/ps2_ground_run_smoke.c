#include <stdio.h>
#include <string.h>
#include <math.h>
#include "f3dex2_ps2.h"
#include "ps2_texture.h"
#include "ps2_gfx_profile.h"

static float gs_texture_precision(float value) {
    union { float f; u32 bits; } number = {.f=value};
    number.bits &= ~255u;
    return number.f;
}
static float texture_interpolant(const GSPRIMSTQPOINT points[3], unsigned axis,
                                bool gsPrecision) {
    const float weights[3]={0.17f,0.31f,0.52f};
    float numerator=0,denominator=0;
    for (unsigned i=0;i<3;i++) {
        float st=axis?points[i].stq.st.t:points[i].stq.st.s;
        float q=points[i].rgbaq.color.q;
        if (gsPrecision) { st=gs_texture_precision(st); q=gs_texture_precision(q); }
        numerator+=weights[i]*st; denominator+=weights[i]*q;
    }
    return numerator/denominator;
}
static int texture_wrap_smoke(void) {
    int failures=0;
    float errorBefore=0,errorAfter=0;
    for (unsigned fixture=0;fixture<7;fixture++) {
        TileDescriptor tile={.width=32,.height=32,.masks=5,.maskt=5,.shifts=14,.shiftt=14};
        tile.gsTexture.Width=tile.gsTexture.Height=32;
        if (fixture==2) { tile.cms=tile.cmt=1; tile.gsTexture.Width=tile.gsTexture.Height=64; }
        if (fixture==3) tile.cms=2;
        if (fixture==4) tile.masks=tile.maskt=0;
        RSPState rsp={.texScaleS=1,.texScaleT=1};
        RDPState rdp={.gs=g_rdpState.gs}; rdp.otherMode.hi=1u<<19;
        RSPVertex vertices[3]; const RSPVertex* addresses[3];
        GSPRIMSTQPOINT original[3],rebased[3],withoutStats[3];
        float sign=fixture==1?-1:1;
        for (unsigned i=0;i<3;i++) {
            vertices[i]=(RSPVertex){.tc_s=sign*(16000+i*512),.tc_t=12000+i*256,
                .cw=600+i*550,.sx=100+i*10,.sy=120};
            if (fixture==5) { vertices[i].tc_s=8*(int)i-8; vertices[i].tc_t=8*(int)i-8; }
            if (fixture==6) vertices[i].tc_s=(int)i*32767-32767;
            addresses[i]=&vertices[i];
            ps2_texture_build_vertex(&tile,&rsp,&rdp,&vertices[i],0x40806020,12345,&original[i]);
        }
        memcpy(rebased,original,sizeof(rebased));
        PS2TextureWrapResult legacy,result;
        ps2_texture_rebase_triangle(&tile,&rsp,&rdp,addresses,rebased,false,&legacy);
        if (memcmp(original,rebased,sizeof(original)) || legacy.offsetS || legacy.offsetT) failures++;
        ps2_texture_rebase_triangle(&tile,&rsp,&rdp,addresses,rebased,true,&result);
        memcpy(withoutStats,original,sizeof(original));
        ps2_texture_rebase_triangle(&tile,&rsp,&rdp,addresses,withoutStats,true,NULL);
        if (memcmp(rebased,withoutStats,sizeof(rebased))) failures++;
        PS2TextureVertexSetup setup;
        ps2_texture_prepare_vertices(&setup,&tile,&rsp,&rdp);
        float uv[2][3];
        for (unsigned i=0;i<3;i++) {
            float coordinates[2];
            ps2_texture_vertex_coordinates(&setup,addresses[i],coordinates);
            uv[0][i]=coordinates[0]; uv[1][i]=coordinates[1];
        }
        memcpy(withoutStats,original,sizeof(original));
        PS2TextureWrapResult cached;
        ps2_texture_rebase_prepared_triangle(&setup,uv,withoutStats,true,&cached);
        if (memcmp(rebased,withoutStats,sizeof(rebased)) || memcmp(&result,&cached,sizeof(result))) failures++;
        if (fixture<3 && (!result.outsideBefore || result.outsideAfter || result.maxAfter>128)) failures++;
        if (fixture==3 && result.offsetS) failures++;
        if (fixture==4 && memcmp(original,rebased,sizeof(original))) failures++;
        if (fixture==5 && (result.offsetS || result.offsetT)) failures++;
        if (fixture==6 && !result.outsideAfter) failures++; /* Translation cannot shrink a wide span. */
        unsigned period=fixture==2?64:32;
        if (fmodf(result.offsetS,period) || fmodf(result.offsetT,period)) failures++;
        for (unsigned i=0;i<3;i++) {
            if (memcmp(&original[i].rgbaq,&rebased[i].rgbaq,sizeof(original[i].rgbaq)) ||
                memcmp(&original[i].xyz2,&rebased[i].xyz2,sizeof(original[i].xyz2))) failures++;
        }
        float width=tile.gsTexture.Width,height=tile.gsTexture.Height;
        for (unsigned axis=0;axis<2;axis++) {
            float extent=axis?height:width,offset=axis?result.offsetT:result.offsetS;
            float ideal=texture_interpolant(original,axis,false)*extent;
            float adjusted=texture_interpolant(rebased,axis,false)*extent;
            if (fabsf(ideal-adjusted-offset)>0.003f) failures++;
            if (fixture<2) {
                errorBefore+=fabsf(texture_interpolant(original,axis,true)*extent-ideal);
                errorAfter+=fabsf(texture_interpolant(rebased,axis,true)*extent+offset-ideal);
            }
        }
    }
    if (errorAfter>=errorBefore*0.2f) failures++;
    printf("[PS2 FLOOR WRAP TEST] repeat, mirror, clamp, wide spans, perspective and unchanged XYZ/Q failures=%d gsErrorBefore=%.6g gsErrorAfter=%.6g\n",
        failures,errorBefore,errorAfter);
    return failures;
}

/* Compare every GS point in draw order with the existing immediate path.
 * Different layer extents also force the base clamp to be restored after
 * the overlay; merging the two passes would fail the packet count checks. */
int ps2_ground_run_smoke(void) {
    static RSPState savedRsp;
    static RDPState savedRdp;
    static GSPRIMSTQPOINT reference[600];
    static const u8 base[8] __attribute__((aligned(16)))={0xf8,1,7,0xc1,0,0x3f,0xff,0xff};
    static const u8 overlay[16] __attribute__((aligned(16)))={
        0,0x3f,0xf8,1,0xff,0xff,7,0xc1,7,0xc1,0xff,0xff,0xf8,1,0,0x3f};
    savedRsp=g_rspState; savedRdp=g_rdpState;
    int failures=texture_wrap_smoke();
    u32 immediateCycles=0, reusedCycles=0;
    for (unsigned fixture=0;fixture<8;fixture++) {
        g_rspState=savedRsp;
        g_rspState.texEnabled=true; g_rspState.texTile=0;
        g_rspState.texScaleS=0.75f; g_rspState.texScaleT=0.5f;
        g_rspState.geometryMode=fixture==1?0x10000u:fixture==7?0x600u:0;
        g_rspState.fogMul=100; g_rspState.fogOff=120;
        for (unsigned i=0;i<3;i++) g_rspState.vtx[i]=(RSPVertex){
            .valid=true,.cw=2,.cx=(int)i-1,.cy=i==1?0.5f:-0.5f,.cz=0.25f*i,
            .sz=0.25f*i,.sx=100+i*10,.sy=i==1?110:100,
            .tc_s=(int)i*321-71,.tc_t=(int)i*157-321,
            .cr=29+i*91,.cg=229-i*73,.cb=53+i*71,.ca=83+i*63};
        if (fixture==5) { g_rspState.vtx[0].cz=-6; g_rspState.vtx[0].clipFlags=16; }
        u32 low=fixture==4?(0xc8112078u&~0xc30u)|0xc10u:0xc8112078u;
        rdp_setothermode((1u<<20)|(fixture==2?1u<<19:0),low,&g_rdpState);
        rdp_setcombine(0xfc267e04,0x1ffcfdf8,&g_rdpState);
        rdp_setenvcolor(0,fixture==0?0:fixture==3?255:128,&g_rdpState);
        g_rdpState.textureTile=0;
        g_rdpState.tile[0]=(TileDescriptor){.fmt=G_IM_FMT_RGBA,.siz=G_IM_SIZ_16b,
            .width=2,.height=2,.masks=1,.maskt=1,.tmem=0,.addr=(void*)base};
        g_rdpState.tile[1]=(TileDescriptor){.fmt=G_IM_FMT_RGBA,.siz=G_IM_SIZ_16b,
            .width=4,.height=2,.masks=2,.maskt=1,.tmem=256,.addr=(void*)overlay,
            .uls=12,.ult=20,.shifts=2,.shiftt=11};
        g_rdpState.textureLoads[0].source=base; g_rdpState.textureLoads[0].stride=0;
        g_rdpState.textureLoads[256].source=overlay; g_rdpState.textureLoads[256].stride=0;
        if (!ps2_combiner_texture_lerp(&g_rdpState)) failures++;
        u32 first=fixture==6?0x06000004:0x06000204;
        u32 second=fixture==6?0x00000004:0x00000204;
        u32 packets=g_rdpState.stats.texturePackets;
        f3dex2_test_capture(true);
        u32 start=ps2_gfx_cycles();
        for (unsigned i=0;i<21;i++) f3dex2_tri2(first,second,&g_rspState,&g_rdpState);
        immediateCycles+=ps2_gfx_cycles()-start;
        unsigned referenceCount;
        const GSPRIMSTQPOINT* points=f3dex2_test_points(&referenceCount);
        u32 referencePackets=g_rdpState.stats.texturePackets-packets;
        memcpy(reference,points,referenceCount*sizeof(*points));
        if (g_rdpState.zWriteEnabled != !!(low & 0x20) ||
            g_rdpState.gs->Test->ZTST != ((low & 0x10)?2:1)) failures++;
        if (fixture!=0) for (unsigned i=0;i+5<referenceCount;i+=6) {
            for (unsigned vertex=0;vertex<3;vertex++) {
                const GSPRIMSTQPOINT* receiver=&reference[i+vertex];
                const GSPRIMSTQPOINT* layer=&reference[i+3+vertex];
                u32 depth=receiver->xyz2.xyz.z & 0xffffffu;
                if ((layer->xyz2.xyz.z & 0xffffffu)!=depth ||
                    layer->xyz2.xyz.x!=receiver->xyz2.xyz.x ||
                    layer->xyz2.xyz.y!=receiver->xyz2.xyz.y ||
                    (layer->xyz2.xyz.z >> 24)!=(receiver->xyz2.xyz.z >> 24) ||
                    layer->rgbaq.color.q!=receiver->rgbaq.color.q) failures++;
            }
        }
        packets=g_rdpState.stats.texturePackets;
        u32 builds=g_rdpState.stats.textureVertexBuilds, hits=g_rdpState.stats.textureVertexHits;
        f3dex2_test_capture(true);
        start=ps2_gfx_cycles();
        if (!f3dex2_batch_begin(&g_rspState,&g_rdpState)) failures++;
        for (unsigned i=0;i<21;i++) f3dex2_tri2(first,second,&g_rspState,&g_rdpState);
        f3dex2_batch_end(&g_rdpState);
        reusedCycles+=ps2_gfx_cycles()-start;
        unsigned actualCount;
        points=f3dex2_test_points(&actualCount);
        if (referenceCount!=actualCount || memcmp(reference,points,actualCount*sizeof(*points)) ||
            g_rdpState.stats.texturePackets-packets!=referencePackets) failures++;
        if (fixture<4) {
            u32 expectedBuilds=fixture==0?3:6;
            if (g_rdpState.stats.textureVertexBuilds-builds!=expectedBuilds ||
                g_rdpState.stats.textureVertexHits-hits!=referenceCount-expectedBuilds) failures++;
        }
        if ((fixture==4 || fixture==5) && g_rdpState.stats.textureVertexHits!=hits) failures++;
        f3dex2_test_capture(false);
        /* Every state-command boundary must invalidate BOTH point sets. */
        const u32 boundary[][2] __attribute__((aligned(8)))={
            {0x05000204,0},{0xfb000000,64},{0x05000204,0},{0xdf000000,0}};
        f3dex2_test_capture(true);
        f3dex2_tri1(0x05000204,0,&g_rspState,&g_rdpState);
        rdp_setenvcolor(0,64,&g_rdpState);
        f3dex2_tri1(0x05000204,0,&g_rspState,&g_rdpState);
        points=f3dex2_test_points(&referenceCount);
        memcpy(reference,points,referenceCount*sizeof(*points));
        rdp_setenvcolor(0,fixture==0?0:fixture==3?255:128,&g_rdpState);
        f3dex2_test_capture(true);
        rdp_to_gs_exec((const Gfx*)boundary);
        points=f3dex2_test_points(&actualCount);
        if (referenceCount!=actualCount || memcmp(reference,points,actualCount*sizeof(*points))) failures++;
        f3dex2_test_capture(false);
    }
#ifdef PS2_FLOOR_DIAGNOSTIC
    /* The laboratory must change exactly the selected material, and a pad
     * request must not change cached vertex setup midway through a frame. */
    int labFailures=0;
    g_rspState.geometryMode=0;
    rdp_setothermode(1u<<20,0xc8112078u,&g_rdpState);
    rdp_setenvcolor(0,128,&g_rdpState);
    ps2_floor_request_mode(PS2_FLOOR_NORMAL);
    ps2_floor_begin_frame();
    f3dex2_test_capture(true);
    u32 packets=g_rdpState.stats.texturePackets;
    u64* queueStart=g_rdpState.gs->CurQueue->pool_cur;
    u64 testBefore=g_rdpState.gs_test;
    f3dex2_tri1(0x05000204,0,&g_rspState,&g_rdpState);
    unsigned normalCount;
    const GSPRIMSTQPOINT* points=f3dex2_test_points(&normalCount);
    memcpy(reference,points,normalCount*sizeof(*points));
    if (normalCount!=6 || g_rdpState.stats.texturePackets-packets!=2) labFailures++;
    unsigned zChanges=0;
    for (u64* q=queueStart; q+1<(u64*)g_rdpState.gs->CurQueue->pool_cur;q+=2) {
        if (q[1] == (u64)(GS_ZBUF_1+g_rdpState.gs->PrimContext)) {
            zChanges++;
        }
    }
    if (zChanges || !g_rdpState.zWriteEnabled ||
        g_rdpState.gs_test!=testBefore || g_rdpState.gs->Test->ZTST!=2) labFailures++;
    ps2_floor_request_mode(PS2_FLOOR_BASE_ONLY);
    ps2_floor_request_mode(99); /* Invalid requests must not erase the valid one. */
    if (ps2_floor_mode()!=PS2_FLOOR_NORMAL) labFailures++;
    ps2_floor_begin_frame();
    if (ps2_floor_mode()!=PS2_FLOOR_BASE_ONLY) labFailures++;
    f3dex2_test_capture(true);
    packets=g_rdpState.stats.texturePackets;
    f3dex2_tri1(0x05000204,0,&g_rspState,&g_rdpState);
    unsigned count;
    points=f3dex2_test_points(&count);
    if (count!=3 || g_rdpState.stats.texturePackets-packets!=1 ||
        memcmp(reference,points,3*sizeof(*points))) labFailures++;
    ps2_floor_request_mode(PS2_FLOOR_LEGACY);
    ps2_floor_begin_frame();
    f3dex2_test_capture(true);
    f3dex2_batch_begin(&g_rspState,&g_rdpState);
    f3dex2_tri1(0x05000204,0,&g_rspState,&g_rdpState);
    f3dex2_batch_end(&g_rdpState);
    points=f3dex2_test_points(&count);
    if (count!=6) labFailures++;
    for (unsigned i=0; i<count; i++) {
        u32 expectedDepth=reference[i%3].xyz2.xyz.z;
        if (points[i].rgbaq.color.q!=1.0f ||
            points[i].xyz2.xyz.x!=reference[i].xyz2.xyz.x ||
            points[i].xyz2.xyz.y!=reference[i].xyz2.xyz.y ||
            points[i].xyz2.xyz.z!=expectedDepth) labFailures++;
    }
    GSPRIMSTQPOINT adjusted[3]; memcpy(adjusted,points+3,sizeof(adjusted));
    const RSPVertex* inputs[3]={&g_rspState.vtx[0],&g_rspState.vtx[1],&g_rspState.vtx[2]};
    ps2_texture_rebase_triangle(&g_rdpState.tile[1],&g_rspState,&g_rdpState,inputs,adjusted,true,NULL);
    if (memcmp(reference,points,3*sizeof(*points)) || memcmp(reference+3,adjusted,sizeof(adjusted))) labFailures++;
    PS2TextureVertexSetup setup;
    GSPRIMSTQPOINT direct, prepared;
    ps2_texture_prepare_vertices(&setup,&g_rdpState.tile[0],&g_rspState,&g_rdpState);
    ps2_texture_build_vertex(&g_rdpState.tile[0],&g_rspState,&g_rdpState,&g_rspState.vtx[0],0xffffffff,123,&direct);
    ps2_texture_build_vertex_prepared(&setup,&g_rspState.vtx[0],0xffffffff,123,&prepared);
    if (memcmp(&direct,&prepared,sizeof(direct)) || direct.rgbaq.color.q!=1.0f) labFailures++;
    rdp_setcombine(0,0,&g_rdpState);
    ps2_texture_prepare_vertices(&setup,&g_rdpState.tile[0],&g_rspState,&g_rdpState);
    ps2_texture_build_vertex(&g_rdpState.tile[0],&g_rspState,&g_rdpState,&g_rspState.vtx[0],0xffffffff,123,&direct);
    if (ps2_combiner_texture_lerp(&g_rdpState) || setup.perspective || direct.rgbaq.color.q!=1.0f) labFailures++;
    ps2_floor_request_mode(PS2_FLOOR_NORMAL);
    ps2_floor_begin_frame();
    f3dex2_test_capture(false);
    failures+=labFailures;
    printf("[PS2 FLOOR LAB TEST] frame latch, layer isolation, legacy UV, both vertex paths and unaffected materials failures=%d\n",labFailures);
#endif
    g_rspState=savedRsp; g_rdpState=savedRdp;
    ps2_gs_state_reset(); rdp_apply_othermode(&g_rdpState);
    printf("[PS2 GROUND RUN TEST] points, layer order, packets, fog, perspective, alpha, decals, clipping and state fences failures=%d\n",failures);
    printf("[PS2 GROUND RUN BENCH] immediateCycles=%u reusedCycles=%u\n",
        (unsigned)immediateCycles,(unsigned)reusedCycles);
    return failures;
}
