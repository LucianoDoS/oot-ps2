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
    /* Gohma's eggs use func_80094E78: SETTILESIZE with width/height zero
     * changes the sampling origin. Masked wrap still samples the loaded
     * 16x16 image, including when the lower bound precedes the origin. */
    unsigned scrollFailures=0, scrollChecks=0;
    memset(source,0xa5,sizeof(source));
    for (unsigned i=0;i<256;i++) {
        u16 raw=(((i*3)&31)<<11)|(((i*5)&31)<<6)|(((i*7)&31)<<1)|1;
        source[i*2]=raw>>8; source[i*2+1]=raw;
    }
    rdp_setothermode(2u<<20,0,rdp);
    rdp_setcombine(0,0,rdp);
    rdp->textureTile=0;
    rdp->tile[0]=(TileDescriptor){.fmt=G_IM_FMT_RGBA,.siz=G_IM_SIZ_16b,
        .line=4,.tmem=509,.masks=4,.maskt=4};
    rdp->textureLoads[509].source=source; rdp->textureLoads[509].stride=0;
    for (unsigned pass=0;pass<128;pass++) {
        unsigned x=pass<64?0:pass-64, y=pass<64?pass:4095-(pass-64);
        rdp_settilesize((x<<12)|y,(((x-4)&4095)<<12)|((y-4)&4095),rdp);
        ps2_texture_upload(&rdp->tile[0],rdp);
        TileDescriptor* tile=&rdp->tile[0];
        if (!tile->gsTextureLoaded || tile->gsTexture.Width!=16 || tile->gsTexture.Height!=16) {
            scrollFailures++; continue;
        }
        const u8* pixels=(const u8*)tile->gsTexture.Mem;
        for (unsigned i=0;i<256;i++) {
            u16 raw=(source[i*2]<<8)|source[i*2+1];
            u8 color[4]={((raw>>11)<<3)|(raw>>13),
                (((raw>>6)&31)<<3)|((raw>>8)&7),
                (((raw>>1)&31)<<3)|((raw>>3)&7),128};
            scrollFailures+=memcmp(pixels+i*4,color,4)!=0;
            scrollChecks++;
        }
        ps2_texture_end_frame(rdp);
    }
    printf("[PS2 ZERO SIZE SCROLL TEST] cases=128 texels=%u masked egg scroll and 12-bit wrap failures=%u\n",
        scrollChecks,scrollFailures);
    failures+=scrollFailures;
    /* Owl torso/neck: repeat the 16x16 image across its full clamp window. */
    unsigned owlFailures=0, owlChecks=0;
    rdp->tile[0]=(TileDescriptor){.fmt=G_IM_FMT_RGBA,.siz=G_IM_SIZ_16b,
        .line=4,.tmem=509,.masks=4,.maskt=4,.cms=2,.cmt=2};
    rdp_settilesize(0,(0x09fcu<<12)|0x02fc,rdp);
    ps2_texture_upload(&rdp->tile[0],rdp);
    TileDescriptor* owl=&rdp->tile[0];
    if (!owl->gsTextureLoaded || owl->gsTexture.Width!=640 || owl->gsTexture.Height!=192) owlFailures++;
    else {
        const u8* pixels=(const u8*)owl->gsTexture.Mem;
        for (unsigned y=0;y<192;y++) for (unsigned x=0;x<640;x++) {
            unsigned i=(y&15)*16+(x&15);
            u16 raw=(source[i*2]<<8)|source[i*2+1];
            u8 color[4]={((raw>>11)<<3)|(raw>>13),
                (((raw>>6)&31)<<3)|((raw>>8)&7),
                (((raw>>1)&31)<<3)|((raw>>3)&7),128};
            owlFailures+=memcmp(pixels+(y*640+x)*4,color,4)!=0;
            owlChecks++;
        }
        ps2_texture_apply_tile(owl,rdp);
        if (rdp->gs->Clamp->WMS!=2 || rdp->gs->Clamp->WMT!=2 ||
            rdp->gs->Clamp->MAXU!=639 || rdp->gs->Clamp->MAXV!=191) owlFailures++;
        unsigned uploads=rdp->stats.textureUploads;
        source[512]^=0xff;
        ps2_texture_upload(owl,rdp);
        if (rdp->stats.textureUploads!=uploads) owlFailures++;
    }
    printf("[PS2 OWL WINDOW TEST] texels=%u repeated source, safe clamp and cache failures=%u\n",owlChecks,owlFailures);
    failures+=owlFailures;
    *rdp=saved;
    rdp->gs_clamp_valid=false;
    printf("[PS2 TILE WINDOW TEST] texels=%u clamp/mirror, source rows, cache bounds and mutation failures=%u\n",checks,failures);
    return failures ? 1 : 0;
}
