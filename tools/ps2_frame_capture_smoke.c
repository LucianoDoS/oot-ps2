#include <stdio.h>
#include <string.h>
#include "rdp_to_gs.h"
#include "ps2_offscreen.h"
#include "ps2_segment.h"

static RDPState s_saved;
static struct { u8 before[128], image[320*240*2], after[128]; } buffer __attribute__((aligned(128)));

int ps2_frame_capture_smoke(void) {
    s_saved=g_rdpState;
    memset(&buffer,0xa5,sizeof(buffer));
    PS2RenderTarget target={buffer.image,320,240};
    int failures=0;
    GSGLOBAL* gs=g_rdpState.gs;
    int width=gs->Width,height=gs->Height;
    /* Malformed native commands must not start an oversized DMA or touch
     * their output. Validate signed screen dimensions before arithmetic. */
    gs->Width=-64; ps2_capture_frame(&g_rdpState,&target); gs->Width=width;
    gs->Height=-16; ps2_capture_frame(&g_rdpState,&target); gs->Height=height;
    target.width=64; ps2_capture_frame(&g_rdpState,&target); target.width=320;
    ps2_capture_frame(&g_rdpState,NULL);
    for (unsigned i=0;i<sizeof(buffer);i++) if (((u8*)&buffer)[i]!=0xa5) failures++;
    for (unsigned pass=0;pass<3;pass++) {
        u16 top=pass==1?0x003f:0xf801, bottom=pass==1?0xf801:0x003f;
        const u32 commands[][2] __attribute__((aligned(8)))={
            {0xed000000,(320u<<14)|(240u<<2)},
            {0xef300000,0},
            {0xfe000000,0x400000}, {0xff10013f,0x500000},
            {0xf7000000,((u32)top<<16)|top},
            {0xf6000000|(319u<<14)|(119u<<2),0},
            {0xf7000000,((u32)bottom<<16)|bottom},
            {0xf6000000|(319u<<14)|(239u<<2),120u<<2},
            {PS2_GFX_CAPTURE_FRAME,(u32)&target},
            {0xdf000000,0}
        };
        /* Third capture repeats the previous image with no writes. That
         * catches a readback which corrupts the displayed framebuffer. */
        if (pass==2) {
            const u32 readOnly[][2]={{PS2_GFX_CAPTURE_FRAME,(u32)&target},{0xdf000000,0}};
            rdp_to_gs_exec((const Gfx*)readOnly);
            top=0x003f; bottom=0xf801;
        } else rdp_to_gs_exec((const Gfx*)commands);
        unsigned bad=0;
        for (int y=0;y<240;y++) for (int x=0;x<320;x++) {
            u16 actual=read_be16(buffer.image+2*(y*320+x));
            u16 expected=y<120?top:bottom;
            if (actual!=expected) {
                if (!bad) printf("[PS2 CAPTURE PIXEL] pass=%u x=%d y=%d actual=%04x expected=%04x\n",pass,x,y,actual,expected);
                bad++;
            }
        }
        failures+=bad;
        for (int i=0;i<128;i++) if (buffer.before[i]!=0xa5 || buffer.after[i]!=0xa5) failures++;
    }
    /* Exercise the pause's real MODULATEIA combiner and XLU blender on GS.
     * A transparent, partial and opaque IA8 panel must respectively retain,
     * mix with and cover the captured blue scene behind it. */
    static u8 panels[3][64] __attribute__((aligned(128)));
    int oldAlpha=g_rdpState.gs->PrimAlphaEnable;
    g_rdpState.gs->PrimAlphaEnable=GS_SETTING_ON;
    g_rdpState.textureTile=0;
    for (unsigned panel=0;panel<3;panel++) {
        memset(panels[panel],panel==0?0xf0:panel==1?0xf8:0xff,64);
        unsigned left=20+panel*40,right=left+32;
        const u32 commands[][2] __attribute__((aligned(8)))={
            {0xef000000,0x00404000},
            {0xfc121824,0xff33ffff},
            {0xfd680007,(u32)panels[panel]},
            {0xf5680200,0x00080200},
            {0xf3000000,63u<<12},
            {0xf2000000,(28u<<12)|28},
            {0xe4000000|(right<<14)|(52u<<2),(left<<14)|(20u<<2)},
            {0xe1000000,0}, {0xf1000000,0x04000400},
            {0xdf000000,0}
        };
        rdp_to_gs_exec((const Gfx*)commands);
    }
    const u32 capture[][2]={{PS2_GFX_CAPTURE_FRAME,(u32)&target},{0xdf000000,0}};
    rdp_to_gs_exec((const Gfx*)capture);
    for (unsigned panel=0;panel<3;panel++) {
        u16 pixel=read_be16(buffer.image+2*(32*320+28+panel*40));
        int bad=panel==0?pixel!=0x003f:panel==2?pixel!=0xffff:
            ((pixel>>11)<15 || (pixel>>11)>17 || ((pixel>>6)&31)<15 ||
             ((pixel>>6)&31)>17 || ((pixel>>1)&31)!=31);
        if (bad) { printf("[PS2 PANEL ALPHA PIXEL] panel=%u actual=%04x\n",panel,pixel); failures++; }
    }
    g_rdpState.gs->PrimAlphaEnable=oldAlpha;
    g_rdpState=s_saved;
    ps2_gs_state_reset();
    rdp_apply_othermode(&g_rdpState);
    printf("[PS2 FRAME CAPTURE TEST] 230400 pixels, byte order, bounds, repeated capture, queue fence and IA8 panel blending failures=%d\n",failures);
    return failures;
}
