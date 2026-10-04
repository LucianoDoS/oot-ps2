/* Actual GS render + VIF1 readback, not a CPU imitation of the portrait. */
#include <stdio.h>
#include <string.h>
#include "rdp_to_gs.h"
#include "ps2_offscreen.h"
#include "ps2_segment.h"
#include "f3dex2_ps2.h"
#include "ps2_texture.h"

static RDPState savedRdp;
static RSPState savedRsp;
static struct { u8 before[128], image[64*112*2], after[128]; } buffer __attribute__((aligned(128)));
static s16 viewport[8] = {128,224,511,0,128,224,511,0};

int ps2_offscreen_smoke(void) {
    int failures=0;
    savedRdp=g_rdpState; savedRsp=g_rspState;
    GSGLOBAL* gs=g_rdpState.gs;
    u32 oldZ=gs->ZBuffer, oldPSMZ=gs->PSMZ;
    memset(&buffer,0xa5,sizeof(buffer));
    PS2RenderTarget target={buffer.image,64,112};
    for (int pass=0;pass<2;pass++) {
        u16 first=pass ? 0x003f : 0xf801;
        const u32 commands[][2] __attribute__((aligned(8))) = {
            {PS2_GFX_TARGET_BEGIN,(u32)(uintptr_t)&target},
            {0xed000000,(64u<<14)|(112u<<2)},
            {0xef300000,0}, /* FILL, no alpha or depth test. */
            {0xfe000000,0x400000},
            {0xff10003f,0x400000},
            {0xf7000000,0xffffffff},
            {0xf6000000|(63u<<14)|(111u<<2),0}, /* Depth clear. */
            {0xff10003f,(u32)(uintptr_t)buffer.image},
            {0xf7000000,0},
            {0xf6000000|(63u<<14)|(111u<<2),0}, /* Transparent clear. */
            {0xdc080008,(u32)(uintptr_t)viewport},
            {0xf7000000,((u32)first<<16)|first},
            {0xf6000000|(24u<<14)|(40u<<2),(8u<<14)|(16u<<2)},
            {0xf7000000,0x07c107c1},
            {0xf6000000|(55u<<14)|(96u<<2),(40u<<14)|(64u<<2)},
            {PS2_GFX_TARGET_END,0},
            {0xdf000000,0}
        };
        rdp_to_gs_exec((const Gfx*)commands);
        printf("[PS2 OFFSCREEN PHASE] pass=%d render returned\n",pass);
        if (g_rdpState.renderTargetActive || gs->ZBuffer!=oldZ || gs->PSMZ!=oldPSMZ ||
            g_rspState.vpScaleX!=32 || g_rspState.vpScaleY!=-56 ||
            g_rspState.vpTransX!=32 || g_rspState.vpTransY!=56 ||
            ps2_render_scale_x(&g_rdpState)!=gs->Width/320.0f) failures++;
        int pixelsFailed=0;
        for (int y=0;y<112;y++) for (int x=0;x<64;x++) {
            u16 expected=x>=8 && x<=24 && y>=16 && y<=40 ? first :
                x>=40 && x<=55 && y>=64 && y<=96 ? 0x07c1 : 0;
            const u8* p=buffer.image+2*(y*64+x);
            if (read_be16(p)!=expected) {
                if (!pixelsFailed) printf("[PS2 OFFSCREEN PIXEL] pass=%d xy=%d,%d actual=%04x expected=%04x\n",pass,x,y,read_be16(p),expected);
                pixelsFailed++;
            }
        }
        failures+=pixelsFailed;
        for (int i=0;i<128;i++) if (buffer.before[i]!=0xa5 || buffer.after[i]!=0xa5) failures++;
        /* Existing decoder must consume the downloaded big-endian RGBA16,
         * and notice the second render even though its RAM address is fixed. */
        g_rdpState.otherMode.hi=2u<<20;
        TileDescriptor tile={.width=64,.height=32,.fmt=G_IM_FMT_RGBA,.siz=G_IM_SIZ_16b,.cms=2,.cmt=2,.addr=buffer.image};
        g_rdpState.textureLoads[0].source=buffer.image;
        g_rdpState.textureLoads[0].stride=128;
        unsigned uploads=g_rdpState.stats.textureUploads;
        printf("[PS2 OFFSCREEN PHASE] pass=%d pixelsFailed=%d bind\n",pass,pixelsFailed);
        ps2_texture_upload(&tile,&g_rdpState);
        printf("[PS2 OFFSCREEN PHASE] pass=%d bound\n",pass);
        if (!tile.gsTextureLoaded || g_rdpState.stats.textureUploads!=uploads+1) failures++;
        const u8* color=(const u8*)tile.gsTexture.Mem+4*(16*64+8);
        if (color[0]!=(pass?0:255) || color[1]!=0 || color[2]!=(pass?255:0) || color[3]!=128) failures++;
    }
    g_rdpState=savedRdp; g_rspState=savedRsp;
    ps2_gs_state_reset();
    g_rdpState.gs_clamp_valid=false;
    rdp_apply_othermode(&g_rdpState);
    printf("[PS2 OFFSCREEN TEST] 14336 pixels, alpha, viewport, depth, bounds and mutable texture failures=%d\n",failures);
    return failures;
}
