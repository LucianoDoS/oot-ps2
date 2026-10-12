/* Packed sun strips: verify independent sources, tint, alpha and cache. */
#include <stdio.h>
#include <string.h>
#include "ps2_texture.h"
static RDPState saved;
static u8 day[32*32], evening[32*32];
int ps2_sun_smoke(void) {
    RDPState* r=&g_rdpState;
    saved=*r;
    unsigned failures=0, channels=0;
    for (unsigned i=0;i<sizeof(day);i++) { day[i]=(i*37)&255; evening[i]=(i*13+91)&255; }
    rdp_setothermode(1u<<20,0,r);
    rdp_setcombine(0xfc262a60,0x150c937f,r);
    failures+=!ps2_combiner_sun(r);
    r->textureTile=0;
    r->tile[0]=(TileDescriptor){.addr=day,.width=64,.height=32,.fmt=G_IM_FMT_I,.siz=G_IM_SIZ_8b,
        .line=8,.tmem=0,.cms=2,.cmt=2,.masks=6,.maskt=5};
    r->tile[1]=r->tile[0]; r->tile[1].addr=evening; r->tile[1].tmem=256;
    r->textureLoads[0].source=day; r->textureLoads[0].stride=0;
    r->textureLoads[256].source=evening; r->textureLoads[256].stride=0;
    for (unsigned pass=0;pass<6;pass++) {
        unsigned factor=pass<3?pass*127:193;
        u32 prim=pass==4?0xffc08093:0xffeddaff, env=0xff803000|factor;
        rdp_setprimcolor(0,prim,r); rdp_setenvcolor(0,env,r);
        if (pass==3) evening[47]^=0xa3;
        if (pass==5) day[100]^=0x5c;
        ps2_texture_end_frame(r);
        ps2_texture_upload(&r->tile[0],r);
        const u8* pixels=(const u8*)r->tile[0].gsTexture.Mem;
        for (unsigned i=0;i<64*32;i++) {
            int a=((i&1)?day[i/2]&15:day[i/2]>>4)*17;
            int b=((i&1)?evening[i/2]&15:evening[i/2]>>4)*17;
            int mix=(int)factor*(b-a)/255+a;
            for (unsigned c=0;c<4;c++) {
                int p=(prim>>((3-c)*8))&255, e=(env>>((3-c)*8))&255;
                int expected=c==3?(mix*p/255)*128/255:e+(p-e)*mix/255;
                failures+=pixels[i*4+c]!=expected; channels++;
            }
        }
        unsigned uploads=r->stats.textureUploads;
        ps2_texture_upload(&r->tile[0],r);
        failures+=r->stats.textureUploads!=uploads;
    }
    *r=saved; r->gs_clamp_valid=false;
    printf("[PS2 SUN TEST] channels=%u packed strips, independent sources, tint, alpha, mutation and cache failures=%u\n",channels,failures);
    return failures?1:0;
}
