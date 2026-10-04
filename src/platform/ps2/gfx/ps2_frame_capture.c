/* VIF1 local-to-host transfer adapted from ps2sdk ee/debug/src/screenshot.c.
 * Copyright 2001-2004, ps2dev. Academic Free License version 2.0.
 * License text: LICENSE.ps2sdk. Upstream:
 * https://github.com/ps2dev/ps2sdk/blob/master/ee/debug/src/screenshot.c
 * Unlike the SDK screenshot helper, this handles CT16S and keeps the source
 * framebuffer stride separate from the rectangle being downloaded. */
#include <gsKit.h>
#include <gsInline.h>
#include <dmaKit.h>
#include <kernel.h>
#include <stdio.h>
#include "rdp_to_gs.h"
#include "ps2_offscreen.h"

#define CAPTURE_ROWS 16u
static u32 s_rows[640 * CAPTURE_ROWS] __attribute__((aligned(128)));
static u64 s_finish[4] __attribute__((aligned(64)));

#define CAP_D1_CHCR ((volatile u32*)0x10009000)
#define CAP_D1_MADR ((volatile u32*)0x10009010)
#define CAP_D1_QWC  ((volatile u32*)0x10009020)
#define CAP_CSR ((volatile u64*)0x12001000)
#define CAP_BUSDIR ((volatile u64*)0x12001040)
#define CAP_VIF_STAT ((volatile u32*)0x10003c00)
#define CAP_VIF_FIFO ((volatile u128*)0x10005000)

static int download_rows(GSGLOBAL* gs, unsigned y, unsigned height) {
    unsigned bpp = gs->PSM == GS_PSM_CT16 || gs->PSM == GS_PSM_CT16S ? 2 :
                   gs->PSM == GS_PSM_CT32 ? 4 : 0;
    if (!bpp || (*CAP_D1_CHCR & 0x100)) return 0;
    u32 packet[28] __attribute__((aligned(16)));
    u64* gif = (u64*)(packet + 4);
    packet[0] = 0; packet[1] = (0x06u << 24) | 0x8000;
    packet[2] = 0x13u << 24; packet[3] = (0x50u << 24) | 6;
    gif[0] = GIF_TAG_AD(5); gif[1] = GIF_AD;
    gif[2] = GS_SETREG_BITBLTBUF(gs->ScreenBuffer[gs->ActiveBuffer&1]/256,
                               gs->Width/64,gs->PSM,0,0,gs->PSM);
    gif[3] = GS_BITBLTBUF;
    gif[4] = GS_SETREG_TRXPOS(0,y,0,0,0); gif[5] = GS_TRXPOS;
    gif[6] = GS_SETREG_TRXREG(gs->Width,height); gif[7] = GS_TRXREG;
    gif[8] = 0; gif[9] = GS_FINISH;
    gif[10] = 1; gif[11] = GS_TRXDIR;
    u32 previousChcr = *CAP_D1_CHCR;
    u32 previousImr = GsPutIMR(GsGetIMR() | 0x0200);
    *CAP_CSR = 2;
    FlushCache(0);
    *CAP_D1_QWC = 7; *CAP_D1_MADR = (u32)packet; *CAP_D1_CHCR = 0x101;
    EE_SYNCL();
    while (*CAP_D1_CHCR & 0x100) {}
    while (!(*CAP_CSR & 2)) {}
    while (*CAP_VIF_STAT & 0x1f000000) {}
    *CAP_VIF_STAT = 1u << 23; *CAP_BUSDIR = 1;
    FlushCache(0);
    *CAP_D1_QWC = gs->Width * height * bpp / 16;
    *CAP_D1_MADR = (u32)s_rows; *CAP_D1_CHCR = 0x100;
    EE_SYNCL();
    while (*CAP_D1_CHCR & 0x100) {}
    *CAP_D1_CHCR = previousChcr;
    EE_SYNCL();
    *CAP_VIF_STAT = 0; *CAP_BUSDIR = 0;
    GsPutIMR(previousImr);
    *CAP_CSR = 2;
    static union { u32 words[4]; u128 quad; } unmask __attribute__((aligned(16))) = {{0x06000000,0,0,0}};
    *CAP_VIF_FIFO = unmask.quad;
    return 1;
}

void ps2_capture_frame(RDPState* rdp, const void* descriptor) {
    const PS2RenderTarget* target = descriptor;
    GSGLOBAL* gs = rdp->gs;
    if (!gs || rdp->renderTargetActive || (uintptr_t)target < 0x100000u ||
        (uintptr_t)target > 0x2000000u-sizeof(*target) ||
        target->width != 320 || target->height != 240 ||
        (uintptr_t)target->image < 0x100000u ||
        (uintptr_t)target->image > 0x2000000u-320u*240u*2u ||
        gs->Width <= 0 || gs->Width > 640 || (gs->Width&63) || gs->Height <= 0 ||
        gs->Height > 512 || (gs->PSM != GS_PSM_CT16S &&
        gs->PSM != GS_PSM_CT16 && gs->PSM != GS_PSM_CT32)) return;
    gsKit_set_finish(gs); gsKit_queue_exec(gs); gsKit_finish();
    unsigned outY = 0;
    int ok = 1;
    for (unsigned baseY = 0; baseY < (unsigned)gs->Height && ok; baseY += CAPTURE_ROWS) {
        unsigned count = gs->Height-baseY;
        if (count > CAPTURE_ROWS) count = CAPTURE_ROWS;
        ok = download_rows(gs,baseY,count);
        while (ok && outY < 240) {
            unsigned sy = (2*outY+1)*gs->Height/480;
            if (sy >= baseY+count) break;
            for (unsigned x = 0; x < 320; x++) {
                unsigned sx = (2*x+1)*gs->Width/640;
                unsigned index = (sy-baseY)*gs->Width+sx;
                u16 pixel;
                if (gs->PSM == GS_PSM_CT32) {
                    const volatile u32* rows = (const volatile u32*)((uintptr_t)s_rows|0x20000000u);
                    u32 p = rows[index];
                    pixel = ((p&0xf8)<<8)|((p>>5)&0x7c0)|((p>>18)&0x3e)|1;
                } else {
                    const volatile u16* rows = (const volatile u16*)((uintptr_t)s_rows|0x20000000u);
                    u16 p = rows[index];
                    pixel = ((p&31)<<11)|(((p>>5)&31)<<6)|(((p>>10)&31)<<1)|1;
                }
                u8* out = target->image;
                out[2*(outY*320+x)] = pixel>>8; out[2*(outY*320+x)+1] = pixel;
            }
            outY++;
        }
    }
    /* Readback acknowledges FINISH. Restore gsKit's completed fence before
     * the next GIF queue, without writing any pixels to the displayed image. */
    dmaKit_wait_fast();
    s_finish[0]=GIF_TAG_AD(1); s_finish[1]=GIF_AD;
    s_finish[2]=0; s_finish[3]=GS_FINISH;
    dmaKit_send(DMA_CHANNEL_GIF,s_finish,2); gsKit_finish();
    static unsigned captures;
    if (++captures<=3 || !ok)
        printf("[PS2 FRAME CAPTURE] readback=%d rows=%u psm=%u image=%p\n",ok,outY,gs->PSM,target->image);
}
