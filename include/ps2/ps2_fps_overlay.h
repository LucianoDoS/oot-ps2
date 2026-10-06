#ifndef PS2_FPS_OVERLAY_H
#define PS2_FPS_OVERLAY_H
#include "rdp_to_gs.h"
/* Append only after the game has finished its main framebuffer commands.
 * No textures/VRAM allocation, disk I/O or change to CPU-side GS state. */
void ps2_fps_overlay_draw(RDPState* rdp, unsigned fps10, bool fallback);
#endif
