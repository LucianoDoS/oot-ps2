/**
 * ps2_init.h — PS2 System Initialization for OoT Port
 *
 * Centralizes all PS2 subsystem setup: kernel, DMA, GS, GIF,
 * SPU2, controller and basic memory layout.
 */

#ifndef PS2_INIT_H
#define PS2_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

/* ── PS2 SDK headers ─────────────────────────────────────── */
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libpad.h>
#include <dmaKit.h>
#include <gsKit.h>

/* ── OoT platform config ─────────────────────────────────── */
#include "ps2_types.h"

/* ─────────────────────────────────────────────────────────── *
 *  Video / GS configuration
 * ─────────────────────────────────────────────────────────── */
/* OoT originally renders at 320x240 (N64 LowRes mode).
 * On PS2 we target 640x448 NTSC interlaced (standard TV output)
 * and scale the internal framebuffer accordingly. */
#define PS2_SCREEN_W       640
#define PS2_SCREEN_H       448
#define PS2_INTERLACED     GS_INTERLACED
#define PS2_VIDEO_MODE     GS_MODE_NTSC
#define PS2_FIELD          GS_FIELD

/* Internal render resolution (matches OoT original) */
#define OOT_RENDER_W       320
#define OOT_RENDER_H       240

/* ─────────────────────────────────────────────────────────── *
 *  Memory layout helpers
 * ─────────────────────────────────────────────────────────── */
#define KB(n)   ((n)  * 1024)
#define MB(n)   ((n)  * 1024 * 1024)

/* OoT heap: the game uses osMemSize to determine available RAM.
 * On PS2 we report 8 MB to stay compatible with N64 allocations. */
#define OOT_HEAP_SIZE      MB(8)
#define OOT_HEAP_BASE      0x00800000  /* 8 MB offset in EE RAM */

/* ─────────────────────────────────────────────────────────── *
 *  Global GS/gsKit context (defined in ps2_main.c)
 * ─────────────────────────────────────────────────────────── */
extern GSGLOBAL* g_gsGlobal;
extern GSTEXTURE g_framebuffer[2]; /* double buffering */

/* ─────────────────────────────────────────────────────────── *
 *  Public API
 * ─────────────────────────────────────────────────────────── */

/**
 * ps2_init_all() — Top-level system initialization.
 * Must be called first in main() before anything else.
 * Initializes: SIF, DMA, GS, IOP modules (pad, audio).
 */
void ps2_init_all(void);

/**
 * ps2_init_gs() — Initialize the Graphics Synthesizer.
 * Sets up the GS for 640x448 NTSC interlaced output,
 * allocates VRAM for framebuffers and z-buffer.
 */
void ps2_init_gs(void);

/**
 * ps2_init_iop() — Load required IOP modules via SIF.
 * Loads: LIBSD, AUDSRV (audio), PADMAN (controller).
 */
void ps2_init_iop(void);

/**
 * ps2_init_pad() — Initialize DualShock 2 controller.
 * Sets up both ports (0 and 1); OoT uses port 0.
 */
void ps2_init_pad(void);

/**
 * ps2_shutdown() — Graceful system shutdown.
 * Called on game exit or reset.
 */
void ps2_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* PS2_INIT_H */
