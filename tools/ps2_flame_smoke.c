#include <stdio.h>
#include <string.h>
#include "ps2_texture.h"

static RDPState s_saved;
static u8 mask[64*64/2], noise[32*32], previous[64*64*4];

static int clamp(int x) { return x<0?0:x>255?255:x; }
static int sample(int s,int t) { return noise[((t&31)*32+s)&1023]; }

static unsigned flame_noise_reuse_smoke(RDPState* rdp) {
    static u8 masks[2][64*64/2], sources[2][32*40];
    static u32 actual[64*64], previousGrid[64*64];
    const unsigned expectedHits[]={0,1,2,2,3,3,3,3,3,3};
    const unsigned expectedBuilds[]={1,1,1,2,2,3,4,5,6,7};
    const unsigned expectedBytes[]={0,1024,2048,3072,4096,5120,5120,5120,5120,6144};
    unsigned failures=0, channels=0;
    u32 checksum[2]={0,0};
    PS2FlameNoiseStats stats[2];
    for (unsigned path=0;path<2;path++) {
        ps2_texture_test_flame_noise_reference(path==0);
        for (unsigned i=0;i<sizeof(masks[0]);i++) {
            masks[0][i]=(i*37+3)&255;
            masks[1][i]=(i*19+0x81)&255;
        }
        memset(sources,0xfe,sizeof(sources));
        for (unsigned source=0;source<2;source++) for (unsigned y=0;y<32;y++)
            for (unsigned x=0;x<32;x++)
                sources[source][y*40+x]=(x*13+y*29+source*71)&255;
        rdp->primColor=0xffa000ff; rdp->envColor=0x641e00ff; rdp->primLodFrac=63;
        TileDescriptor effect={.uls=1,.ult=3};
        for (unsigned pass=0;pass<10;pass++) {
            const u8* source=sources[pass==6];
            unsigned stride=pass==7?32:40;
            const u8* maskSource=masks[pass!=0];
            if (pass==2) {
                /* Tint and LOD change the color LUT, not the filtered noise. */
                rdp->primColor=0xe5a040c0; rdp->envColor=0x175bd9ff; rdp->primLodFrac=191;
            }
            if (pass==3) sources[0][31*40+31]^=0xff; /* loaded byte, same frame */
            if (pass==4) sources[0][31*40+32]^=0xff; /* excluded row padding */
            if (pass==5) { effect.uls=126; effect.ult=127; }
            if (pass==9) { effect.uls=4095; effect.ult=4095; }
            /* The direct converter bypasses the VRAM cache. All passes use
             * one frame, including same-layout mutation and padding changes. */
            ps2_texture_test_flame_convert(maskSource,source,stride,&effect,rdp,actual);
            for (int y=0;y<64;y++) for (int x=0;x<64;x++) {
                int s=x*2-(int)effect.uls, t=y*2-(int)effect.ult;
                int ix=s>=0?s/4:(s-3)/4, iy=t>=0?t/4:(t-3)/4;
                int fx=s-ix*4, fy=t-iy*4;
                const int offsets[4]={0,1,32,33};
                int weights[4]={0};
                if (fx+fy<=4) { weights[0]=4-fx-fy; weights[1]=fx; weights[2]=fy; }
                else { weights[1]=4-fy; weights[2]=4-fx; weights[3]=fx+fy-4; }
                int filtered=0, base=(iy&31)*32+ix;
                for (unsigned i=0;i<4;i++) {
                    unsigned at=(base+offsets[i])&1023;
                    filtered+=weights[i]*source[(at/32)*stride+(at%32)];
                }
                filtered/=4;
                unsigned at=y*64+x, packed=maskSource[at/2];
                int intensity=((at&1)?packed&15:packed>>4)*17;
                u32 expected=0;
                for (unsigned c=0;c<4;c++) {
                    int p=(rdp->primColor>>((3-c)*8))&255, e=(rdp->envColor>>((3-c)*8))&255;
                    int intermediate=clamp(intensity+(filtered-(c==3?255:p))*(int)rdp->primLodFrac/255);
                    int value=c==3?intermediate*p/255:e+(p-e)*intermediate/255;
                    if (c==3) value=value*128/255;
                    expected|=(u32)value<<(c*8);
                    channels++;
                }
                if (actual[at]!=expected) {
                    if (!failures) printf("[PS2 FLAME GRID PIXEL] path=%u pass=%u xy=%d,%d actual=%08x expected=%08x\n",
                        path,pass,x,y,actual[at],expected);
                    failures++;
                }
                checksum[path]+=actual[at];
            }
            if (pass==3) memcpy(previousGrid,actual,sizeof(actual));
            if (pass==4) failures+=memcmp(previousGrid,actual,sizeof(actual))!=0;
            PS2FlameNoiseStats current=ps2_texture_flame_noise_stats(false);
            failures+=current.hits!=(path?expectedHits[pass]:0) ||
                current.builds!=(path?expectedBuilds[pass]:0) ||
                current.comparedBytes!=(path?expectedBytes[pass]:0);
        }
        stats[path]=ps2_texture_flame_noise_stats(false);
    }
    failures+=checksum[0]!=checksum[1] || stats[1].storageBytes!=5184;
    printf("[PS2 FLAME NOISE TEST] channels=%u hits=%u builds=%u comparedBytes=%u storageBytes=%u checksums=%u,%u two masks, tint/LOD, same-frame mutation, stride/padding and fractional/wrapped scroll failures=%u\n",
        channels,stats[1].hits,stats[1].builds,stats[1].comparedBytes,stats[1].storageBytes,
        checksum[0],checksum[1],failures);
    ps2_texture_test_flame_noise_reference(false);
    return failures;
}

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
    failures+=flame_noise_reuse_smoke(rdp);
    rdp_setcombine(0,0,rdp);
    if (ps2_combiner_flame(rdp)) failures++;
    *rdp=s_saved; rdp->gs_clamp_valid=false;
    printf("[PS2 FLAME TEST] channels=%u two sources, fractional scroll, alpha, tint, cache and mutation failures=%u\n",checks,failures);
    return failures?1:0;
}
