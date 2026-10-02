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
void ps2_texture_load_tlut(const u8* tlut_src, int count);
void ps2_texture_load_tlut_at(const u8* tlut_src, int first, int count);
void ps2_texture_apply_tile(TileDescriptor* tile, RDPState* rdp);
void ps2_texture_build_vertex(const TileDescriptor* tile, const RSPState* rsp,
    const RDPState* rdp, const RSPVertex* vertex, u32 color, u32 depth,
    GSPRIMSTQPOINT* output);

#endif /* PS2_TEXTURE_H */
