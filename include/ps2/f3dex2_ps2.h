/**
 * f3dex2_ps2.h — F3DEX2 Geometry Command Declarations
 */

#ifndef F3DEX2_PS2_H
#define F3DEX2_PS2_H

#include "ps2_types.h"
#include "rdp_to_gs.h"

/* Geometry commands */
void f3dex2_vtx(u32 w0, u32 w1, RSPState* rsp);
void f3dex2_tri1(u32 w0, u32 w1, RSPState* rsp, RDPState* rdp);
void f3dex2_tri2(u32 w0, u32 w1, RSPState* rsp, RDPState* rdp);
void f3dex2_mtx(u32 w0, u32 w1, RSPState* rsp);
void f3dex2_popmtx(u32 w0, u32 w1, RSPState* rsp);
void f3dex2_geometrymode(u32 w0, u32 w1, RSPState* rsp);
void f3dex2_texture(u32 w0, u32 w1, RSPState* rsp, RDPState* rdp);
void f3dex2_moveword(u32 w0, u32 w1, RSPState* rsp);
void f3dex2_movemem(u32 w0, u32 w1, RSPState* rsp);

/* Texture upload */
void ps2_texture_upload(TileDescriptor* tile, RDPState* rdp);
void ps2_texture_load_tlut(const u8* tlut_src, int count);

#endif /* F3DEX2_PS2_H */
