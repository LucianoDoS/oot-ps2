#ifndef PS2_TRIANGLE_PACKET_H
#define PS2_TRIANGLE_PACKET_H
#include <stdbool.h>
#include <gsKit.h>
/* REGLIST carries the same 64-bit GS registers as gsKit's A_D vertices,
 * without repeating a 64-bit register address for every attribute. */
typedef struct { u64 tex0, prim; u32 filter; } PS2TriangleTextureSetup;
/* Prepare after texture upload; valid until a state command or reupload. */
void ps2_triangle_prepare_texture(PS2TriangleTextureSetup* setup, const GSGLOBAL* gs,
    const GSTEXTURE* texture);
void ps2_prim_prepared_textured_triangles(GSGLOBAL* gs, const PS2TriangleTextureSetup* setup,
    GSTEXTURE* texture, int count, const GSPRIMSTQPOINT* points, bool setFilter);
void ps2_prim_textured_triangles(GSGLOBAL* gs, GSTEXTURE* texture, int count,
    const GSPRIMSTQPOINT* points, bool setFilter);
void ps2_prim_colored_triangles(GSGLOBAL* gs, int count, const GSPRIMPOINT* points);
#endif
