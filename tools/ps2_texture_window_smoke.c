#include <stdio.h>
#include <string.h>
#include "ps2_texture.h"

/* These tile layouts match Link's hair and tunic: 16 loaded columns,
 * 32 columns in the clamp window. Poison after the image catches decoding
 * the clamp window as RAM, and mutations test what the cache hashes. */
int ps2_texture_window_smoke(void) {
    static RDPState saved;
    static u8 source[4096] __attribute__((aligned(128)));
    static u8 expected[32*32*4];
    RDPState* rdp = &g_rdpState;
    saved = *rdp;
    memset(&rdp->textureLoads[510], 0, sizeof(rdp->textureLoads[510]));
    rdp_setothermode(2u << 20, 0, rdp);
    rdp_setcombine(0, 0, rdp);
    unsigned checks = 0, failures = 0;
    for (unsigned format=0; format<2; format++) {
        unsigned bytes = format ? 1 : 2, height = format ? 32 : 16;
        for (unsigned mode=2; mode<=3; mode++) for (unsigned extent=24; extent<=32; extent+=8) {
            memset(source, 0xa5, sizeof(source));
            for (unsigned y=0; y<height; y++) for (unsigned x=0; x<16; x++) {
                unsigned i=y*16+x;
                if (format) source[i]=(x*11+y*7+3)&255;
                else {
                    u16 raw=(((x*2)&31)<<11)|(((y*2)&31)<<6)|(((x+y)&31)<<1)|1;
                    source[i*2]=raw>>8; source[i*2+1]=raw;
                }
            }
            TileDescriptor tile={.addr=source,.width=extent,.height=height,
                .fmt=format ? G_IM_FMT_I : G_IM_FMT_RGBA,
                .siz=format ? G_IM_SIZ_8b : G_IM_SIZ_16b,
                .cms=mode,.cmt=2,.masks=4,.maskt=format ? 5 : 4,
                .line=bytes*2,.tmem=510};
            ps2_texture_upload(&tile,rdp);
            if (!tile.gsTextureLoaded || tile.gsTexture.Width!=extent || tile.gsTexture.Height!=height) {
                failures++; continue;
            }
            const u8* pixels=(const u8*)tile.gsTexture.Mem;
            for (unsigned y=0; y<height; y++) for (unsigned x=0; x<extent; x++) {
                unsigned sx=x&15;
                if ((mode&1) && (x&16)) sx^=15;
                unsigned i=y*16+sx;
                u8 color[4];
                if (format) color[0]=color[1]=color[2]=source[i],color[3]=source[i]>>1;
                else {
                    u16 raw=(source[i*2]<<8)|source[i*2+1];
                    /* The decoder expands five-bit channels by replication. */
                    color[0]=((raw>>11)<<3)|(raw>>13);
                    color[1]=(((raw>>6)&31)<<3)|((raw>>8)&7);
                    color[2]=(((raw>>1)&31)<<3)|((raw>>3)&7);
                    color[3]=128;
                }
                if (memcmp(pixels+(y*extent+x)*4,color,4)) {
                    if (!failures) printf("[PS2 TILE WINDOW DIFFERENCE] format=%u mode=%u extent=%u x=%u y=%u\n",format,mode,extent,x,y);
                    failures++;
                }
                memcpy(expected+(y*extent+x)*4,color,4);
                checks++;
            }
            ps2_texture_apply_tile(&tile,rdp);
            if (rdp->gs->Clamp->WMS!=2 || rdp->gs->Clamp->MINU!=0 ||
                rdp->gs->Clamp->MAXU!=extent-1 || rdp->gs->Clamp->MAXV!=height-1) failures++;
            u32 vram=tile.gsTexture.Vram, uploads=rdp->stats.textureUploads;
            /* The bytes beyond the loaded 16-column image are unrelated. */
            source[16*height*bytes]^=0xff;
            ps2_texture_end_frame(rdp);
            ps2_texture_upload(&tile,rdp);
            if (rdp->stats.textureUploads!=uploads || tile.gsTexture.Vram!=vram) failures++;
            /* Every loaded byte must still participate in invalidation. */
            source[bytes*15]^=0xff;
            ps2_texture_end_frame(rdp);
            ps2_texture_upload(&tile,rdp);
            if (rdp->stats.textureUploads!=uploads+1 ||
                !memcmp(tile.gsTexture.Mem,expected,extent*height*4)) failures++;
        }
    }
    *rdp=saved;
    rdp->gs_clamp_valid=false;
    printf("[PS2 TILE WINDOW TEST] texels=%u clamp/mirror, source rows, cache bounds and mutation failures=%u\n",checks,failures);
    return failures ? 1 : 0;
}
