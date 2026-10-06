/* Compact GS register lists; texture/state equations remain gsKit-compatible.
 * PRE is not applied in REGLIST, so PRIM stays an explicit A_D write.
 * The allocator still owns DMA chaining and queue rollover.
 * GS/gsKit reference: https://github.com/ps2dev/gsKit/blob/master/ee/gs/src/gsTexture.c
 */
#include <stdbool.h>
#include <stdint.h>
#include <gsKit.h>
#include <gsInline.h>
#include "ps2_triangle_packet.h"

_Static_assert(sizeof(GSPRIMSTQPOINT) == 48, "gsKit A_D textured vertex layout");
_Static_assert(sizeof(GSPRIMPOINT) == 32, "gsKit A_D colored vertex layout");
typedef u64 PacketWord __attribute__((may_alias));

static unsigned texture_bits(unsigned extent) {
    unsigned leading;
    if (!extent) return 0;
    __asm__("plzcw %0,%1" : "=r"(leading) : "r"(extent));
    return 30 - leading + !!(extent & (extent - 1));
}

static u64* vertex_packet(GSGLOBAL* gs, int count, unsigned registers,
    u64 registerList, u64 prim, bool textured, u64 tex0) {
    unsigned stateCount = textured ? 2 : 1;
    unsigned words = count * registers;
    unsigned qsize = stateCount + 1 + (words + 1) / 2;
    u64* packet = gsKit_heap_alloc(gs, qsize, qsize * 16, GIF_AD);
    packet[0] = GIF_TAG_AD(stateCount); packet[1] = GIF_AD;
    packet += 2;
    if (textured) {
        packet[0] = tex0; packet[1] = GS_TEX0_1 + gs->PrimContext;
        packet += 2;
    }
    packet[0] = prim; packet[1] = GS_PRIM;
    packet[2] = GIF_TAG(count, 1, 0, 0, GSKIT_GIF_FLG_REGLIST, registers);
    packet[3] = registerList;
    packet += 4;
    if (words & 1) packet[words] = 0; /* GIF skips the odd trailing 64-bit word. */
    return packet;
}

void ps2_triangle_prepare_texture(PS2TriangleTextureSetup* setup, const GSGLOBAL* gs,
    const GSTEXTURE* texture) {
    unsigned tw = texture_bits(texture->Width), th = texture_bits(texture->Height);
    setup->tex0 = GS_SETREG_TEX0(texture->Vram / 256, texture->TBW, texture->PSM,
        tw, th, gs->PrimAlphaEnable, 0,
        texture->VramClut / 256, texture->VramClut ? texture->ClutPSM : 0,
        texture->VramClut ? texture->ClutStorageMode : 0, 0,
        texture->VramClut ? GS_CLUT_STOREMODE_LOAD : GS_CLUT_STOREMODE_NOLOAD);
    setup->prim = GS_SETREG_PRIM(GS_PRIM_PRIM_TRIANGLE, 1, 1, gs->PrimFogEnable,
        gs->PrimAlphaEnable, gs->PrimAAEnable, 0, gs->PrimContext, 0);
    setup->filter = texture->Filter;
}

void ps2_prim_textured_triangles(GSGLOBAL* gs, GSTEXTURE* texture, int count,
    const GSPRIMSTQPOINT* points, bool setFilter) {
    PS2TriangleTextureSetup setup;
    ps2_triangle_prepare_texture(&setup, gs, texture);
    ps2_prim_prepared_textured_triangles(gs, &setup, texture, count, points, setFilter);
}

void ps2_prim_prepared_textured_triangles(GSGLOBAL* gs, const PS2TriangleTextureSetup* setup,
    GSTEXTURE* texture, int count, const GSPRIMSTQPOINT* points, bool setFilter) {
    if (count <= 0) return;
    if ((uintptr_t)points & 7) {
        gsKit_prim_list_triangle_goraud_texture_stq_3d(gs, texture, count, points);
        return;
    }
    /* gsKit's packed unions have alignment 1. After checking the caller's
     * actual alignment, word aliases permit LD instead of byte assembly. */
    const PacketWord* input = (const PacketWord*)points;
    unsigned xyz = input[5];
    for (int i = 0; i < count; i++) {
        if ((xyz != GS_XYZ2 && xyz != GS_XYZF2) ||
            input[i * 6 + 1] != GS_RGBAQ || input[i * 6 + 3] != GS_ST || input[i * 6 + 5] != xyz) {
            gsKit_prim_list_triangle_goraud_texture_stq_3d(gs, texture, count, points);
            return;
        }
    }
    if (setFilter) gsKit_set_texfilter(gs, setup->filter);
    u64* output = vertex_packet(gs, count, 3,
        GS_RGBAQ | ((u64)GS_ST << 4) | ((u64)xyz << 8), setup->prim, true, setup->tex0);
    for (int i = 0; i < count; i++) {
        output[0] = input[0];
        output[1] = input[2];
        output[2] = input[4];
        input += 6;
        output += 3;
    }
}

void ps2_prim_colored_triangles(GSGLOBAL* gs, int count, const GSPRIMPOINT* points) {
    if (count <= 0) return;
    if ((uintptr_t)points & 7) {
        gsKit_prim_list_triangle_gouraud_3d(gs, count, points);
        return;
    }
    const PacketWord* input = (const PacketWord*)points;
    unsigned xyz = input[3];
    for (int i = 0; i < count; i++) {
        if ((xyz != GS_XYZ2 && xyz != GS_XYZF2) ||
            input[i * 4 + 1] != GS_RGBAQ || input[i * 4 + 3] != xyz) {
            gsKit_prim_list_triangle_gouraud_3d(gs, count, points);
            return;
        }
    }
    u64 prim = GS_SETREG_PRIM(GS_PRIM_PRIM_TRIANGLE, 1, 0, gs->PrimFogEnable,
        gs->PrimAlphaEnable, gs->PrimAAEnable, 0, gs->PrimContext, 0);
    u64* output = vertex_packet(gs, count, 2, GS_RGBAQ | ((u64)xyz << 4), prim, false, 0);
    for (int i = 0; i < count; i++) {
        output[0] = input[0];
        output[1] = input[2];
        input += 4;
        output += 2;
    }
}
