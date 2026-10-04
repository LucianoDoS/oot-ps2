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
/* Only consecutive primitive commands may share a batch. Flush before any
 * state change, vertex load, display-list branch or end command. */
bool f3dex2_batch_begin(RSPState* rsp, RDPState* rdp);
void f3dex2_batch_end(RDPState* rdp);
#ifdef PS2_RENDERER_SMOKE_CAPTURE
void f3dex2_test_capture(bool enabled);
const GSPRIMSTQPOINT* f3dex2_test_points(unsigned* count);
/* Force the original initial viewport calculation for differential tests. */
void f3dex2_test_geometry_reference(bool enabled);
#endif
void f3dex2_mtx(u32 w0, u32 w1, RSPState* rsp);
void f3dex2_popmtx(u32 w0, u32 w1, RSPState* rsp);
void f3dex2_geometrymode(u32 w0, u32 w1, RSPState* rsp);
void f3dex2_texture(u32 w0, u32 w1, RSPState* rsp, RDPState* rdp);
void f3dex2_moveword(u32 w0, u32 w1, RSPState* rsp);
void f3dex2_movemem(u32 w0, u32 w1, RSPState* rsp);
/* Output has room for 12 vertices; result is a convex homogeneous polygon. */
int ps2_clip_triangle(const RSPVertex* a, const RSPVertex* b, const RSPVertex* c,
                      RSPVertex* output);

/* Texture upload */
void ps2_texture_upload(TileDescriptor* tile, RDPState* rdp);
void ps2_texture_load_tlut(const u8* tlut_src, int count);

#endif /* F3DEX2_PS2_H */
