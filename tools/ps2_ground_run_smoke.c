#include <stdio.h>
#include <string.h>
#include "f3dex2_ps2.h"
#include "ps2_texture.h"
#include "ps2_gfx_profile.h"

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
    int failures=0;
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
#ifdef PS2_FLOOR_DIAGNOSTIC
        if (g_rdpState.zWriteEnabled != !!(low & 0x20) ||
            g_rdpState.gs->Test->ZTST != ((low & 0x10)?2:1)) failures++;
        if (fixture!=0) for (unsigned i=0;i+5<referenceCount;i+=6) {
            for (unsigned vertex=0;vertex<3;vertex++) {
                const GSPRIMSTQPOINT* receiver=&reference[i+vertex];
                const GSPRIMSTQPOINT* layer=&reference[i+3+vertex];
                u32 depth=receiver->xyz2.xyz.z & 0xffffffu;
                u32 expected=(low & 0x30)==0x30?(depth>0xfffffdu?0xffffffu:depth+2):depth;
                if ((layer->xyz2.xyz.z & 0xffffffu)!=expected ||
                    layer->xyz2.xyz.x!=receiver->xyz2.xyz.x ||
                    layer->xyz2.xyz.y!=receiver->xyz2.xyz.y ||
                    (layer->xyz2.xyz.z >> 24)!=(receiver->xyz2.xyz.z >> 24) ||
                    layer->rgbaq.color.q!=receiver->rgbaq.color.q) failures++;
            }
        }
#endif
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
    unsigned readonlySeen=0,restoreSeen=0;
    for (u64* q=queueStart; q+1<(u64*)g_rdpState.gs->CurQueue->pool_cur;q+=2) {
        if (q[1] == (u64)(GS_ZBUF_1+g_rdpState.gs->PrimContext)) {
            if (q[0] & (1ull<<32)) readonlySeen++;
            else if (readonlySeen) restoreSeen++;
        }
    }
    if (!readonlySeen || !restoreSeen || !g_rdpState.zWriteEnabled ||
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
        if (i>=3 && reference[i].xyz2.xyz.z !=
            (expectedDepth>0xfffffdu?0xffffffu:expectedDepth+2)) labFailures++;
        if (points[i].rgbaq.color.q!=1.0f ||
            points[i].stq.st.s!=reference[i].stq.st.s ||
            points[i].stq.st.t!=reference[i].stq.st.t ||
            points[i].xyz2.xyz.x!=reference[i].xyz2.xyz.x ||
            points[i].xyz2.xyz.y!=reference[i].xyz2.xyz.y ||
            points[i].xyz2.xyz.z!=expectedDepth) labFailures++;
    }
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
    printf("[PS2 FLOOR LAB TEST] frame latch, layer isolation, legacy depth, both vertex paths and unaffected materials failures=%d\n",labFailures);
#endif
    g_rspState=savedRsp; g_rdpState=savedRdp;
    ps2_gs_state_reset(); rdp_apply_othermode(&g_rdpState);
    printf("[PS2 GROUND RUN TEST] points, layer order, packets, fog, perspective, alpha, decals, clipping and state fences failures=%d\n",failures);
    printf("[PS2 GROUND RUN BENCH] immediateCycles=%u reusedCycles=%u\n",
        (unsigned)immediateCycles,(unsigned)reusedCycles);
    return failures;
}
