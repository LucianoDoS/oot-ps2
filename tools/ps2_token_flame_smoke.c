#include <stdio.h>
#include <string.h>
#include "ps2_texture.h"

static RDPState s_saved;
static u8 noise[32*40], mask[64*40], previous[32*64*4];

static int clamp(int value) { return value<0?0:value>255?255:value; }
static int sample(int x, int y) { return noise[(y&31)*40+(x&31)]; }

int ps2_token_flame_smoke(void) {
    RDPState* rdp=&g_rdpState;
    s_saved=*rdp;
    unsigned failures=0, checks=0, changes=0, transparent=0;
    memset(noise,0xfe,sizeof(noise)); memset(mask,0xfe,sizeof(mask));
    for (int y=0;y<32;y++) for (int x=0;x<32;x++) noise[y*40+x]=(x*17+y*43)&255;
    for (int y=0;y<64;y++) for (int x=0;x<32;x++)
        mask[y*40+x]=(x<4 || x>27 || y<5 || y>58)?0:(x*31+y*13)&255;
    rdp_setothermode(1u<<20,0,rdp);
    rdp_setcombine(0xfc171c60,0x35fd6578,rdp);
    if (!ps2_combiner_token_flame(rdp) || ps2_combiner_flame(rdp)) failures++;
    rdp->textureTile=0;
    rdp->tile[0]=(TileDescriptor){.addr=noise,.width=32,.height=32,
        .fmt=G_IM_FMT_I,.siz=G_IM_SIZ_8b,.line=4,.tmem=0,.masks=5,.maskt=5};
    rdp->tile[1]=(TileDescriptor){.addr=mask,.width=32,.height=64,
        .fmt=G_IM_FMT_I,.siz=G_IM_SIZ_8b,.line=4,.tmem=256,.masks=5,.maskt=6,.cms=2,.cmt=2};
    rdp->textureLoads[0].source=noise; rdp->textureLoads[0].stride=40;
    rdp->textureLoads[256].source=mask; rdp->textureLoads[256].stride=40;
    for (unsigned pass=0;pass<5;pass++) {
        unsigned scroll=pass==4?4095:pass*19, lod=pass==3?191:128;
        u32 prim=pass==3?0xff804040:0xaaffaaff, env=0x0000ffff;
        rdp_setprimcolor(lod,prim,rdp); rdp_setenvcolor(0,env,rdp);
        rdp_settilesize(scroll,(124u<<12)|((scroll+124)&4095),rdp);
        // The tile's extents retain their size when the 12-bit scroll wraps.
        rdp->tile[0].width=32; rdp->tile[0].height=32;
        ps2_texture_end_frame(rdp);
        unsigned uploads=rdp->stats.textureUploads;
        ps2_texture_upload(&rdp->tile[0],rdp);
        if (!rdp->tile[0].gsTextureLoaded || rdp->tile[0].gsTexture.Width!=32 ||
            rdp->tile[0].gsTexture.Height!=64 || rdp->stats.textureUploads!=uploads+1) failures++;
        const u8* pixels=(const u8*)rdp->tile[0].gsTexture.Mem;
        for (int y=0;y<64;y++) for (int x=0;x<32;x++) {
            int sx=x, t=y*4-(int)scroll, iy=t>=0?t/4:(t-3)/4, fy=t&3;
            int effect=(sample(sx,iy)*(4-fy)+sample(sx,iy+1)*fy)/4;
            int m=mask[y*40+x];
            for (int c=0;c<4;c++) {
                int p=(prim>>((3-c)*8))&255, e=(env>>((3-c)*8))&255;
                int intermediate=clamp(m+(effect-(c==3?255:p))*(int)lod/255);
                int expected=c==3?intermediate*128/255:e+(p-e)*intermediate/255;
                if (pixels[(y*32+x)*4+c]!=expected) {
                    if (failures<3) printf("[PS2 TOKEN PIXEL] pass=%u xy=%d,%d channel=%d actual=%u expected=%d\n",
                        pass,x,y,c,pixels[(y*32+x)*4+c],expected);
                    failures++;
                }
                checks++;
            }
            if (!m) { if (pixels[(y*32+x)*4+3]) failures++; else transparent++; }
        }
        ps2_texture_apply_tile(&rdp->tile[0],rdp);
        if (rdp->gs->Clamp->WMS!=2 || rdp->gs->Clamp->WMT!=2 ||
            rdp->gs->Clamp->MAXU!=31 || rdp->gs->Clamp->MAXV!=63) failures++;
        RSPState rsp={.texScaleS=1,.texScaleT=1};
        RSPVertex vertex={.tc_s=32*32,.tc_t=64*32,.cw=1};
        GSPRIMSTQPOINT reference, prepared;
        PS2TextureVertexSetup setup;
        ps2_texture_prepare_vertices(&setup,&rdp->tile[0],&rsp,rdp);
        ps2_texture_build_vertex(&rdp->tile[0],&rsp,rdp,&vertex,0x80808080,0,&reference);
        ps2_texture_build_vertex_prepared(&setup,&vertex,0x80808080,0,&prepared);
        if (memcmp(&reference,&prepared,sizeof(reference)) || setup.offsetS || setup.offsetT ||
            setup.invWidth!=1.0f/32 || setup.invHeight!=1.0f/64) failures++;
        if (pass && memcmp(previous,pixels,sizeof(previous))) changes++;
        memcpy(previous,pixels,sizeof(previous));
        uploads=rdp->stats.textureUploads;
        ps2_texture_upload(&rdp->tile[0],rdp);
        if (rdp->stats.textureUploads!=uploads) failures++;
    }
    if (changes!=4 || !transparent) failures++;
    mask[63*40+31]^=0xff;
    unsigned uploads=rdp->stats.textureUploads;
    ps2_texture_upload(&rdp->tile[0],rdp);
    if (rdp->stats.textureUploads!=uploads+1) failures++;
    rdp_setcombine(0,0,rdp);
    if (ps2_combiner_token_flame(rdp)) failures++;
    *rdp=s_saved; rdp->gs_clamp_valid=false;
    printf("[PS2 TOKEN FLAME TEST] channels=%u transparent=%u dual I8, scroll, clamp, points, cache failures=%u\n",
        checks,transparent,failures);
    return failures?1:0;
}
