#include <stdio.h>
#include <string.h>
#include "ps2_texture.h"

static RDPState s_saved;
static u8 mask[64*64/2], noise[32*32], previous[64*64*4];

static int clamp(int x) { return x<0?0:x>255?255:x; }
static int sample(int s,int t) { return noise[((t&31)*32+s)&1023]; }

int ps2_flame_smoke(void) {
    RDPState* rdp=&g_rdpState;
    s_saved=*rdp;
    unsigned failures=0, checks=0, changes=0;
    for (int i=0;i<(int)sizeof(mask);i++) mask[i]=(i*37+3)&255;
    for (int i=0;i<(int)sizeof(noise);i++) noise[i]=(i*13+(i/32)*29)&255;
    rdp_setothermode(1u<<20,0,rdp);
    rdp_setcombine(0x00272c60,0x350ce37f,rdp);
    if (!ps2_combiner_flame(rdp)) failures++;
    rdp->textureTile=0;
    rdp->tile[0]=(TileDescriptor){.addr=mask,.width=64,.height=64,
        .fmt=G_IM_FMT_I,.siz=G_IM_SIZ_4b,.line=4,.tmem=0};
    rdp->tile[1]=(TileDescriptor){.addr=noise,.width=32,.height=32,
        .fmt=G_IM_FMT_I,.siz=G_IM_SIZ_8b,.line=4,.tmem=256,.maskt=5,.shifts=1,.shiftt=1};
    rdp->textureLoads[0].source=mask; rdp->textureLoads[0].stride=0;
    rdp->textureLoads[256].source=noise; rdp->textureLoads[256].stride=0;
    for (unsigned pass=0;pass<5;pass++) {
        unsigned scroll=pass==4?126:pass*2, lod=pass==3?127:63;
        u32 prim=pass==3?0xe5a040c0:0xffa000ff, env=0x641e00ff;
        rdp_setprimcolor(lod,prim,rdp); rdp_setenvcolor(0,env,rdp);
        rdp_settilesize(scroll,(1u<<24)|(124u<<12)|(124+scroll),rdp);
        ps2_texture_end_frame(rdp);
        unsigned uploads=rdp->stats.textureUploads;
        ps2_texture_upload(&rdp->tile[0],rdp);
        if (!rdp->tile[0].gsTextureLoaded || rdp->stats.textureUploads!=uploads+1) failures++;
        const u8* pixels=(const u8*)rdp->tile[0].gsTexture.Mem;
        for (int y=0;y<64;y++) for (int x=0;x<64;x++) {
            int s=x*2,t=y*2-(int)scroll;
            int ix=s/4,iy=t>=0?t/4:(t-3)/4,fx=s&3,fy=t&3;
            int values[4]={sample(ix,iy),sample(ix+1,iy),sample(ix,iy+1),sample(ix+1,iy+1)};
            int weights[4]={0};
            if (fx+fy<=4) { weights[0]=4-fx-fy; weights[1]=fx; weights[2]=fy; }
            else { weights[1]=4-fy; weights[2]=4-fx; weights[3]=fx+fy-4; }
            int effect=0;
            for (int i=0;i<4;i++) effect+=weights[i]*values[i];
            effect/=4;
            unsigned packed=mask[(y*64+x)/2];
            int intensity=((x&1)?packed&15:packed>>4)*17;
            for (int c=0;c<4;c++) {
                int p=(prim>>((3-c)*8))&255, e=(env>>((3-c)*8))&255;
                int intermediate=clamp(intensity+(effect-(c==3?255:p))*(int)lod/255);
                int expected=c==3?intermediate*p/255:e+(p-e)*intermediate/255;
                if (c==3) expected=expected*128/255;
                if (pixels[(y*64+x)*4+c]!=expected) {
                    if (!failures) printf("[PS2 FLAME PIXEL] pass=%u xy=%d,%d channel=%d actual=%u expected=%d\n",pass,x,y,c,pixels[(y*64+x)*4+c],expected);
                    failures++;
                }
                checks++;
            }
        }
        if (pass && memcmp(previous,pixels,sizeof(previous))) changes++;
        memcpy(previous,pixels,sizeof(previous));
        uploads=rdp->stats.textureUploads;
        ps2_texture_upload(&rdp->tile[0],rdp);
        if (rdp->stats.textureUploads!=uploads) failures++;
    }
    if (changes!=4) failures++;
    /* Mutating the second source must invalidate the pair even at the same
     * scroll/tint, and another combiner must leave this special path. */
    noise[0]^=0xff;
    unsigned uploads=rdp->stats.textureUploads;
    ps2_texture_upload(&rdp->tile[0],rdp);
    if (rdp->stats.textureUploads!=uploads+1) failures++;
    rdp_setcombine(0,0,rdp);
    if (ps2_combiner_flame(rdp)) failures++;
    *rdp=s_saved; rdp->gs_clamp_valid=false;
    printf("[PS2 FLAME TEST] channels=%u two sources, fractional scroll, alpha, tint, cache and mutation failures=%u\n",checks,failures);
    return failures?1:0;
}
