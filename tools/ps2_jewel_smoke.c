#include <stdio.h>
#include <string.h>
#include "ps2_texture.h"

static RDPState saved;
static u8 scintillation[64*64/2], noise[32*40], previous[64*64*4];
static int clamp(int value) { return value<0?0:value>255?255:value; }
static int sample(int s,int t) { return noise[(t&31)*40+(s&31)]; }
static int floor_quarter(int value) { return value>=0?value/4:(value-3)/4; }

int ps2_jewel_smoke(void) {
    RDPState* rdp=&g_rdpState;
    saved=*rdp;
    unsigned failures=0, channels=0, changes=0;
    const unsigned frames[]={0,1,2,12,63,127,255};
    for (unsigned i=0;i<sizeof(scintillation);i++) scintillation[i]=(i*37+3)&255;
    memset(noise,0xfe,sizeof(noise));
    for (int y=0;y<32;y++) for (int x=0;x<32;x++) noise[y*40+x]=(x*17+y*43)&255;
    rdp_setothermode(1u<<20,0,rdp);
    // Exact Kokiri Emerald two-cycle combiner, with TEXEL0 as alpha B.
    rdp_setcombine(0x00272c60,0x350c937f,rdp);
    if (!ps2_combiner_jewel(rdp) || ps2_combiner_flame(rdp) || ps2_combiner_token_flame(rdp)) failures++;
    rdp->textureTile=0;
    rdp->tile[0]=(TileDescriptor){.fmt=G_IM_FMT_I,.siz=G_IM_SIZ_4b,
        .line=4,.tmem=0,.masks=6,.maskt=6};
    rdp->tile[1]=(TileDescriptor){.fmt=G_IM_FMT_I,.siz=G_IM_SIZ_8b,
        .line=4,.tmem=256,.masks=5,.maskt=5,.shifts=15,.shiftt=15};
    rdp->textureLoads[0].source=scintillation; rdp->textureLoads[0].stride=0;
    rdp->textureLoads[256].source=noise; rdp->textureLoads[256].stride=40;
    for (unsigned pass=0;pass<sizeof(frames)/sizeof(frames[0]);pass++) {
        unsigned frame=frames[pass], s0=frame*4%256, t0=255-frame*2%256;
        unsigned s1=frame*2%256, t1=255-frame%256;
        u32 prim=pass==5?0xe5a040c0:0xffffa0ff, env=0x00ff00ff;
        unsigned lod=pass==5?63:128;
        rdp_setprimcolor(lod,prim,rdp); rdp_setenvcolor(0,env,rdp);
        rdp_settilesize((s0<<12)|t0,(((s0+252)&4095)<<12)|((t0+252)&4095),rdp);
        rdp_settilesize((s1<<12)|t1,(1u<<24)|(((s1+60)&4095)<<12)|((t1+60)&4095),rdp);
        ps2_texture_end_frame(rdp);
        unsigned uploads=rdp->stats.textureUploads;
        ps2_texture_upload(&rdp->tile[0],rdp);
        if (!rdp->tile[0].gsTextureLoaded || rdp->tile[0].gsTexture.Width!=64 ||
            rdp->tile[0].gsTexture.Height!=64 || rdp->stats.textureUploads!=uploads+1) {
            failures++; continue;
        }
        const u8* pixels=(const u8*)rdp->tile[0].gsTexture.Mem;
        for (int y=0;y<64;y++) for (int x=0;x<64;x++) {
            // Independent three-point interpolation, after each tile's shift/origin.
            int s=(x*4+(int)s0)*2-(int)s1, t=(y*4+(int)t0)*2-(int)t1;
            int ix=floor_quarter(s), iy=floor_quarter(t), fx=s-ix*4, fy=t-iy*4;
            int values[]={sample(ix,iy),sample(ix+1,iy),sample(ix,iy+1),sample(ix+1,iy+1)};
            int weights[]={0,0,0,0}, effect=0;
            if (fx+fy<=4) { weights[0]=4-fx-fy; weights[1]=fx; weights[2]=fy; }
            else { weights[1]=4-fy; weights[2]=4-fx; weights[3]=fx+fy-4; }
            for (unsigned i=0;i<4;i++) effect+=weights[i]*values[i];
            effect/=4;
            unsigned packed=scintillation[(y*64+x)/2];
            int intensity=((x&1)?packed&15:packed>>4)*17;
            for (int c=0;c<4;c++) {
                int p=(prim>>((3-c)*8))&255, e=(env>>((3-c)*8))&255;
                int intermediate=clamp(intensity+(effect-(c==3?intensity:p))*(int)lod/255);
                int expected=c==3?(intermediate*p/255)*128/255:e+(p-e)*intermediate/255;
                if (pixels[(y*64+x)*4+c]!=expected) {
                    if (!failures) printf("[PS2 JEWEL PIXEL] pass=%u xy=%d,%d channel=%d actual=%u expected=%d\n",
                        pass,x,y,c,pixels[(y*64+x)*4+c],expected);
                    failures++;
                }
                channels++;
            }
        }
        if (pass && memcmp(previous,pixels,sizeof(previous))) changes++;
        memcpy(previous,pixels,sizeof(previous));
        uploads=rdp->stats.textureUploads;
        ps2_texture_upload(&rdp->tile[0],rdp);
        if (rdp->stats.textureUploads!=uploads) failures++;
        noise[32]^=0xff; // Padding outside each loaded row must not invalidate.
        ps2_texture_upload(&rdp->tile[0],rdp);
        if (rdp->stats.textureUploads!=uploads) failures++;
    }
    if (changes!=6) failures++;
    unsigned uploads=rdp->stats.textureUploads;
    noise[31*40+31]^=0xff; // Full loaded image, beyond the 16x16 scroll window.
    ps2_texture_upload(&rdp->tile[0],rdp);
    if (rdp->stats.textureUploads!=uploads+1) failures++;
    uploads=rdp->stats.textureUploads;
    scintillation[0]^=0xff;
    ps2_texture_upload(&rdp->tile[0],rdp);
    if (rdp->stats.textureUploads!=uploads+1) failures++;
    *rdp=saved; rdp->gs_clamp_valid=false;
    printf("[PS2 JEWEL TEST] channels=%u independent scrolls, two sources, green tint, alpha and cache failures=%u\n",
        channels,failures);
    return failures!=0;
}
