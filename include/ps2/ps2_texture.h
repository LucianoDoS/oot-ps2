/**
 * ps2_texture.h — Texture system declarations
 */

#ifndef PS2_TEXTURE_H
#define PS2_TEXTURE_H

#include "ps2_types.h"
#include "rdp_to_gs.h"

void ps2_texture_upload(TileDescriptor* tile, RDPState* rdp);
u32 ps2_texture_hash(const u8* source, u32 bytes, u32 seed);
void ps2_texture_end_frame(RDPState* rdp);
void ps2_texture_profile_cache(void);
typedef struct {
    u32 hits, builds, changedEntries;
} PS2PaletteHashStats;
/* Hashes describe decoded CLUT contents; every TLUT load still reads RAM. */
PS2PaletteHashStats ps2_texture_palette_hash_stats(bool reset);
#ifdef PS2_RENDERER_SMOKE_CAPTURE
unsigned ps2_texture_test_cache_capacity(void);
unsigned ps2_texture_test_combiner_lookup(const RDPState* rdp, u8 values[4][256]);
void ps2_texture_test_combiner_rgba16(const RDPState* rdp, u32 values[4][32]);
int ps2_texture_palette_hash_smoke(void);
#endif
void ps2_texture_load_tlut(const u8* tlut_src, int count);
void ps2_texture_load_tlut_at(const u8* tlut_src, int first, int count);
void ps2_texture_apply_tile(TileDescriptor* tile, RDPState* rdp);
/* Prepare after upload, and discard at the next state command or upload of
 * this layer. The consecutive triangle run keeps its descriptor unchanged.
 * Applying still records every GS read of the cache entry for retirement. */
typedef struct {
    u64 clamp;
    u32 modeS, modeT, minS, maxS, minT, maxT, filter;
} PS2TextureTileSetup;
void ps2_texture_prepare_tile(PS2TextureTileSetup* setup,
    const TileDescriptor* tile, const RDPState* rdp);
void ps2_texture_apply_tile_prepared(TileDescriptor* tile, RDPState* rdp,
    const PS2TextureTileSetup* setup);
/* Prepare only after upload resolves the GS dimensions. Valid until the next
 * RSP/RDP state command; vertex coordinates and depth remain independent. */
typedef struct {
    float texScaleS, texScaleT, shiftS, shiftT, offsetS, offsetT;
    float invWidth, invHeight;
    unsigned repeatS, repeatT;
    GSGLOBAL* gs;
    u32 primAlpha;
    bool perspective, dynamicAlpha;
} PS2TextureVertexSetup;
void ps2_texture_prepare_vertices(PS2TextureVertexSetup* setup,
    const TileDescriptor* tile, const RSPState* rsp, const RDPState* rdp);
void ps2_texture_build_vertex_prepared(const PS2TextureVertexSetup* setup,
    const RSPVertex* vertex, u32 color, u32 depth, GSPRIMSTQPOINT* output);
void ps2_texture_build_vertex(const TileDescriptor* tile, const RSPState* rsp,
    const RDPState* rdp, const RSPVertex* vertex, u32 color, u32 depth,
    GSPRIMSTQPOINT* output);

typedef struct {
    float offsetS, offsetT, maxBefore, maxAfter, maxSpan;
    unsigned outsideBefore, outsideAfter;
} PS2TextureWrapResult;
/* Translate an entire triangle by whole repeat periods before forming STQ.
 * Per-vertex modulo would change interpolation across texture seams. */
/* Pass NULL for result in regular gameplay to skip diagnostic measurements. */
void ps2_texture_rebase_triangle(const TileDescriptor* tile,
    const RSPState* rsp, const RDPState* rdp,
    const RSPVertex* const vertices[3], GSPRIMSTQPOINT points[3], bool apply,
    PS2TextureWrapResult* result);
void ps2_texture_vertex_coordinates(const PS2TextureVertexSetup* setup,
    const RSPVertex* vertex, float coordinates[2]);
void ps2_texture_rebase_prepared_triangle(const PS2TextureVertexSetup* setup,
    const float coordinates[2][3], GSPRIMSTQPOINT points[3], bool apply,
    PS2TextureWrapResult* result);

#endif /* PS2_TEXTURE_H */
