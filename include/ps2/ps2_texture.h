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
#ifdef PS2_RENDERER_SMOKE_CAPTURE
unsigned ps2_texture_test_cache_capacity(void);
unsigned ps2_texture_test_combiner_lookup(const RDPState* rdp, u8 values[4][256]);
void ps2_texture_test_combiner_rgba16(const RDPState* rdp, u32 values[4][32]);
#endif
void ps2_texture_load_tlut(const u8* tlut_src, int count);
void ps2_texture_load_tlut_at(const u8* tlut_src, int first, int count);
void ps2_texture_apply_tile(TileDescriptor* tile, RDPState* rdp);
/* Prepare only after upload resolves the GS dimensions. Valid until the next
 * RSP/RDP state command; vertex coordinates and depth remain independent. */
typedef struct {
    float texScaleS, texScaleT, shiftS, shiftT, offsetS, offsetT;
    float invWidth, invHeight;
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

#endif /* PS2_TEXTURE_H */
