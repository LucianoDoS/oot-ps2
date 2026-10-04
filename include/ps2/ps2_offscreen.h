#ifndef PS2_OFFSCREEN_H
#define PS2_OFFSCREEN_H

/* Native display-list extensions, carried by otherwise unused NOOP tags. */
#define PS2_GFX_TARGET_BEGIN 0x00F25001u
#define PS2_GFX_TARGET_END   0x00F25002u
#define PS2_GFX_CAPTURE_FRAME 0x00F25003u
typedef struct {
    void* image;
    unsigned short width, height;
} PS2RenderTarget;

#endif
