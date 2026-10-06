/* Decode installed gsKit and compact packets into identical GS register
 * streams. Raw packet length alone cannot detect lost Q or fog. */
#include <gsKit.h>
#include <gsInline.h>
#include <stdio.h>
#include <string.h>
#include "ps2_triangle_packet.h"
#include "rdp_to_gs.h"
#include "ps2_gfx_profile.h"

typedef struct { u64 value, address; } Register;
static u64 arena[4096] __attribute__((aligned(64)));
static Register expected[256], actual[256];
static GSPRIMSTQPOINT textured[60];
static GSPRIMPOINT colored[60];

static void reset(GSQUEUE* queue) {
    memset(queue, 0, sizeof(*queue));
    queue->pool[0] = arena; queue->pool_max[0] = arena + 4096;
    queue->dma_tag = arena; queue->pool_cur = arena + 2;
    queue->last_type = GIF_RESERVED;
}
static int decode(const GSQUEUE* queue, Register* output) {
    const u64* packet = arena + 2;
    const u64* end = queue->pool_cur;
    unsigned at = 0;
    while (packet < end) {
        if (packet == queue->dma_tag) { packet += 2; continue; }
        u64 tag = packet[0], list = packet[1]; packet += 2;
        unsigned loops = tag & 0x7fff, flag = (tag >> 58) & 3;
        unsigned registers = (tag >> 60) & 15;
        if (!registers) registers = 16;
        unsigned words = loops * registers;
        if (flag > 1 || (flag == 0 && (registers != 1 || list != GIF_AD))) return -1;
        if (packet + (flag ? ((words + 1) & ~1u) : words * 2) > end) return -2;
        for (unsigned i = 0; i < words; i++) {
            if (at >= 256) return -3;
            output[at].value = *packet++;
            output[at++].address = flag ? (list >> ((i % registers) * 4)) & 15 : *packet++;
        }
        if (flag && (words & 1)) packet++;
    }
    return at;
}

int ps2_triangle_packet_smoke(void) {
    GSGLOBAL gs = *g_rdpState.gs;
    GSQUEUE queue;
    gs.CurQueue = &queue;
    int failures = 0;
    unsigned oldBytes = 0, newBytes = 0;
    for (unsigned fixture = 0; fixture < 24; fixture++) {
        int count = fixture % 3 == 0 ? 3 : fixture % 3 == 1 ? 6 : 60;
        gs.PrimContext = fixture & 1; gs.PrimFogEnable = !!(fixture & 2);
        gs.PrimAlphaEnable = !!(fixture & 4); gs.PrimAAEnable = !!(fixture & 8);
        GSTEXTURE texture = {.Width = fixture & 1 ? 31 : 32, .Height = fixture & 2 ? 9 : 16,
            .Vram = 0x8000, .TBW = 1, .PSM = fixture & 4 ? GS_PSM_T8 : GS_PSM_CT16S,
            .VramClut = fixture & 4 ? 0x18000 : 0, .ClutPSM = GS_PSM_CT32,
            .ClutStorageMode = fixture & 8 ? 1 : 0,
            .Filter = fixture & 16 ? GS_FILTER_LINEAR : GS_FILTER_NEAREST};
        for (int i = 0; i < count; i++) {
            textured[i].rgbaq = rgba_to_RGBAQ(0x40802001u + i * 79231u, 1.0f / (i + 1));
            textured[i].stq = vertex_to_STQ(i * -1.23f, i * 0.76f);
            textured[i].xyz2 = vertex_to_XYZ2(&gs, 80 + i, 110 - i, 0x345678 + i * 21);
            if (gs.PrimFogEnable) {
                textured[i].xyz2.tag = GS_XYZF2;
                textured[i].xyz2.xyz.z |= (27u + i) << 24;
            }
            colored[i].rgbaq = textured[i].rgbaq; colored[i].xyz2 = textured[i].xyz2;
        }
        reset(&queue);
        gsKit_prim_list_triangle_goraud_texture_stq_3d(&gs, &texture, count, textured);
        int expectedCount = decode(&queue, expected);
        oldBytes += (u8*)queue.pool_cur - (u8*)arena;
        reset(&queue);
        PS2TriangleTextureSetup setup;
        ps2_triangle_prepare_texture(&setup, &gs, &texture);
        ps2_prim_prepared_textured_triangles(&gs, &setup, &texture, count, textured, true);
        int actualCount = decode(&queue, actual);
        newBytes += (u8*)queue.pool_cur - (u8*)arena;
        if (expectedCount < 0 || actualCount != expectedCount ||
            memcmp(expected, actual, expectedCount * sizeof(*expected))) failures++;
        reset(&queue);
        gsKit_prim_list_triangle_gouraud_3d(&gs, count, colored);
        expectedCount = decode(&queue, expected);
        reset(&queue);
        ps2_prim_colored_triangles(&gs, count, colored);
        actualCount = decode(&queue, actual);
        if (expectedCount < 0 || actualCount != expectedCount ||
            memcmp(expected, actual, expectedCount * sizeof(*expected))) failures++;
    }
    /* Unsupported mixed layouts fall back. Odd padding must not add vertices. */
    gs.PrimFogEnable = 1; textured[0].xyz2.tag = GS_XYZ2;
    GSTEXTURE texture = {.Width = 32, .Height = 32, .PSM = GS_PSM_CT16S, .TBW = 1};
    reset(&queue);
    gsKit_prim_list_triangle_goraud_texture_stq_3d(&gs, &texture, 3, textured);
    int expectedCount = decode(&queue, expected);
    reset(&queue);
    ps2_prim_textured_triangles(&gs, &texture, 3, textured, true);
    int actualCount = decode(&queue, actual);
    if (actualCount != expectedCount || memcmp(expected, actual, expectedCount * sizeof(*expected))) failures++;
    /* SDK fallback must also preserve arbitrarily byte-aligned callers. */
    static u8 unaligned[sizeof(GSPRIMSTQPOINT) * 3 + 1];
    memcpy(unaligned + 1, textured, sizeof(unaligned) - 1);
    reset(&queue);
    ps2_prim_textured_triangles(&gs, &texture, 3, (const GSPRIMSTQPOINT*)(unaligned + 1), true);
    actualCount = decode(&queue, actual);
    if (actualCount != expectedCount || memcmp(expected, actual, expectedCount * sizeof(*expected))) failures++;
    /* Force the allocator's DMA_CNT rollover without sending the synthetic
     * huge prefix to hardware. Both GIF packets must remain decodable. */
    textured[0].xyz2.tag = textured[1].xyz2.tag;
    reset(&queue); queue.tag_size = 65534;
    gsKit_prim_list_triangle_goraud_texture_stq_3d(&gs, &texture, 3, textured);
    expectedCount = decode(&queue, expected);
    reset(&queue); queue.tag_size = 65534;
    ps2_prim_textured_triangles(&gs, &texture, 3, textured, true);
    actualCount = decode(&queue, actual);
    if (queue.dma_tag != arena + 2 || expectedCount < 0 || actualCount != expectedCount ||
        memcmp(expected, actual, expectedCount * sizeof(*expected))) failures++;
    u32 oldCycles = 0, newCycles = 0;
    for (unsigned pass = 0; pass < 40; pass++) {
        textured[0].xyz2.tag = textured[1].xyz2.tag;
        reset(&queue);
        u32 start = ps2_gfx_cycles();
        for (unsigned i = 0; i < 64; i++) gsKit_prim_list_triangle_goraud_texture_stq_3d(&gs, &texture, 3, textured);
        oldCycles += ps2_gfx_cycles() - start;
        reset(&queue);
        start = ps2_gfx_cycles();
        PS2TriangleTextureSetup setup;
        ps2_triangle_prepare_texture(&setup, &gs, &texture);
        for (unsigned i = 0; i < 64; i++)
            ps2_prim_prepared_textured_triangles(&gs, &setup, &texture, 3, textured, i == 0);
        newCycles += ps2_gfx_cycles() - start;
    }
    printf("[PS2 TRIANGLE PACKET TEST] failures=%d oldBytes=%u newBytes=%u oldCycles=%u newCycles=%u\n",
        failures, oldBytes, newBytes, oldCycles, newCycles);
    return failures;
}
