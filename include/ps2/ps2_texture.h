/**
 * ps2_texture.h — Texture system declarations
 */

#ifndef PS2_TEXTURE_H
#define PS2_TEXTURE_H

#include "ps2_types.h"
#include "rdp_to_gs.h"

void ps2_texture_upload(TileDescriptor* tile, RDPState* rdp);
void ps2_texture_end_frame(RDPState* rdp);
void ps2_texture_load_tlut(const u8* tlut_src, int count);

#endif /* PS2_TEXTURE_H */
