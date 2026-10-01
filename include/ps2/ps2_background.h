#ifndef PS2_BACKGROUND_H
#define PS2_BACKGROUND_H
/* Plain types keep this bridge compatible with both game and SDK headers. */
int ps2_jpeg_decode(void* data, unsigned int capacity, unsigned int width, unsigned int height);
typedef struct {
    unsigned short imageX, imageW;
    short frameX;
    unsigned short frameW, imageY, imageH;
    short frameY;
    unsigned short frameH;
    void* imagePtr;
    unsigned short imageLoad;
    unsigned char imageFmt, imageSiz;
    unsigned short imagePal, imageFlip, scaleW, scaleH;
    int imageYorig;
    unsigned char padding[4];
} PS2Background;
_Static_assert(sizeof(PS2Background) == 40, "native S2DEX background layout");
#endif
