/* Render the pause portrait independently of the displayed framebuffer.
 * Readback uses the SDK's VIF1 transfer, after retiring every GS draw, and
 * publishes the original RGBA5551 byte stream consumed by texture decoding. */
#include <gsKit.h>
#include <gsInline.h>
#include <dmaKit.h>
#include <screenshot.h>
#include <stdio.h>
#include "rdp_to_gs.h"
#include "ps2_offscreen.h"

static u32 s_color = GSKIT_ALLOC_ERROR, s_depth = GSKIT_ALLOC_ERROR;
static u32 s_pixels[64 * 128] __attribute__((aligned(128)));
static PS2RenderTarget s_target;
static u32 s_savedZ, s_savedPSMZ;
static void* s_savedColorImage;
static void* s_savedDepthImage;
static GSGLOBAL* s_owner;
static u64 s_finishPacket[4] __attribute__((aligned(64)));

void ps2_offscreen_init(GSGLOBAL* gs) {
    s_target.image = NULL;
    if (s_owner == gs) return;
    s_owner = gs;
    /* Allocate before the texture pool so its eviction ranges cannot alias
     * this image. CT32 and Z24 each occupy four 8 KiB GS pages. */
    s_color = gsKit_vram_alloc(gs, gsKit_texture_size(64,128,GS_PSM_CT32), GSKIT_ALLOC_SYSBUFFER);
    s_depth = gsKit_vram_alloc(gs, gsKit_texture_size(64,128,GS_PSM_CT32), GSKIT_ALLOC_SYSBUFFER);
}

static void set_frame(RDPState* rdp, u32 address, u32 width, u32 psm) {
    u64* p = gsKit_heap_alloc(rdp->gs,1,16,GIF_AD);
    p[0]=GIF_TAG_AD(1); p[1]=GIF_AD;
    p[2]=GS_SETREG_FRAME(address/8192,width/64,psm,0);
    p[3]=GS_FRAME_1+rdp->gs->PrimContext;
}

void ps2_offscreen_begin(RDPState* rdp, const PS2RenderTarget* target) {
    if (!rdp->gs || rdp->renderTargetActive || (uintptr_t)target < 0x100000u ||
        (uintptr_t)target > 0x2000000u-sizeof(*target) || !target->image ||
        target->width != 64 || !target->height || target->height > 128 ||
        (uintptr_t)target->image < 0x100000u ||
        (uintptr_t)target->image > 0x2000000u-128u*target->height ||
        s_color == GSKIT_ALLOC_ERROR || s_depth == GSKIT_ALLOC_ERROR) return;
    s_target=*target;
    s_savedZ=rdp->gs->ZBuffer; s_savedPSMZ=rdp->gs->PSMZ;
    s_savedColorImage=rdp->colorImage; s_savedDepthImage=rdp->zImage;
    rdp->renderTargetActive=true;
    rdp->gs->ZBuffer=s_depth; rdp->gs->PSMZ=GS_PSMZ_24;
    set_frame(rdp,s_color,64,GS_PSM_CT32);
    ps2_gs_set_zbuf(rdp,GS_SETREG_ZBUF(s_depth/8192,GS_PSMZ_24&0xf,!rdp->zWriteEnabled));
}

void ps2_offscreen_end(RDPState* rdp) {
    if (!rdp->gs || !rdp->renderTargetActive) return;
    GSGLOBAL* gs=rdp->gs;
    gsKit_set_finish(gs);
    gsKit_queue_exec(gs);
    gsKit_finish();
    int ok=ps2_screenshot(s_pixels,s_color/256,0,0,64,s_target.height,GS_PSM_CT32);
    /* The SDK transfer acknowledges FINISH on return. gsKit waits for the
     * previous FINISH before submitting its next queue, so publish a fresh
     * completed fence through GIF rather than leaving that queue waiting
     * for an event it cannot itself submit. */
    dmaKit_wait_fast();
    s_finishPacket[0]=GIF_TAG_AD(1); s_finishPacket[1]=GIF_AD;
    s_finishPacket[2]=0; s_finishPacket[3]=GS_FINISH;
    dmaKit_send(DMA_CHANNEL_GIF,s_finishPacket,2);
    gsKit_finish();
    if (ok) {
        const volatile u32* pixels=(const volatile u32*)((uintptr_t)s_pixels|0x20000000u);
        u8* out=s_target.image;
        for (unsigned i=0;i<64u*s_target.height;i++) {
            u32 p=pixels[i];
            u16 rgba=((p&0xf8)<<8)|((p>>5)&0x7c0)|((p>>18)&0x3e)|((p>>24)>=64);
            out[2*i]=rgba>>8; out[2*i+1]=rgba;
        }
    }
    static unsigned captures;
    if (++captures<=3 || !ok)
        printf("[PS2 PORTRAIT] readback=%d image=%p size=64x%u\n",ok,s_target.image,s_target.height);
    gs->ZBuffer=s_savedZ; gs->PSMZ=s_savedPSMZ;
    rdp->colorImage=s_savedColorImage; rdp->zImage=s_savedDepthImage;
    rdp->renderTargetActive=false;
    set_frame(rdp,gs->ScreenBuffer[gs->ActiveBuffer&1],gs->Width,gs->PSM);
    ps2_gs_set_zbuf(rdp,GS_SETREG_ZBUF(gs->ZBuffer/8192,gs->PSMZ&0xf,!rdp->zWriteEnabled));
    gsKit_set_scissor(gs,GS_SETREG_SCISSOR(0,gs->Width-1,0,gs->Height-1));
    rdp->gs_clamp_valid=false;
}
