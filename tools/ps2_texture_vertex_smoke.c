/* Compare all 48 GS point bytes with the original preparation on the EE. */
#include <stdio.h>
#include <string.h>
#include "ps2_texture.h"
#include "f3dex2_ps2.h"
#include "ps2_gfx_profile.h"

int ps2_texture_vertex_smoke(void) {
    static RSPState rsp;
    static RDPState rdp;
    static GSPRIMSTQPOINT reference[32], prepared[32];
    static u8 unaligned[32 * sizeof(GSPRIMSTQPOINT) + 16] __attribute__((aligned(16)));
    static const float cameraW[] = {-1, 0, 0.00009999f, 0.0001f, 0.00010001f, 0.75f, 2.5f, 1000};
    static const float scales[] = {0, -0.7f, 0.33333f};
    int failures = 0;
    unsigned cases = 0;
    rsp = g_rspState;
    rdp = g_rdpState;
    for (unsigned i = 0; i < 32; i++) {
        rsp.vtx[i] = (RSPVertex){.cw=cameraW[i & 7], .sx=15.25f+i*3.125f,
            .sy=400.125f-i*5.375f, .tc_s=(s16)(i*45929u+32768u),
            .tc_t=(s16)(i*25531u+13579u)};
    }
    for (unsigned material = 0; material < 3; material++) {
        rdp.combiner.w0 = material == 0 ? 0 : 0xfc119623;
        rdp.combiner.w1 = material == 0 ? 0 : 0xff2fffff;
        for (unsigned perspective = 0; perspective < 2; perspective++) {
            rdp.otherMode.hi = (material == 2 ? 1u << 20 : 0) | (perspective ? 1u << 19 : 0);
            rdp.otherMode.lo = 0;
            if (ps2_combiner_dynamic_alpha(&rdp) != (material == 1)) failures++;
            for (unsigned extent = 0; extent < 3; extent++) {
                TileDescriptor tile = {.width=7, .height=9};
                if (extent == 1) { tile.gsTexture.Width=31; tile.gsTexture.Height=65; }
                if (extent == 2) { tile.gsInvWidth=1.0f/32; tile.gsInvHeight=1.0f/64; }
                for (unsigned shift = 0; shift < 16; shift++) {
                    tile.shifts=shift; tile.shiftt=15-shift;
                    tile.uls=4095-shift*71; tile.ult=shift*131;
                    rdp.primColor = 0x12345600u | (shift == 0 ? 0 : shift == 15 ? 255 : shift*17);
                    for (unsigned scale = 0; scale < 3; scale++) {
                        rsp.texScaleS=scales[scale]; rsp.texScaleT=scales[2-scale];
                        PS2TextureVertexSetup setup;
                        ps2_texture_prepare_vertices(&setup, &tile, &rsp, &rdp);
                        for (unsigned i = 0; i < 32; i++) {
                            u32 color=0x001937edu | ((i*53u & 255) << 24);
                            u32 depth=i == 0 ? 0 : i == 31 ? 0xffffff : i*540931;
                            memset(&reference[i], 0xA5, sizeof(reference[i]));
                            memset(&prepared[i], 0xA5, sizeof(prepared[i]));
                            ps2_texture_build_vertex(&tile, &rsp, &rdp, &rsp.vtx[i], color, depth, &reference[i]);
                            ps2_texture_build_vertex_prepared(&setup, &rsp.vtx[i], color, depth, &prepared[i]);
                            if (memcmp(&reference[i], &prepared[i], sizeof(reference[i]))) {
                                if (failures < 4) printf("[PS2 POINT SETUP] difference material=%u perspective=%u extent=%u shift=%u scale=%u vertex=%u\n",
                                    material, perspective, extent, shift, scale, i);
                                failures++;
                            }
                            cases++;
                        }
                    }
                }
            }
        }
    }
    TileDescriptor tile = {.gsInvWidth=1.0f/32, .gsInvHeight=1.0f/64,
        .shifts=2, .shiftt=11, .uls=12, .ult=20};
    rsp.texScaleS=0.75f; rsp.texScaleT=0.5f;
    rdp.otherMode.hi=1u<<19; rdp.otherMode.lo=0;
    rdp.combiner.w0=0xfc119623; rdp.combiner.w1=0xff2fffff; rdp.primColor=0xffffff80;
    PS2TextureVertexSetup setup;
    ps2_texture_prepare_vertices(&setup, &tile, &rsp, &rdp);
    /* All destination alignments, XY clamp extremes and all depth bits.
     * Guard bytes prove that a full-word store never crosses the point. */
    unsigned alignmentCases = 0;
    for (unsigned offset = 0; offset < 8; offset++) for (unsigned i = 0; i < 32; i++) {
        RSPVertex vertex = rsp.vtx[i];
        vertex.sx = i & 1 ? -8000.125f : 8000.875f;
        vertex.sy = i & 2 ? -8000.875f : 8000.125f;
        u32 depth = i & 4 ? 0xffffffffu : 0x80000000u;
        memset(unaligned, 0xA5, sizeof(unaligned));
        ps2_texture_build_vertex(&tile, &rsp, &rdp, &vertex, 0xff123456, depth, &reference[0]);
        ps2_texture_build_vertex_prepared(&setup, &vertex, 0xff123456, depth,
            (GSPRIMSTQPOINT*)(unaligned + offset));
        if (memcmp(&reference[0], unaligned + offset, sizeof(reference[0]))) failures++;
        for (unsigned byte = 0; byte < sizeof(unaligned); byte++)
            if ((byte < offset || byte >= offset + sizeof(reference[0])) && unaligned[byte] != 0xA5) failures++;
        alignmentCases++;
    }
    u32 start=ps2_gfx_cycles();
    for (unsigned i=0; i<32768; i++)
        ps2_texture_build_vertex(&tile, &rsp, &rdp, &rsp.vtx[i&31], 0x801937ed, i, &reference[i&31]);
    u32 referenceCycles=ps2_gfx_cycles()-start;
    start=ps2_gfx_cycles();
    for (unsigned i=0; i<32768; i++)
        ps2_texture_build_vertex_prepared(&setup, &rsp.vtx[i&31], 0x801937ed, i, &prepared[i&31]);
    u32 preparedCycles=ps2_gfx_cycles()-start;
    if (memcmp(reference, prepared, sizeof(reference))) failures++;
    start=ps2_gfx_cycles();
    for (unsigned i=0; i<32768; i++)
        ps2_texture_build_vertex_prepared(&setup, &rsp.vtx[i&31], 0x801937ed, i,
            (GSPRIMSTQPOINT*)(unaligned + 1 + (i&31)*sizeof(GSPRIMSTQPOINT)));
    u32 packedCycles=ps2_gfx_cycles()-start;
    if (memcmp(prepared, unaligned + 1, sizeof(prepared))) failures++;
    printf("[PS2 POINT SETUP TEST] %s: cases=%u alignmentCases=%u failures=%d referenceCycles=%u preparedCycles=%u packedCycles=%u (32768 points)\n",
        failures ? "FAIL" : "PASS", cases, alignmentCases, failures, referenceCycles, preparedCycles, packedCycles);
    return failures;
}
