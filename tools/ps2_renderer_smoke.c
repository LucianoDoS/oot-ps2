/* Real EE/GS checks for bugs observed in the title-screen capture. */
#include <gsKit.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ps2_types.h"
#include "rdp_to_gs.h"
#include "ps2_texture.h"
#include "ps2_background.h"
#include "ps2_gfx_profile.h"
#include "ps2_segment.h"
const u64 gspS2DEX2d_fifoTextStart[1] = {0};
#include "f3dex2_ps2.h"
extern const void* ps2_prerender_fixture(int);
extern void ps2_prerender_mutate(void);
extern int ps2_prerender_failures(void);
extern int ps2_lighting_smoke(void);
extern int ps2_combiner_channel_smoke(void);
extern int ps2_glow_dependency_smoke(void);
extern int ps2_memcpy_smoke(void);
extern int ps2_ground_run_smoke(void);
extern int ps2_texture_vertex_smoke(void);
extern int ps2_clip_projection_smoke(void);
extern int ps2_texture_window_smoke(void);
extern int ps2_offscreen_smoke(void);
extern int ps2_frame_capture_smoke(void);
extern int ps2_flame_smoke(void);
extern int ps2_token_flame_smoke(void);
extern int ps2_jewel_smoke(void);
extern int ps2_triangle_packet_smoke(void);
extern int ps2_fps_smoke(void);
extern int ps2_texture_content_smoke(void);
extern int ps2_vertex_index_smoke(void);
extern int ps2_combiner_analysis_smoke(void);
extern int ps2_texture_cache_key_smoke(void);

static void matrix(short* out, float scale, float translate) {
    memset(out, 0, 64);
    for (int i = 0; i < 16; i++) {
        float value = (i == 0 || i == 5 || i == 10) ? scale : (i == 15 ? 1 : 0);
        if (i == 12) value = translate;
        int fixed = (int)(value * 65536.0f);
        out[i] = fixed >> 16;
        out[i + 16] = fixed & 0xffff;
    }
}

static __attribute__((noinline)) u32 command_read_bench(const u8* source, bool native) {
    u32 checksum=0;
    if (native) for (unsigned i=0;i<50000;i++) {
        const u8* command=source+(i&511)*8;
        u32 w0,w1; gfx_read_words(command,&w0,&w1); checksum+=w0^(w1+i);
    } else for (unsigned i=0;i<50000;i++) {
        const u8* command=source+(i&511)*8;
        u32 w0,w1; memcpy(&w0,command,4); memcpy(&w1,command+4,4); checksum+=w0^(w1+i);
    }
    return checksum;
}

/* Volatile byte loads keep the oracle independent of GCC's unaligned
 * word-load folding. The EE's 64-bit GPRs also require a canonical word
 * representation when comparing values with BEQ/BNE. */
static __attribute__((noinline)) u32 asset_read_reference(const volatile u8* p) {
    volatile u32 value=p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);
    return value;
}

#ifdef PS2_RENDERER_SMOKE_CAPTURE
/* Start with an empty texture cache. The wall/glyph corruption requires
 * descriptor pressure while VRAM still has a free, non-overlapping region. */
static int texture_descriptor_lifetimes(void) {
    static u8 glyphs[1025][128] __attribute__((aligned(128)));
    unsigned capacity=ps2_texture_test_cache_capacity();
    if (capacity>1024 || capacity<300) {
        printf("[PS2 DESCRIPTOR TEST] FAIL: unsupported capacity=%u\n",capacity);
        return 1;
    }
    RDPState* rdp=&g_rdpState;
    u32 oldHi=rdp->otherMode.hi,oldLo=rdp->otherMode.lo;
    u32 oldCombine0=rdp->combiner.w0,oldCombine1=rdp->combiner.w1;
    const u8* oldSource=rdp->textureLoads[511].source;
    u32 oldStride=rdp->textureLoads[511].stride;
    rdp_setothermode(2u<<20,0,rdp);
    rdp_setcombine(0,0,rdp);
    for (unsigned i=0;i<=capacity;i++) for (unsigned j=0;j<128;j++)
        glyphs[i][j]=(i*37+j*13+5)&255;
    TileDescriptor tile={.width=16,.height=16,.fmt=G_IM_FMT_I,
        .siz=G_IM_SIZ_4b,.tmem=511,.cms=2,.cmt=2};
    u32 firstVram=0;
    unsigned firstSlot=0;
    int failures=0;
    for (unsigned i=0;i<capacity;i++) {
        rdp->textureLoads[511].source=glyphs[i]; rdp->textureLoads[511].stride=0;
        tile.gsTextureLoaded=false;
        ps2_texture_upload(&tile,rdp);
        if (!tile.gsTextureLoaded) { failures++; break; }
        if (!i) { firstVram=tile.gsTexture.Vram; firstSlot=tile.cacheSlot; }
        ps2_texture_apply_tile(&tile,rdp);
        gsKit_prim_sprite_texture(rdp->gs,&tile.gsTexture,
            8,8,0,0,24,24,16,16,1,GS_SETREG_RGBA(128,128,128,128));
    }
    u32 waits=rdp->stats.textureWaits;
    rdp->textureLoads[511].source=glyphs[capacity];
    tile.gsTextureLoaded=false;
    ps2_texture_upload(&tile,rdp);
    /* This upload uses spare VRAM. Only discarding the old descriptor
     * requires the fence: checking overlap alone misses these queued draws. */
    u32 fenceWaits=rdp->stats.textureWaits-waits;
    if (!tile.gsTextureLoaded || tile.cacheSlot!=firstSlot ||
        tile.gsTexture.Vram==firstVram || fenceWaits!=1) failures++;
    printf("[PS2 DESCRIPTOR FENCE TEST] capacity=%u oldVram=%08x newVram=%08x waits=%u failures=%d\n",
        capacity,firstVram,tile.gsTexture.Vram,fenceWaits,failures);
    u32 passUploads[2];
    for (unsigned pass=0;pass<2;pass++) {
        ps2_texture_end_frame(rdp);
        u32 uploads=rdp->stats.textureUploads;
        for (unsigned i=0;i<300;i++) {
            rdp->textureLoads[511].source=glyphs[i];
            tile.gsTextureLoaded=false;
            ps2_texture_upload(&tile,rdp);
            if (!tile.gsTextureLoaded) failures++;
            ps2_texture_apply_tile(&tile,rdp);
            gsKit_prim_sprite_texture(rdp->gs,&tile.gsTexture,
                8,8,0,0,24,24,16,16,1,GS_SETREG_RGBA(128,128,128,128));
        }
        passUploads[pass]=rdp->stats.textureUploads-uploads;
    }
    if (passUploads[1]) failures++;
    printf("[PS2 DIALOGUE CACHE TEST] 300 glyphs uploads=%u,%u failures=%d\n",
        passUploads[0],passUploads[1],failures);
    rdp->textureLoads[511].source=oldSource; rdp->textureLoads[511].stride=oldStride;
    rdp_setcombine(oldCombine0,oldCombine1,rdp);
    rdp_setothermode(oldHi,oldLo,rdp);
    return failures;
}
#endif

int ps2_renderer_smoke(void) {
    int failures = ps2_lighting_smoke();
    failures += ps2_memcpy_smoke();
    failures += ps2_triangle_packet_smoke();
    failures += ps2_fps_smoke();
    failures += ps2_texture_content_smoke();
    failures += ps2_combiner_channel_smoke();
    failures += ps2_combiner_analysis_smoke();
    failures += ps2_texture_cache_key_smoke();
    failures += ps2_texture_palette_hash_smoke();
    static u8 readInputs[4112] __attribute__((aligned(128)));
    for (unsigned i=0;i<sizeof(readInputs);i++) readInputs[i]=(i*73+159)&255;
    for (unsigned offset=0;offset<16;offset++) for (unsigned i=0;i<256;i++) {
        const u8* input=readInputs+offset+i;
        u16 expected16=input[0]|((u16)input[1]<<8);
        u32 expected0=asset_read_reference(input);
        u32 expected1=asset_read_reference(input+4);
        u32 w0,w1; gfx_read_words(input,&w0,&w1);
        if (ASSET_U16(input)!=expected16 || ASSET_S16(input)!=(s16)expected16 ||
            ASSET_U32(input)!=expected0 || GFX_READ_W0(input)!=expected0 || GFX_READ_W1(input)!=expected1 ||
            w0!=expected0 || w1!=expected1) {
            if (failures<4) printf("[PS2 ASSET READ DIFFERENCE] offset=%u i=%u half=%04x/%04x signed=%d/%d word=%08x/%08x pair=%08x,%08x/%08x,%08x single=%08x,%08x\n",
                offset,i,(unsigned)ASSET_U16(input),(unsigned)expected16,(int)ASSET_S16(input),(int)(s16)expected16,
                ASSET_U32(input),expected0,w0,w1,expected0,expected1,GFX_READ_W0(input),GFX_READ_W1(input));
            failures++;
        }
    }
    printf("[PS2 ASSET READ TEST] native signed/unsigned fields, command pairs and 16 alignments failures=%d\n",failures);
    for (unsigned offset=0;offset<2;offset++) {
        u32 start=ps2_gfx_cycles();
        u32 oldChecksum=command_read_bench(readInputs+offset,false);
        u32 oldCycles=ps2_gfx_cycles()-start;
        start=ps2_gfx_cycles();
        u32 nativeChecksum=command_read_bench(readInputs+offset,true);
        u32 nativeCycles=ps2_gfx_cycles()-start;
        if (oldChecksum != nativeChecksum) failures++;
        printf("[PS2 COMMAND READ BENCH] offset=%u oldCycles=%u nativeCycles=%u checksum=%08x failures=%d\n",
            offset,oldCycles,nativeCycles,nativeChecksum,failures);
    }
    /* XXH32 vectors from the independent system libxxhash reference. Verify
     * short tails, stripes, seeds and every possible word alignment on EE. */
    const unsigned lengths[] = {0,1,3,4,7,15,16,17,31,32,33,65,257};
    const u32 reference[][2] = {
        {0x02cc5d05,0xbd209070},{0x3fe5a29d,0xa0e7a8b7},{0x4021af28,0x763863f5},
        {0x1eb8f11a,0xe8eb117d},{0x02226fe7,0x4f89101b},{0x162986eb,0x28e7b73d},
        {0x96ac9fa6,0x4d269dbf},{0x3cdfb1a2,0xa321e227},{0xdf853fbf,0xd71ee24f},
        {0xda8072dc,0x75048b5b},{0x3c8fd07d,0x845fe473},{0x3922f755,0xc8c31042},
        {0x0c5d12f1,0x896ebb1d}
    };
    static u8 hashInput[273] __attribute__((aligned(16)));
    for (int offset=0;offset<16;offset++) {
        for (int i=0;i<257;i++) hashInput[offset+i]=(i*37+11)&255;
        for (unsigned i=0;i<sizeof(lengths)/sizeof(lengths[0]);i++) {
            if (ps2_texture_hash(hashInput+offset,lengths[i],0)!=reference[i][0] ||
                ps2_texture_hash(hashInput+offset,lengths[i],0x12345678)!=reference[i][1]) failures++;
        }
        for (int i=0;i<257;i++) {
            hashInput[offset+i]^=1;
            if (ps2_texture_hash(hashInput+offset,257,0)==reference[12][0]) failures++;
            hashInput[offset+i]^=1;
        }
    }
    printf("[PS2 TEXTURE HASH TEST] reference vectors, unaligned rows, all-byte mutations (%d)\n",failures);
    static u8 alignedHash[4096] __attribute__((aligned(128)));
    static u8 eightHash[4104] __attribute__((aligned(128)));
    static u8 wordHash[4100] __attribute__((aligned(128)));
    static u8 unalignedHash[4097] __attribute__((aligned(128)));
    for (unsigned i=0;i<4096;i++)
        alignedHash[i]=eightHash[i+8]=wordHash[i+4]=unalignedHash[i+1]=(i*37+11)&255;
    for (unsigned bytes=2048;bytes<=4096;bytes*=2) {
        volatile u32 alignedChecksum=0, eightChecksum=0, wordChecksum=0, unalignedChecksum=0;
        u32 start=ps2_gfx_cycles();
        for (unsigned i=0;i<1000;i++) alignedChecksum+=ps2_texture_hash(alignedHash,bytes,i);
        u32 alignedCycles=ps2_gfx_cycles()-start;
        start=ps2_gfx_cycles();
        for (unsigned i=0;i<1000;i++) eightChecksum+=ps2_texture_hash(eightHash+8,bytes,i);
        u32 eightCycles=ps2_gfx_cycles()-start;
        start=ps2_gfx_cycles();
        for (unsigned i=0;i<1000;i++) wordChecksum+=ps2_texture_hash(wordHash+4,bytes,i);
        u32 wordCycles=ps2_gfx_cycles()-start;
        start=ps2_gfx_cycles();
        for (unsigned i=0;i<1000;i++) unalignedChecksum+=ps2_texture_hash(unalignedHash+1,bytes,i);
        u32 unalignedCycles=ps2_gfx_cycles()-start;
        if (alignedChecksum != eightChecksum || alignedChecksum != wordChecksum ||
            alignedChecksum != unalignedChecksum) failures++;
        printf("[PS2 HASH ALIGN BENCH] bytes=%u alignedCycles=%u eightCycles=%u wordCycles=%u unalignedCycles=%u checksum=%08x failures=%d\n",
            bytes,alignedCycles,eightCycles,wordCycles,unalignedCycles,(u32)alignedChecksum,failures);
    }
    int initializedDepth = g_rdpState.gs->PSMZ;
    const int formats[] = {GS_PSMZ_16, GS_PSMZ_16S, GS_PSMZ_24, GS_PSMZ_32};
    for (unsigned format = 0; format < sizeof(formats)/sizeof(formats[0]); format++) {
        g_rdpState.gs->PSMZ = formats[format];
        u32 maximum = format < 2 ? 0xffff : 0xffffff;
        u32 previous = maximum;
        for (unsigned step = 0; step <= 65536; step++) {
            u32 depth = ps2_zbuffer_value(&g_rdpState, step / 65536.0f);
            if (depth > maximum || depth > previous) failures++;
            previous = depth;
        }
        if (ps2_zbuffer_value(&g_rdpState, -1) != maximum ||
            ps2_zbuffer_value(&g_rdpState, 2) != 0 || previous != 0) failures++;
    }
    g_rdpState.gs->PSMZ = initializedDepth;
    printf("[PS2 DEPTH TEST] initialized=%02x monotonic failures=%d\n", initializedDepth, failures);
    RSPVertex decalA = {.sx=0,.sy=0}, decalB = {.sx=10,.sy=0}, decalC = {.sx=0,.sy=10};
    u32 decalDepth[3] = {10000,10100,10200};
    g_rdpState.otherMode.lo = 0xc10;
    ps2_zbuffer_decal_bias(&g_rdpState, &decalA, &decalB, &decalC, decalDepth);
    if (decalDepth[0] != 10062 || decalDepth[1] != 10162 || decalDepth[2] != 10262) failures++;
    /* A separate foreground surface must still hide the decal; ordinary
     * geometry and depth-writing materials receive no polygon offset. */
    if (decalDepth[0] >= 10500) failures++;
    g_rdpState.otherMode.lo = 0xc30;
    ps2_zbuffer_decal_bias(&g_rdpState, &decalA, &decalB, &decalC, decalDepth);
    if (decalDepth[0] != 10062) failures++;
    g_rdpState.otherMode.lo = 0x10;
    ps2_zbuffer_decal_bias(&g_rdpState, &decalA, &decalB, &decalC, decalDepth);
    if (decalDepth[0] != 10062) failures++;
    g_rdpState.otherMode.lo = 0xc10;
    decalDepth[0] = decalDepth[1] = decalDepth[2] = 0xfffffe;
    ps2_zbuffer_decal_bias(&g_rdpState, &decalA, &decalB, &decalC, decalDepth);
    if (decalDepth[0] != 0xffffff) failures++;
    g_rdpState.gs->PSMZ = GS_PSMZ_16;
    decalDepth[0] = decalDepth[1] = decalDepth[2] = 0xfffe;
    ps2_zbuffer_decal_bias(&g_rdpState, &decalA, &decalB, &decalC, decalDepth);
    if (decalDepth[0] != 0xffff) failures++;
    g_rdpState.gs->PSMZ = initializedDepth;
    g_rdpState.otherMode.lo = 0;
    printf("[PS2 DECAL TEST] slope bias, occlusion and depth limits failures=%d\n", failures);
    const u8 texel[4] = {128, 64, 32, 200}, shade[4] = {80, 100, 120, 140};
    u8 combined[4];
    rdp_setcombine(0xfcffffff, 0xfffdf6fb, &g_rdpState); /* PRIMITIVE */
    rdp_setprimcolor(0, 0x10203000, &g_rdpState);
    ps2_combiner_color(&g_rdpState, texel, shade, combined);
    if (combined[0] != 16 || combined[1] != 32 || combined[2] != 48 || combined[3] != 0) failures++;
    rdp_setprimcolor(0, 0x10203080, &g_rdpState);
    ps2_combiner_color(&g_rdpState, texel, shade, combined);
    if (combined[3] != 128) failures++;
    rdp_setcombine(0xfcffffff, 0xfffe793c, &g_rdpState); /* SHADE */
    ps2_combiner_color(&g_rdpState, texel, shade, combined);
    if (memcmp(combined, shade, 4)) failures++;
    rdp_setothermode(2u << 20, 0, &g_rdpState); /* COPY bypass */
    ps2_combiner_color(&g_rdpState, texel, shade, combined);
    if (memcmp(combined, texel, 4)) failures++;
    /* Differential LUT checks use the original scalar equations, including
     * both cycles, combined alpha, copy, dynamic alpha and varied constants. */
    static u8 lookup[4][256];
    RDPState material = {0};
    const u32 materials[][2] = {
        {0,0},{0xfcffffff,0xfffdf6fb},{0xfcffffff,0xfffe793c},
        {0xfc267e04,0x1ffcfdf8},{0xfc262a04,0x1f5893f8},
        {0xfc119623,0xff2fffff},{0xfc127e24,0xfffff9fc}
    };
    u32 random = 0x79214;
    for (int fixture = 0; fixture < 31; fixture++) {
        random = random*1664525u+1013904223u;
        material.combiner.w0 = fixture < 7 ? materials[fixture][0] : 0xfc000000u | (random & 0xffffff);
        random = random*1664525u+1013904223u;
        material.combiner.w1 = fixture < 7 ? materials[fixture][1] : random;
        material.primColor = random ^ 0xc39820a4u;
        material.envColor = random ^ 0x142589d3u;
        for (int cycle = 0; cycle < 3; cycle++) {
            material.otherMode.hi = cycle << 20;
            ps2_combiner_build_lookup(&material, lookup);
            for (int value = 0; value < 256; value++) {
                u8 input[4] = {value,value,value,255}, full[4] = {255,255,255,255}, expected[4];
                ps2_combiner_color(&material, input, full, expected);
                for (int channel = 0; channel < 3; channel++) if (lookup[channel][value] != expected[channel]) failures++;
                input[0] = input[1] = input[2] = 255; input[3] = value;
                ps2_combiner_color(&material, input, full, expected);
                unsigned alpha = ps2_combiner_dynamic_alpha(&material) ? value*128u/255 : expected[3]*128u/255;
                if (lookup[3][value] != alpha) failures++;
            }
        }
    }
    printf("[PS2 COMBINER LUT TEST] original equations vs compiled references, all intensities (%d)\n", failures);
    /* White texels are the production untextured path. Vary each shade
     * channel independently, plus mixed RGBA, to cover combined-alpha RGB
     * dependencies, both clamps and material changes without setter calls. */
    for (int fixture=0;fixture<64;fixture++) {
        random=random*1664525u+1013904223u;
        material.combiner.w0=fixture<7 ? materials[fixture][0] : 0xfc000000u|(random&0xffffff);
        random=random*1664525u+1013904223u;
        material.combiner.w1=fixture<7 ? materials[fixture][1] : random;
        material.primColor=random^0x319c70e5u; material.envColor=random^0x7a520d93u;
        for (unsigned cycle=0;cycle<4;cycle++) {
            material.otherMode.hi=cycle<<20;
            for (unsigned pattern=0;pattern<5;pattern++) for (unsigned value=0;value<256;value++) {
                u8 input[4]={19,83,147,211}, white[4]={255,255,255,255}, expected[4], actual[4];
                if (pattern<4) input[pattern]=value;
                else for (unsigned c=0;c<4;c++) input[c]=(value*(2*c+1)+c*29)&255;
                ps2_combiner_color(&material,white,input,expected);
                ps2_combiner_vertex_color(&material,input,actual);
                if (memcmp(expected,actual,4)) failures++;
            }
        }
    }
    printf("[PS2 VERTEX COLOR TEST] 64 materials, four cycles, independent/mixed shade RGBA failures=%d\n",failures);
    for (unsigned fixture=0;fixture<3;fixture++) {
        material.combiner.w0=materials[fixture+1][0]; material.combiner.w1=materials[fixture+1][1];
        material.otherMode.hi=fixture == 2 ? 1u<<20 : 0;
        u8 input[4]={19,83,147,211}, white[4]={255,255,255,255}, actual[4];
        volatile unsigned checksum=0;
        ps2_combiner_vertex_color(&material,input,actual);
        u32 start=ps2_gfx_cycles();
        for (unsigned i=0;i<10000;i++) {
            input[fixture]=i;
            ps2_combiner_color(&material,white,input,actual); checksum+=actual[fixture];
        }
        u32 scalarCycles=ps2_gfx_cycles()-start;
        start=ps2_gfx_cycles();
        for (unsigned i=0;i<10000;i++) {
            input[fixture]=i;
            ps2_combiner_vertex_color(&material,input,actual); checksum+=actual[fixture];
        }
        u32 compiledCycles=ps2_gfx_cycles()-start;
        printf("[PS2 VERTEX COLOR BENCH] material=%u scalarCycles=%u compiledCycles=%u checksum=%u\n",
            fixture,scalarCycles,compiledCycles,checksum);
    }
    rdp_setcombine(0, 0, &g_rdpState);
    rdp_setothermode(0, 0, &g_rdpState);
    static short projection[32] __attribute__((aligned(16)));
    static short view[32] __attribute__((aligned(16)));
    static short model[32] __attribute__((aligned(16)));
    static short viewport[8] = {640, 480, 0, 0, 640, 480, 0, 0};
    static short vertex[8] = {1, 1, 0, 0, 0, 0, -1, -1};
    matrix(projection, 0.125f, 0);
    matrix(view, 1, 2);
    matrix(model, 1, 3);
    f3dex2_mtx(0xDA380007, (u32)projection, &g_rspState);
    f3dex2_mtx(0xDA380005, (u32)view, &g_rspState);
    f3dex2_mtx(0xDA380003, (u32)model, &g_rspState);
    f3dex2_movemem(0xDC080008, (u32)viewport, &g_rspState);
    f3dex2_vtx(0x01001002, (u32)vertex, &g_rspState);
    RSPVertex* v = &g_rspState.vtx[0];
    printf("[PS2 RENDER CHECK] clip=%f,%f screen=%f,%f\n", v->cx, v->cy, v->sx, v->sy);
    if (fabsf(v->cx - 0.75f) > 0.001f || fabsf(v->cy - 0.125f) > 0.001f ||
        fabsf(v->sx - 560.0f) > 0.01f || fabsf(v->sy - 196.0f) > 0.01f) failures++;
    /* Bad asset pointers must discard the old vertex rather than reading
     * outside EE RAM or drawing geometry from a previous material. */
    f3dex2_vtx(0x01001002, 0xe00c0700, &g_rspState);
    if (v->valid) failures++;
    f3dex2_vtx(0x01001002, (u32)vertex, &g_rspState);
    if (!v->valid || fabsf(v->cx - 0.75f) > 0.001f) failures++;
    RSPVertex a = {.cx=-0.5f,.cy=-0.5f,.cw=1,.ca=255};
    RSPVertex b = {.cx=0.5f,.cy=-0.5f,.cw=1,.ca=255};
    RSPVertex c = {.cx=0,.cy=0.5f,.cz=-2,.cw=1,.ca=255};
    RSPVertex clipped[12];
    int count = ps2_clip_triangle(&a, &b, &c, clipped);
    if (count != 4) failures++;
    for (int i = 0; i < count; i++)
        if (clipped[i].cw <= 0 || fabsf(clipped[i].cz) > clipped[i].cw + 0.0001f ||
            clipped[i].ca != 255) failures++;
    c.cz = -3; a.cz = b.cz = -2;
    if (ps2_clip_triangle(&a, &b, &c, clipped) != 0) failures++;
    /* Check the emitted queue, not just software TEST/ALPHA fields. Copies
     * of RDPState must share the actual GS shadow; contexts and frame reset
     * must still issue state. ZBUF keys include VRAM base and depth format. */
    GSGLOBAL* stateGs = g_rdpState.gs;
    int stateContext = stateGs->PrimContext;
    unsigned stateZBuffer = stateGs->ZBuffer, stateZFormat = stateGs->PSMZ;
    ps2_gs_state_reset();
    u32 writesStart = g_rdpState.stats.stateWrites;
    rdp_setothermode(1u<<20,0xc8113078u,&g_rdpState);
    if (g_rdpState.stats.stateWrites != writesStart+3) failures++;
    void* stateQueue = stateGs->CurQueue->pool_cur;
    for (unsigned i=0;i<100;i++) {
        rdp_setothermode(1u<<20,0xc8113078u,&g_rdpState);
        rdp_setothermode_h(0xE3001201,2u<<12,&g_rdpState); /* filter only */
        rdp_setothermode_l(0xE200001C,0xc8113078u,&g_rdpState);
    }
    if (stateGs->CurQueue->pool_cur != stateQueue ||
        g_rdpState.stats.stateWrites != writesStart+3) failures++;
    stateGs->PrimContext ^= 1;
    rdp_apply_othermode(&g_rdpState);
    if (g_rdpState.stats.stateWrites != writesStart+6) failures++;
    stateGs->PrimContext = stateContext;
    stateQueue = stateGs->CurQueue->pool_cur;
    rdp_apply_othermode(&g_rdpState);
    if (stateGs->CurQueue->pool_cur != stateQueue) failures++;
    stateGs->ZBuffer += 8192;
    ps2_zbuffer_set_writes(&g_rdpState,true);
    if (g_rdpState.stats.stateWrites != writesStart+7) failures++;
    stateGs->PSMZ = GS_PSMZ_16;
    ps2_zbuffer_set_writes(&g_rdpState,true);
    if (g_rdpState.stats.stateWrites != writesStart+8) failures++;
    stateGs->ZBuffer = stateZBuffer; stateGs->PSMZ = stateZFormat;
    ps2_zbuffer_set_writes(&g_rdpState,true);
    u64 stateAlpha = g_rdpState.gs_alpha;
    ps2_gs_set_alpha(&g_rdpState,GS_SETREG_ALPHA(0,1,0,1,0x80));
    RDPState stateCopy = g_rdpState;
    ps2_gs_set_alpha(&stateCopy,stateAlpha);
    stateQueue = stateGs->CurQueue->pool_cur;
    ps2_gs_set_alpha(&g_rdpState,stateAlpha);
    if (stateGs->CurQueue->pool_cur != stateQueue || stateGs->PrimAlpha != stateAlpha) failures++;
    /* A depth clear changes all three registers and restores their shadow. */
    void* stateColorImage = g_rdpState.colorImage;
    void* stateZImage = g_rdpState.zImage;
    g_rdpState.colorImage = g_rdpState.zImage = (void*)0x200000;
    rdp_fillrect((16u<<12)|16u,0,&g_rdpState);
    g_rdpState.colorImage = stateColorImage; g_rdpState.zImage = stateZImage;
    stateQueue = stateGs->CurQueue->pool_cur;
    rdp_apply_othermode(&g_rdpState);
    if (stateGs->CurQueue->pool_cur != stateQueue || stateGs->Test->AREF != 64 ||
        stateGs->Test->ZTST != 2 || !g_rdpState.zWriteEnabled) failures++;
    writesStart = g_rdpState.stats.stateWrites;
    ps2_gs_state_reset();
    rdp_apply_othermode(&g_rdpState);
    if (g_rdpState.stats.stateWrites != writesStart+3) failures++;
    printf("[PS2 GS STATE TEST] queue reuse, contexts, ZBUF relocation/format, copied state and depth clear failures=%d\n",failures);
    rdp_setothermode_h(0xE3000A01, 3u << 20, &g_rdpState);
    if ((g_rdpState.otherMode.hi & (3u << 20)) != (3u << 20)) failures++;
    rdp_setothermode_l(0xE200001C, 0x10, &g_rdpState);
    printf("[PS2 RENDER CHECK] mode=%08x,%08x z=%u\n", g_rdpState.otherMode.hi,
           g_rdpState.otherMode.lo, g_rdpState.gs->Test->ZTST);
    if (g_rdpState.otherMode.lo != 0x10 || g_rdpState.gs->Test->ZTST != 2) failures++;
    /* Real GBL_c1/c2 encodings: opaque coverage must preserve source RGB,
     * and a two-cycle fog+translucent mode must use the final blender. */
    rdp_setothermode(0, (1u << 22) | (1u << 18) | 0x30, &g_rdpState);
    printf("[PS2 RENDER CHECK] opaque alpha=%016llx expected=%016llx\n",
        (unsigned long long)g_rdpState.gs_alpha,
        (unsigned long long)GS_SETREG_ALPHA(0, 2, 2, 2, 0x80));
    if (g_rdpState.gs_alpha != (GS_SETREG_ALPHA(0, 2, 2, 2, 0x80))) failures++;
    if (!g_rdpState.zWriteEnabled) failures++;
    rdp_setothermode(1u << 20, (3u << 30) | (2u << 26) | (1u << 20) | 0x4000,
                     &g_rdpState);
    printf("[PS2 RENDER CHECK] two-cycle alpha=%016llx expected=%016llx\n",
        (unsigned long long)g_rdpState.gs_alpha,
        (unsigned long long)GS_SETREG_ALPHA(0, 1, 0, 1, 0x80));
    if (g_rdpState.gs_alpha != (GS_SETREG_ALPHA(0, 1, 0, 1, 0x80))) failures++;
    if (g_rdpState.zWriteEnabled) failures++;
    /* Cutout ladders/fences use alpha coverage, not G_AC_THRESHOLD. */
    rdp_setothermode(1u << 20, 0xc8113078u, &g_rdpState);
    if (!g_rdpState.gs->Test->ATE || g_rdpState.gs->Test->AREF != 64 ||
        g_rdpState.gs->Test->ATST != 6 || g_rdpState.gs->Test->AFAIL != 0 ||
        g_rdpState.gs->Test->ZTST != 2 || !g_rdpState.zWriteEnabled) failures++;
    rdp_setothermode(1u << 20, 0xc8112078u, &g_rdpState);
    if (g_rdpState.gs->Test->ATE) failures++;
    rdp_setothermode(0, 1, &g_rdpState);
    rdp_setblendcolor(0, 0x00000080, &g_rdpState);
    if (!g_rdpState.gs->Test->ATE || g_rdpState.gs->Test->AREF != 65) failures++;
    rdp_setblendcolor(0, 0, &g_rdpState);
    if (g_rdpState.gs->Test->AREF != 1) failures++;
    rdp_setothermode(0, 0, &g_rdpState);
#ifdef PS2_RENDERER_SMOKE_CAPTURE
    failures+=texture_descriptor_lifetimes();
    failures+=ps2_glow_dependency_smoke();
    failures+=ps2_clip_projection_smoke();
#endif
    static const u8 mirroredTexels[] = {
        10,0,0,0, 20,0,0,255, 30,0,0,255, 40,0,0,255,
        50,0,0,255, 60,0,0,255, 70,0,0,255, 80,0,0,255};
    TileDescriptor mirrored = {.addr=(void*)mirroredTexels,.width=4,.height=2,
        .fmt=G_IM_FMT_RGBA,.siz=G_IM_SIZ_32b,.masks=2,.maskt=1,.tmem=511};
    u32 mirrorVram[4];
    rdp_setothermode(2u << 20, 0, &g_rdpState);
    for (int pass=0; pass<2; pass++) for (int mode=0; mode<4; mode++) {
        mirrored.cms = mode & 1; mirrored.cmt = (mode >> 1) & 1;
        mirrored.gsTextureLoaded = false;
        ps2_texture_upload(&mirrored, &g_rdpState);
        int mw = mode & 1 ? 8 : 4, mh = mode & 2 ? 4 : 2;
        if (!mirrored.gsTextureLoaded || mirrored.gsTexture.Width != mw || mirrored.gsTexture.Height != mh) failures++;
        if (!pass) {
            mirrorVram[mode] = mirrored.gsTexture.Vram;
            for (int y=0;y<mh;y++) for (int x=0;x<mw;x++) {
                int sx = x >= 4 ? 7-x : x, sy = y >= 2 ? 3-y : y;
                const u8* pixel = (u8*)mirrored.gsTexture.Mem + (y*mw+x)*4;
                if (pixel[0] != mirroredTexels[(sy*4+sx)*4] ||
                    pixel[3] != (sx==0 && sy==0 ? 0 : 128)) failures++;
            }
            for (int i=0;i<mode;i++) if (mirrorVram[i]==mirrorVram[mode]) failures++;
        } else if (mirrored.gsTexture.Vram != mirrorVram[mode]) failures++;
        ps2_texture_apply_tile(&mirrored, &g_rdpState);
        if (g_rdpState.gs->Clamp->MINU != mw-1 || g_rdpState.gs->Clamp->MINV != mh-1) failures++;
    }
    RSPVertex mirrorVertex = {.tc_s=256,.tc_t=-128,.cw=1};
    RSPState mirrorRsp = {.texScaleS=1,.texScaleT=1};
    GSPRIMSTQPOINT mirrorPoint;
    ps2_texture_build_vertex(&mirrored,&mirrorRsp,&g_rdpState,&mirrorVertex,0,0,&mirrorPoint);
    if (mirrorPoint.stq.st.s != 1 || mirrorPoint.stq.st.t != -1) failures++;
    printf("[PS2 MIRROR TEST] S/T texels, alpha, cache variants and coordinates failures=%d\n", failures);
    failures += ps2_texture_window_smoke();
    rdp_setothermode(0, 0, &g_rdpState);
    static const u8 texture[] = {255,255, 255,255, 255,255, 255,255};
    for (int i = 0; i < 2; i++) {
        TileDescriptor* tile = &g_rdpState.tile[i];
        tile->addr = (void*)texture;
        tile->width = tile->height = 2;
        tile->fmt = G_IM_FMT_RGBA;
        tile->siz = G_IM_SIZ_16b;
        ps2_texture_upload(tile, &g_rdpState);
        printf("[PS2 RENDER CHECK] tile=%d loaded=%d vram=%08x floor=%08x\n",
               i, tile->gsTextureLoaded, tile->gsTexture.Vram, g_rdpState.gs->TexturePointer);
        if (!tile->gsTextureLoaded || tile->gsTexture.Vram < g_rdpState.gs->TexturePointer ||
            tile->gsTexture.Vram == 0) failures++;
    }
    if (!g_rdpState.tile[0].gsTextureLoaded ||
        g_rdpState.tile[0].gsTexture.Vram != g_rdpState.tile[1].gsTexture.Vram ||
        g_rdpState.tile[1].gsTexture.TBW != 1) failures++;
    rdp_setprimcolor(g_rdpState.primLodFrac, g_rdpState.primColor, &g_rdpState);
    rdp_setenvcolor(0, g_rdpState.envColor, &g_rdpState);
    rdp_setcombine(g_rdpState.combiner.w0, g_rdpState.combiner.w1, &g_rdpState);
    if (!g_rdpState.tile[0].gsTextureLoaded) failures++;
    rdp_setprimcolor(g_rdpState.primLodFrac, g_rdpState.primColor ^ 0x100, &g_rdpState);
    if (g_rdpState.tile[0].gsTextureLoaded) failures++;
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    ps2_texture_end_frame(&g_rdpState);
    if (g_rdpState.tile[0].gsTextureLoaded) failures++;
    static const u8 rows[] = {0,0,0,255, 1,0,0,255, 2,0,0,255, 3,0,0,255,
                             4,0,0,255, 5,0,0,255, 6,0,0,255, 7,0,0,255};
    static const u8 nextRows[] = {20,0,0,255, 21,0,0,255, 22,0,0,255, 23,0,0,255,
                                 24,0,0,255, 25,0,0,255, 26,0,0,255, 27,0,0,255};
    rdp_settile(0xF5180000, 0, &g_rdpState);
    rdp_settile(0xF5180000, 7u << 24, &g_rdpState);
    rdp_settilesize(0xF2000000, (4u << 12) | 4, &g_rdpState);
    rdp_settimg(0xFD180003, (u32)rows, &g_rdpState);
    rdp_loadtexture(0xF4, 0xF4004000, 7u << 24, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    u8* pixels = (u8*)g_rdpState.tile[0].gsTexture.Mem;
    if (pixels[0] != 1 || pixels[8] != 5) failures++;
    rdp_settimg(0xFD180003, (u32)nextRows, &g_rdpState);
    rdp_loadtexture(0xF4, 0xF4004000, 7u << 24, &g_rdpState);
    if (g_rdpState.tile[0].gsTextureLoaded) failures++;
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    if (pixels[0] != 21 || pixels[8] != 25) failures++;
    rdp_settile(0xF5180100, 7u << 24, &g_rdpState);
    rdp_settimg(0xFD180003, (u32)rows, &g_rdpState);
    rdp_loadtexture(0xF3, 0, 7u << 24, &g_rdpState);
    if (g_rdpState.textureLoads[0].source != nextRows + 4 ||
        g_rdpState.textureLoads[256].source != rows) failures++;
    /* IA4 endpoints and nonzero CI4 palette placement. Use COPY so that
     * color-combiner state cannot hide incorrect decoder output. */
    rdp_setothermode(2u << 20, 0, &g_rdpState);
    static const u8 ia4[] = {0xf0};
    rdp_settile(0xf5600000, 0, &g_rdpState);
    rdp_settilesize(0xf2000000, 4u << 12, &g_rdpState);
    rdp_settimg(0xfd600001, (u32)ia4, &g_rdpState);
    rdp_loadtexture(0xf3, 0, 0, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    pixels = (u8*)g_rdpState.tile[0].gsTexture.Mem;
    if (pixels[0] != 255 || pixels[3] != 128 || pixels[4] != 0 || pixels[7] != 0) failures++;
    static const u8 palette0[] = {0xf8,0x01};
    static const u8 palette3[] = {0x07,0xc1};
    static const u8 ci4[] = {0};
    ps2_texture_load_tlut(palette0, 1);
    rdp_settile(0xf5100130, 7u << 24, &g_rdpState); /* TLUT entry 48. */
    rdp_settimg(0xfd100000, (u32)palette3, &g_rdpState);
    rdp_loadtlut(0xf0000000, 7u << 24, &g_rdpState);
    rdp_settile(0xf5400000, 3u << 20, &g_rdpState);
    rdp_settilesize(0xf2000000, 4u << 12, &g_rdpState);
    rdp_settimg(0xfd400001, (u32)ci4, &g_rdpState);
    rdp_loadtexture(0xf3, 0, 0, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    pixels = (u8*)g_rdpState.tile[0].gsTexture.Mem;
    if (pixels[0] != 0 || pixels[1] != 255 || pixels[2] != 0 || pixels[3] != 128) failures++;
    rdp_settile(0xf5400000, 0, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    if (pixels[0] != 255 || pixels[1] != 0) failures++;
    static u8 animated[] = {10,20,30,255, 10,20,30,255};
    rdp_settile(0xf5180000, 0, &g_rdpState);
    rdp_settimg(0xfd180001, (u32)animated, &g_rdpState);
    rdp_loadtexture(0xf3, 0, 0, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    ps2_texture_end_frame(&g_rdpState);
    animated[0] = 77;
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    pixels = (u8*)g_rdpState.tile[0].gsTexture.Mem;
    if (pixels[0] != 77 || pixels[1] != 20) failures++;
    u32 unchangedVram = g_rdpState.tile[0].gsTexture.Vram;
    rdp_setprimcolor(0, 0x12345678, &g_rdpState);
    rdp_setenvcolor(0, 0x87654321, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    if (g_rdpState.tile[0].gsTexture.Vram != unchangedVram) failures++;
    /* A fence for A also retires B's old accesses. Reusing B must not
     * wait a second time, but a later cache hit makes A live again. */
    static u8 fencePixels[2][8] = {{1,2,3,255,1,2,3,255},{4,5,6,255,4,5,6,255}};
    TileDescriptor fenceTiles[2] = {{0}};
    rdp_setothermode(2u << 20, 0, &g_rdpState);
    g_rdpState.textureLoads[0].source = NULL;
    for (int i = 0; i < 2; i++) {
        fenceTiles[i].width = 2; fenceTiles[i].height = 1;
        fenceTiles[i].fmt = G_IM_FMT_RGBA; fenceTiles[i].siz = G_IM_SIZ_32b;
        fenceTiles[i].addr = fencePixels[i];
        ps2_texture_upload(&fenceTiles[i], &g_rdpState);
        ps2_texture_apply_tile(&fenceTiles[i], &g_rdpState);
        gsKit_prim_sprite_texture(g_rdpState.gs, &fenceTiles[i].gsTexture,
            0,0,0,0,2,1,2,1,0,GS_SETREG_RGBAQ(128,128,128,128,1));
    }
    ps2_texture_end_frame(&g_rdpState);
    unsigned fenceWaits = g_rdpState.stats.textureWaits;
    fencePixels[0][0]++;
    ps2_texture_upload(&fenceTiles[0], &g_rdpState);
    if (g_rdpState.stats.textureWaits != fenceWaits + 1) failures++;
    fencePixels[1][0]++;
    ps2_texture_upload(&fenceTiles[1], &g_rdpState);
    if (g_rdpState.stats.textureWaits != fenceWaits + 1) failures++;
    PS2TextureTileSetup fenceSetup;
    ps2_texture_prepare_tile(&fenceSetup,&fenceTiles[0],&g_rdpState);
    ps2_texture_apply_tile_prepared(&fenceTiles[0],&g_rdpState,&fenceSetup);
    gsKit_prim_sprite_texture(g_rdpState.gs, &fenceTiles[0].gsTexture,
        0,0,0,0,2,1,2,1,0,GS_SETREG_RGBAQ(128,128,128,128,1));
    ps2_texture_end_frame(&g_rdpState);
    fencePixels[0][0]++;
    ps2_texture_upload(&fenceTiles[0], &g_rdpState);
    if (g_rdpState.stats.textureWaits != fenceWaits + 2) failures++;
    printf("[PS2 TEXTURE FENCE TEST] retired reads and later reuse failures=%d\n", failures);
    /* More tint states than LUT slots, then revisit old states with distinct
     * source pointers so a VRAM hit cannot hide a stale combiner lookup. */
    static u8 whites[48][8];
    memset(whites, 255, sizeof(whites));
    rdp_setothermode(0, 0, &g_rdpState);
    rdp_setcombine(0xfcffffff, 0xfffdf6fb, &g_rdpState); /* PRIMITIVE */
    for (int i = 0; i < 48; i++) {
        unsigned tint = 16 + i % 24;
        rdp_setprimcolor(0, (tint << 24) | 0x2030ff, &g_rdpState);
        rdp_settimg(0xfd180001, (u32)whites[i], &g_rdpState);
        rdp_loadtexture(0xf3, 0, 0, &g_rdpState);
        ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
        pixels = (u8*)g_rdpState.tile[0].gsTexture.Mem;
        if (pixels[0] != tint || pixels[1] != 32 || pixels[2] != 48 || pixels[3] != 128) failures++;
    }
    static u8 strips[32][4096];
    u32 stripVram[32];
    memset(strips, 255, sizeof(strips));
    rdp_setothermode(2u << 20, 0, &g_rdpState);
    rdp_settile(0xf5100000, 0, &g_rdpState);
    rdp_settilesize(0xf2000000, (124u << 12) | 252u, &g_rdpState);
    for (int pass = 0; pass < 2; pass++) for (int i = 0; i < 32; i++) {
        rdp_settimg(0xfd10001f, (u32)strips[i], &g_rdpState);
        rdp_loadtexture(0xf3, 0, 0, &g_rdpState);
        ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
        u32 address = g_rdpState.tile[0].gsTexture.Vram;
        if (!pass) stripVram[i] = address;
        else if (address != stripVram[i]) failures++;
    }
    /* Hundreds of changing tint values must reuse the previous frame's
     * version instead of evicting unchanged scene strips. */
    static const u8 colorChanging[] = {255,255,255,255,255,255,255,255};
    u32 changingVram = 0;
    rdp_setothermode(0, 0, &g_rdpState);
    rdp_setcombine(0xfcffffff, 0xfffdf6fb, &g_rdpState);
    rdp_settile(0xf5180000, 0, &g_rdpState);
    rdp_settilesize(0xf2000000, 4u << 12, &g_rdpState);
    rdp_settimg(0xfd180001, (u32)colorChanging, &g_rdpState);
    rdp_loadtexture(0xf3, 0, 0, &g_rdpState);
    for (int frame = 0; frame < 320; frame++) {
        ps2_texture_end_frame(&g_rdpState);
        rdp_setprimcolor(0, ((u32)(frame & 255) << 24) | 0x2030ff, &g_rdpState);
        ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
        if (!frame) changingVram = g_rdpState.tile[0].gsTexture.Vram;
        else if (g_rdpState.tile[0].gsTexture.Vram != changingVram) failures++;
    }
    rdp_setprimcolor(0, 0xc82030ff, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    if (g_rdpState.tile[0].gsTexture.Vram == changingVram) failures++;
    rdp_setprimcolor(0, 0x3f2030ff, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    if (g_rdpState.tile[0].gsTexture.Vram != changingVram) failures++;
    u32 stableUploads = g_rdpState.stats.textureUploads;
    rdp_setothermode(2u << 20, 0, &g_rdpState);
    rdp_setcombine(0xfcffffff, 0xfffdf6fb, &g_rdpState);
    rdp_settile(0xf5100000, 0, &g_rdpState);
    rdp_settilesize(0xf2000000, (124u << 12) | 252u, &g_rdpState);
    for (int i = 0; i < 32; i++) {
        rdp_settimg(0xfd10001f, (u32)strips[i], &g_rdpState);
        rdp_loadtexture(0xf3, 0, 0, &g_rdpState);
        ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
        if (g_rdpState.tile[0].gsTexture.Vram != stripVram[i]) failures++;
    }
    if (g_rdpState.stats.textureUploads != stableUploads) failures++;
    printf("[PS2 CACHE ANIMATION TEST] failures=%d\n", failures);
    /* A large upload may evict several strips, but must fit the same budget. */
    /* A keyboard needs more than 64 small glyph/color combinations. They
     * occupy 1 KiB each, rather than wasting a 4 KiB allocation per letter. */
    static u8 glyphs[128][128];
    u32 glyphVram[128];
    memset(glyphs, 255, sizeof(glyphs));
    rdp_settile(0xf5800000, 0, &g_rdpState); /* I4 */
    rdp_settilesize(0xf2000000, (60u << 12) | 60u, &g_rdpState);
    printf("[PS2 RENDER CHECK] glyph allocation=%u\n", (unsigned)gsKit_texture_size(16,16,GS_PSM_CT32));
    for (int pass = 0; pass < 2; pass++) for (int i = 0; i < 128; i++) {
        rdp_settimg(0xfd80000f, (u32)glyphs[i], &g_rdpState);
        rdp_loadtexture(0xf3, 0, 0, &g_rdpState);
        ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
        u32 address = g_rdpState.tile[0].gsTexture.Vram;
        if (!pass) glyphVram[i] = address;
        else if (address != glyphVram[i]) failures++;
    }
    rdp_settile(0xf5100000, 0, &g_rdpState);
    rdp_settilesize(0xf2000000, (1020u << 12) | 1020u, &g_rdpState);
    rdp_settimg(0xfd1000ff, (u32)strips, &g_rdpState);
    rdp_loadtexture(0xf3, 0, 0, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    if (g_rdpState.tile[0].gsTexture.Vram + 262144 > 0x400000) failures++;
    /* A logo fade changes vertex alpha while retaining its uploaded texels. */
    printf("[PS2 RENDER CHECK] failures before fade=%d\n", failures);
    rdp_setothermode(0, 0, &g_rdpState);
    rdp_setcombine(0xfc119623, 0xff2fffff, &g_rdpState);
    rdp_setprimcolor(0, 0xffffff80, &g_rdpState);
    if (!ps2_combiner_dynamic_alpha(&g_rdpState)) failures++;
    rdp_settile(0xf5180000, 0, &g_rdpState);
    rdp_settilesize(0xf2000000, 4u << 12, &g_rdpState);
    rdp_settimg(0xfd180001, (u32)animated, &g_rdpState);
    rdp_loadtexture(0xf3, 0, 0, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    pixels = (u8*)g_rdpState.tile[0].gsTexture.Mem;
    if (pixels[3] != 128) failures++;
    printf("[PS2 RENDER CHECK] fade dynamic=%d texalpha=%u\n", ps2_combiner_dynamic_alpha(&g_rdpState), pixels[3]);
    u32 fadeVram = g_rdpState.tile[0].gsTexture.Vram;
    RSPVertex fading = {.cw=1};
    GSPRIMSTQPOINT fadedPoint;
    ps2_texture_build_vertex(&g_rdpState.tile[0], &g_rspState, &g_rdpState,
        &fading, 0x80808080, 0, &fadedPoint);
    if (fadedPoint.rgbaq.color.components.a != 64) failures++;
    printf("[PS2 RENDER CHECK] fade vertex alpha=%u\n", fadedPoint.rgbaq.color.components.a);
    rdp_setprimcolor(0, 0xffffff00, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    if (g_rdpState.tile[0].gsTexture.Vram != fadeVram) failures++;
    ps2_texture_build_vertex(&g_rdpState.tile[0], &g_rspState, &g_rdpState,
        &fading, 0x80808080, 0, &fadedPoint);
    if (fadedPoint.rgbaq.color.components.a != 0) failures++;
    /* Primitive alpha used in RGB, or a second cycle, stays on the CPU path. */
    rdp_setcombine((0xfc119623 & ~(31u << 15)) | (10u << 15), 0xff2fffff, &g_rdpState);
    if (ps2_combiner_dynamic_alpha(&g_rdpState)) failures++;
    rdp_setcombine(0xfc119623, 0xff2fffff, &g_rdpState);
    rdp_setothermode(1u << 20, 0, &g_rdpState);
    if (ps2_combiner_dynamic_alpha(&g_rdpState)) failures++;
    rdp_setcombine(0, 0, &g_rdpState);
    printf("[PS2 RENDER CHECK] failures after fade=%d\n", failures);
    /* Runtime S2DEX rectangles have native layout and retain RDP command
     * dispatch. A 320x240 background must not exceed the conversion scratch. */
    static u8 roomImage[320 * 240 * 2];
    for (unsigned i = 0; i < sizeof(roomImage); i += 2) { roomImage[i] = 0xf8; roomImage[i + 1] = 1; }
    static PS2Background background = {.imageW=320*4, .imageH=240*4, .frameW=320*4, .frameH=240*4,
        .imagePtr=roomImage, .imageSiz=G_IM_SIZ_16b, .imageFmt=G_IM_FMT_RGBA, .scaleW=1024, .scaleH=1024};
    u32 backgroundList[][2] = {
        {0xdd00038f, (u32)gspS2DEX2d_fifoTextStart},
        {0xef200000, 0}, {0x0a000000, (u32)&background},
        {0xdd000fff, 0x123456}, {0xdf000000, 0}
    };
    u32 beforeDraw = g_rdpState.stats.submitted, beforeBackground = g_rdpState.stats.backgrounds;
    rdp_to_gs_exec((const Gfx*)backgroundList);
    printf("[PS2 RENDER CHECK] background delta=%u draws=%u source=%p\n",
           g_rdpState.stats.backgrounds - beforeBackground, g_rdpState.stats.submitted - beforeDraw, roomImage);
    if (g_rdpState.stats.backgrounds != beforeBackground + 1 ||
        g_rdpState.stats.submitted != beforeDraw + 8) failures++;
    pixels = (u8*)g_rdpState.tile[0].gsTexture.Mem;
    if (pixels[0] != 255 || pixels[1] != 0 || pixels[2] != 0 || pixels[3] != 128) failures++;
    TileDescriptor repeating = {.width=32, .height=16, .masks=5, .cmt=2};
    RSPVertex mapped = {.cw=2, .tc_s=4096, .tc_t=-512, .sx=10, .sy=20};
    GSPRIMSTQPOINT point;
    g_rspState.texScaleS = g_rspState.texScaleT = 1;
    rdp_setothermode(1u << 19, 0, &g_rdpState);
    ps2_texture_apply_tile(&repeating, &g_rdpState);
    ps2_texture_build_vertex(&repeating, &g_rspState, &g_rdpState, &mapped, 0x80706050, 123, &point);
    if (g_rdpState.gs->Clamp->WMS != 3 || g_rdpState.gs->Clamp->MINU != 31 ||
        g_rdpState.gs->Clamp->WMT != 2 || g_rdpState.gs->Clamp->MAXV != 15 ||
        point.rgbaq.color.q != 0.5f || point.stq.st.s != 2.0f || point.stq.st.t != -0.5f ||
        point.xyz2.xyz.z != 123 || point.rgbaq.color.components.r != 0x50) failures++;
    repeating.shifts = 2; repeating.shiftt = 11;
    ps2_texture_build_vertex(&repeating, &g_rspState, &g_rdpState, &mapped, 0, 0, &point);
    if (point.stq.st.s != 0.5f || point.stq.st.t != -16.0f) failures++;
    rdp_setothermode(0, 0, &g_rdpState);
    ps2_texture_build_vertex(&repeating, &g_rspState, &g_rdpState, &mapped, 0, 0, &point);
    if (point.rgbaq.color.q != 1.0f || point.stq.st.s != 1.0f) failures++;
    /* All encoded shifts, signed UVs, offsets and perspective modes retain
     * the scalar division path's exact values (all divisors are powers of 2). */
    for (int perspective = 0; perspective < 2; perspective++) {
        rdp_setothermode(perspective << 19, 0, &g_rdpState);
        repeating.uls = 12; repeating.ult = 20;
        for (unsigned s = 0; s < 16; s++) for (unsigned t = 0; t < 16; t++) {
            repeating.shifts = s; repeating.shiftt = t;
            float u = mapped.tc_s / 32.0f, v = mapped.tc_t / 32.0f;
            u = (s <= 10 ? u / (1u << s) : u * (1u << (16-s))) - 3;
            v = (t <= 10 ? v / (1u << t) : v * (1u << (16-t))) - 5;
            float q = perspective ? 0.5f : 1;
            ps2_texture_build_vertex(&repeating,&g_rspState,&g_rdpState,&mapped,0,0,&point);
            if (point.stq.st.s != u*q/32 || point.stq.st.t != v*q/16 || point.rgbaq.color.q != q) failures++;
        }
    }
    rdp_setothermode(0,0,&g_rdpState);
    printf("[PS2 UV SCALE TEST] all shifts, offsets and perspective failures=%d\n",failures);
    RSPState reflection = g_rspState;
    memset(reflection.modelview, 0, sizeof(reflection.modelview));
    reflection.modelview[0] = reflection.modelview[5] = reflection.modelview[10] = reflection.modelview[15] = 1;
    reflection.geometryMode = 0x00060000; /* lighting + spherical texture generation */
    static const struct { s16 position[3]; u16 flag; s16 uv[2]; u8 normal[4]; } normal =
        {{0,0,0}, 0, {123,456}, {127,0,0,255}};
    f3dex2_vtx(0x01001002, (u32)&normal, &reflection);
    if (reflection.vtx[0].tc_s != 32767 || reflection.vtx[0].tc_t != 16383) failures++;
    reflection.geometryMode |= 0x00080000; /* linear generation */
    f3dex2_vtx(0x01001002, (u32)&normal, &reflection);
    if (reflection.vtx[0].tc_s != 0 || abs(reflection.vtx[0].tc_t - 16383) > 1) failures++;
    reflection.geometryMode = 0x00060000;
    reflection.modelview[0] = reflection.modelview[5] = 0;
    reflection.modelview[1] = 1; reflection.modelview[4] = -1;
    f3dex2_vtx(0x01001002, (u32)&normal, &reflection);
    if (reflection.vtx[0].tc_s != 16383 || reflection.vtx[0].tc_t != 32767) failures++;
    static const u8 lookY[16] = {0,0,0,0,0,0,0,0,0,127,0};
    f3dex2_movemem(0xdc00000a, (u32)lookY, &reflection);
    f3dex2_vtx(0x01001002, (u32)&normal, &reflection);
    if (reflection.vtx[0].tc_s != 32767 || reflection.lookAt[0][1] != 1) failures++;
    RSPState fogState = g_rspState;
    fogState.geometryMode = 0x00010000;
    /* gSPFogPosition(500,1000): Z/W=0 starts fog, 0.5 is half, 1 is full. */
    f3dex2_moveword(0xdb080000, 256u << 16, &fogState);
    if (ps2_fog_calculate_factor(-1, &fogState) != 255 ||
        ps2_fog_calculate_factor(0, &fogState) != 255 ||
        ps2_fog_calculate_factor(0.5f, &fogState) != 127 ||
        ps2_fog_calculate_factor(1, &fogState) != 0) failures++;
    gs_xyz2 fogVertex = point.xyz2;
    u64 packedFog;
    ps2_fog_apply_xyz(&fogVertex, 0.5f, &fogState);
    memcpy(&packedFog, &fogVertex.xyz, 8);
    if (fogVertex.tag != GS_XYZF2 || (packedFog >> 56) != 127 ||
        (u32)(packedFog >> 32) % 0x1000000 != point.xyz2.xyz.z ||
        fogVertex.xyz.x != point.xyz2.xyz.x || fogVertex.xyz.y != point.xyz2.xyz.y) failures++;
    rdp_setothermode(1u << 20, 0xc8112078u, &g_rdpState); /* FOG_SHADE_A + opaque cycle 2 */
    if (!ps2_fog_enabled(&g_rdpState, &fogState)) failures++;
    fogState.geometryMode = 0;
    if (ps2_fog_enabled(&g_rdpState, &fogState) ||
        ps2_fog_calculate_factor(1, &fogState) != 255) failures++;
    fogState.geometryMode = 0x00010000;
    rdp_setothermode(0, 0xc8112078u, &g_rdpState);
    if (ps2_fog_enabled(&g_rdpState, &fogState)) failures++;
    /* A very narrow room fog range must not use the old generic 0.2..1 curve. */
    f3dex2_moveword(0xdb080000, (16000u << 16) | (u16)-15744, &fogState);
    if (ps2_fog_calculate_factor(0.98f, &fogState) != 255 ||
        ps2_fog_calculate_factor(1, &fogState) != 0) failures++;
    /* Two ground textures must keep distinct VRAM and both draw; changing
     * ENV_ALPHA must not reconvert their unchanged source pixels. */
    static const u8 ground0[] = {0xf8,1,0xf8,1}, ground1[] = {0,0x3f,0,0x3f};
    rdp_setothermode(1u << 20, 0xc8112078u, &g_rdpState);
    rdp_setcombine(0xfc267e04, 0x1ffcfdf8, &g_rdpState);
    rdp_setenvcolor(0, 128, &g_rdpState);
    printf("[PS2 GROUND CHECK] before=%d recognized=%d\n", failures, ps2_combiner_texture_lerp(&g_rdpState));
    if (!ps2_combiner_texture_lerp(&g_rdpState)) failures++;
    u32 unusedPrim, unusedEnv;
    ps2_combiner_constants(&g_rdpState, &unusedPrim, &unusedEnv);
    if (unusedEnv || unusedPrim) failures++;
    for (int i = 0; i < 2; i++) {
        u32 tmem = i ? 256 : 0;
        rdp_settile(0xf5100000 | tmem, (u32)i << 24, &g_rdpState);
        rdp_settilesize(0xf2000000, ((u32)i << 24) | (4u << 12), &g_rdpState);
        rdp_settimg(0xfd100001, (u32)(i ? ground1 : ground0), &g_rdpState);
        rdp_loadtexture(0xf3, 0, (u32)i << 24, &g_rdpState);
    }
    reflection.geometryMode = 0;
    reflection.texEnabled = true; reflection.texTile = 0;
    reflection.texScaleS = reflection.texScaleT = 1;
    for (int i = 0; i < 3; i++) reflection.vtx[i] = (RSPVertex){
        .valid=true, .cw=1, .sz=0.5f, .sx=100 + i*10, .sy=100 + (i==1 ? 10 : 0),
        .cr=255,.cg=255,.cb=255,.ca=255};
    beforeDraw = g_rdpState.stats.submitted;
    f3dex2_tri1(0x05000204, 0, &reflection, &g_rdpState);
    printf("[PS2 GROUND CHECK] draws=%u vram=%08x,%08x fog=%d alpha=%016llx\n",
        g_rdpState.stats.submitted-beforeDraw,g_rdpState.tile[0].gsTexture.Vram,g_rdpState.tile[1].gsTexture.Vram,
        g_rdpState.gs->PrimFogEnable,(unsigned long long)g_rdpState.gs_alpha);
    if (g_rdpState.stats.submitted != beforeDraw + 2 ||
        g_rdpState.tile[0].gsTexture.Vram == g_rdpState.tile[1].gsTexture.Vram ||
        g_rdpState.gs->PrimFogEnable ||
        g_rdpState.gs_alpha != (GS_SETREG_ALPHA(0,2,2,2,0x80))) failures++;
    printf("[PS2 GROUND CHECK] after first=%d expectedAlpha=%016llx\n", failures,
           (unsigned long long)GS_SETREG_ALPHA(0,2,2,2,0x80));
    u32 groundVram[2] = {g_rdpState.tile[0].gsTexture.Vram,g_rdpState.tile[1].gsTexture.Vram};
    u32 groundUploads = g_rdpState.stats.textureUploads;
    rdp_setenvcolor(0, 64, &g_rdpState);
    f3dex2_tri1(0x05000204, 0, &reflection, &g_rdpState);
    if (g_rdpState.stats.textureUploads != groundUploads ||
        g_rdpState.tile[0].gsTexture.Vram != groundVram[0] ||
        g_rdpState.tile[1].gsTexture.Vram != groundVram[1]) failures++;
    printf("[PS2 GROUND CHECK] uploads=%u failures=%d constants=%08x,%08x\n",
        g_rdpState.stats.textureUploads-groundUploads,failures,unusedPrim,unusedEnv);
    printf("[PS2 GROUND CHECK] retained=%08x,%08x previous=%08x,%08x\n",
        g_rdpState.tile[0].gsTexture.Vram,g_rdpState.tile[1].gsTexture.Vram,groundVram[0],groundVram[1]);
    rdp_setenvcolor(0, 0, &g_rdpState);
    beforeDraw = g_rdpState.stats.submitted;
    f3dex2_tri1(0x05000204, 0, &reflection, &g_rdpState);
    if (g_rdpState.stats.submitted != beforeDraw + 1) failures++;
    rdp_setothermode(1u << 20, 0xc8113078u, &g_rdpState);
    if (ps2_combiner_texture_lerp(&g_rdpState)) failures++;
    /* Cutout paths must combine both RGBA sources BEFORE the alpha test.
     * One input has a hole where the other has an opaque blue texel. */
    static u8 path0[] = {0xf8,0,0xf8,1}, path1[] = {0,0x3f,0,0x3e};
    rdp_setcombine(0xfc262a04, 0x1f5893f8, &g_rdpState);
    if (!ps2_combiner_path_lerp(&g_rdpState)) failures++;
    rdp_setprimcolor(0, 0xffffffff, &g_rdpState);
    rdp_setenvcolor(0, 255, &g_rdpState);
    for (int i = 0; i < 2; i++) {
        rdp_settimg(0xfd100001, (u32)(i ? path1 : path0), &g_rdpState);
        rdp_loadtexture(0xf3, 0, (u32)i << 24, &g_rdpState);
    }
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    pixels = (u8*)g_rdpState.tile[0].gsTexture.Mem;
    if (pixels[0] || pixels[2] != 255 || pixels[3] != 128 || pixels[7]) failures++;
    u32 pathVram = g_rdpState.tile[0].gsTexture.Vram;
    u32 pathUploads = g_rdpState.stats.textureUploads;
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    if (g_rdpState.stats.textureUploads != pathUploads || g_rdpState.tile[0].gsTexture.Vram != pathVram) failures++;
    /* The second input is RAM, not immutable ROM: mutation must invalidate. */
    path1[1] = 0x3e;
    ps2_texture_end_frame(&g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    pixels = (u8*)g_rdpState.tile[0].gsTexture.Mem;
    if (pixels[3] || g_rdpState.stats.textureUploads != pathUploads + 1) failures++;
    rdp_setenvcolor(0, 0, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    pixels = (u8*)g_rdpState.tile[0].gsTexture.Mem;
    if (pixels[0] != 255 || pixels[2] || pixels[3] || pixels[7] != 128) failures++;
    path1[1] = 0x3f;
    rdp_setenvcolor(0, 128, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    pixels = (u8*)g_rdpState.tile[0].gsTexture.Mem;
    if (pixels[0] != 127 || pixels[2] != 128 || pixels[3] != 64 || pixels[7] != 64) failures++;
    rdp_setprimcolor(255, 0xffffffff, &g_rdpState);
    ps2_texture_upload(&g_rdpState.tile[0], &g_rdpState);
    pixels = (u8*)g_rdpState.tile[0].gsTexture.Mem;
    if (pixels[3] != 128 || pixels[7] != 64) failures++;
    /* Repeated tile application emits no redundant CLAMP packet. */
    ps2_texture_apply_tile(&g_rdpState.tile[0], &g_rdpState);
    void* queueEnd = g_rdpState.gs->CurQueue->pool_cur;
    ps2_texture_apply_tile(&g_rdpState.tile[0], &g_rdpState);
    if (g_rdpState.gs->CurQueue->pool_cur != queueEnd) failures++;
    printf("[PS2 PATH TEST] failures=%d\n", failures);
    /* TRI2 retains primitive order but shares one GS material packet.
     * Discarded triangles must neither leak into the next command nor
     * interfere with the two-pass ground material. */
    u32 beforePackets = g_rdpState.stats.texturePackets;
    beforeDraw = g_rdpState.stats.submitted;
    f3dex2_tri2(0x06000204, 0x00000204, &reflection, &g_rdpState);
    if (g_rdpState.stats.submitted != beforeDraw + 2 ||
        g_rdpState.stats.texturePackets != beforePackets + 1) failures++;
    for (int i = 0; i < 3; i++) {
        reflection.vtx[i+3] = reflection.vtx[i];
        reflection.vtx[i+3].clipFlags = 1;
    }
    beforePackets = g_rdpState.stats.texturePackets;
    beforeDraw = g_rdpState.stats.submitted;
    f3dex2_tri2(0x0606080a, 0x00000204, &reflection, &g_rdpState);
    if (g_rdpState.stats.submitted != beforeDraw + 1 ||
        g_rdpState.stats.texturePackets != beforePackets + 1) failures++;
    beforePackets = g_rdpState.stats.texturePackets;
    beforeDraw = g_rdpState.stats.submitted;
    f3dex2_tri2(0x0606080a, 0x0006080a, &reflection, &g_rdpState);
    if (g_rdpState.stats.submitted != beforeDraw ||
        g_rdpState.stats.texturePackets != beforePackets) failures++;
    f3dex2_tri1(0x05000204, 0, &reflection, &g_rdpState);
    if (g_rdpState.stats.submitted != beforeDraw + 1 ||
        g_rdpState.stats.texturePackets != beforePackets + 1) failures++;
    rdp_setothermode(1u << 20, 0xc8112078u, &g_rdpState);
    rdp_setcombine(0xfc267e04, 0x1ffcfdf8, &g_rdpState);
    rdp_setenvcolor(0, 128, &g_rdpState);
    beforePackets = g_rdpState.stats.texturePackets;
    beforeDraw = g_rdpState.stats.submitted;
    f3dex2_tri2(0x06000204, 0x00000204, &reflection, &g_rdpState);
    if (g_rdpState.stats.submitted != beforeDraw + 4 ||
        g_rdpState.stats.texturePackets != beforePackets + 4) failures++;
    printf("[PS2 TRI2 TEST] failures=%d\n", failures);
    g_rspState = reflection;
    failures+=ps2_ground_run_smoke();
    failures+=ps2_texture_vertex_smoke();
    rdp_setcombine(0xfcffffff, 0xfffdf6fb, &g_rdpState);
    rdp_setprimcolor(0, 0xffffffff, &g_rdpState);
    const u32 runDL[][2] __attribute__((aligned(8))) = {
        {0x06000204,0x00000204},{0x05000204,0},{0x06000204,0x00000204},{0xdf000000,0}
    };
    beforePackets = g_rdpState.stats.texturePackets;
    beforeDraw = g_rdpState.stats.submitted;
    rdp_to_gs_exec((const Gfx*)runDL);
    if (g_rdpState.stats.submitted != beforeDraw + 5 ||
        g_rdpState.stats.texturePackets != beforePackets + 1) failures++;
    const u32 boundaryDL[][2] __attribute__((aligned(8))) = {
        {0x05000204,0},{0xfb000000,0x80808080},{0x05000204,0},
        {0xe7000000,0},{0x05000204,0},{0xdf000000,0}
    };
    beforePackets = g_rdpState.stats.texturePackets;
    beforeDraw = g_rdpState.stats.submitted;
    rdp_to_gs_exec((const Gfx*)boundaryDL);
    if (g_rdpState.stats.submitted != beforeDraw + 3 ||
        g_rdpState.stats.texturePackets != beforePackets + 3) failures++;
    u32 capacityDL[23][2] __attribute__((aligned(8)));
    for (int i = 0; i < 21; i++) {
        capacityDL[i][0] = 0x05000204; capacityDL[i][1] = 0;
    }
    capacityDL[21][0] = 0x0506080a; capacityDL[21][1] = 0; /* rejected */
    capacityDL[22][0] = 0xdf000000; capacityDL[22][1] = 0;
    beforePackets = g_rdpState.stats.texturePackets;
    beforeDraw = g_rdpState.stats.submitted;
    rdp_to_gs_exec((const Gfx*)capacityDL);
    if (g_rdpState.stats.submitted != beforeDraw + 21 ||
        g_rdpState.stats.texturePackets != beforePackets + 2) failures++;
    printf("[PS2 TRI RUN TEST] mixed commands, state fences, capacity and rejection failures=%d\n", failures);
#ifdef PS2_RENDERER_SMOKE_CAPTURE
    /* Compare the actual GS points against the uncached immediate path,
     * including perspective, fog, dynamic alpha, decal bias and clipping. */
    static GSPRIMSTQPOINT referencePoints[600];
    RSPState pointState=g_rspState;
    for (unsigned fixture=0;fixture<8;fixture++) {
        g_rspState=pointState;
        rdp_setothermode((1u<<19)|(fixture == 1 ? 1u<<20 : 0),
            fixture == 1 ? 0xc8000030u : fixture == 3 ? 0xc10u : 0x30u,&g_rdpState);
        rdp_setcombine(fixture == 2 ? 0xfc119623u : fixture == 6 ? 0xfc121800u : 0,
            fixture == 2 ? 0xff2fffffu : fixture == 6 ? 0xf003fe00u : 0,&g_rdpState);
        rdp_setprimcolor(0,0xffffff71u,&g_rdpState);
        g_rspState.geometryMode=(g_rspState.geometryMode&~0x10000u)|(fixture == 1 ? 0x10000u : 0);
        g_rspState.fogMul=100; g_rspState.fogOff=120;
        if (fixture == 4) { g_rspState.vtx[0].cz=-2*g_rspState.vtx[0].cw; g_rspState.vtx[0].clipFlags=16; }
        if (fixture == 5) {
            g_rspState.vtx[0].sz=-0.5f; g_rspState.vtx[1].sz=1.5f;
            g_rspState.vtx[2].cr=7; g_rspState.vtx[2].cg=229; g_rspState.vtx[2].ca=93;
        }
        if (fixture == 6 && (!ps2_combiner_uses_shade(&g_rdpState,false) ||
            !ps2_combiner_uses_shade(&g_rdpState,true))) failures++;
        if (fixture == 7) g_rspState.geometryMode=0;
        u32 triangle0=fixture == 7 ? 0x06000004 : 0x06000204;
        u32 triangle1=fixture == 7 ? 0x00000004 : 0x00000204;
        f3dex2_test_capture(true);
        u32 builds=g_rdpState.stats.textureVertexBuilds, hits=g_rdpState.stats.textureVertexHits;
        for (unsigned i=0;i<21;i++) f3dex2_tri2(triangle0,triangle1,&g_rspState,&g_rdpState);
        unsigned referenceCount;
        const GSPRIMSTQPOINT* captured=f3dex2_test_points(&referenceCount);
        if (fixture == 1) {
            if (!referenceCount || !ps2_fog_enabled(&g_rdpState,&g_rspState)) failures++;
            for (unsigned i=0;i<referenceCount;i++) if (captured[i].xyz2.tag!=GS_XYZF2) failures++;
        }
        if (referenceCount>600) failures++;
        else memcpy(referencePoints,captured,referenceCount*sizeof(*captured));
        f3dex2_test_capture(true);
        if (!f3dex2_batch_begin(&g_rspState,&g_rdpState)) failures++;
        for (unsigned i=0;i<21;i++) f3dex2_tri2(triangle0,triangle1,&g_rspState,&g_rdpState);
        f3dex2_batch_end(&g_rdpState);
        unsigned cachedCount;
        captured=f3dex2_test_points(&cachedCount);
        if (cachedCount != referenceCount || memcmp(referencePoints,captured,cachedCount*sizeof(*captured))) failures++;
        if (fixture != 3 && fixture != 4 && fixture != 7 &&
            (g_rdpState.stats.textureVertexBuilds-builds != 129 ||
             g_rdpState.stats.textureVertexHits-hits != 123)) failures++;
        if ((fixture == 3 || fixture == 4) && g_rdpState.stats.textureVertexHits != hits) failures++;
        if (fixture == 7 && (g_rdpState.stats.textureVertexBuilds-builds!=128 ||
            g_rdpState.stats.textureVertexHits-hits!=124)) failures++;
        f3dex2_test_capture(false);
        /* A following run must see new vertex/texture/alpha state. */
        g_rspState.vtx[0].tc_s+=16; g_rspState.vtx[0].sx+=3;
        rdp_setprimcolor(0,0xffffff28,&g_rdpState);
        f3dex2_test_capture(true);
        f3dex2_tri2(0x06000204,0x00000204,&g_rspState,&g_rdpState);
        captured=f3dex2_test_points(&referenceCount);
        memcpy(referencePoints,captured,referenceCount*sizeof(*captured));
        f3dex2_test_capture(true);
        if (!f3dex2_batch_begin(&g_rspState,&g_rdpState)) failures++;
        f3dex2_tri2(0x06000204,0x00000204,&g_rspState,&g_rdpState);
        f3dex2_batch_end(&g_rdpState);
        captured=f3dex2_test_points(&cachedCount);
        if (cachedCount != referenceCount || memcmp(referencePoints,captured,cachedCount*sizeof(*captured))) failures++;
        f3dex2_test_capture(false);
    }
    g_rspState=pointState;
    rdp_setothermode(1u<<20,0xc8112078u,&g_rdpState);
    rdp_setcombine(0xfcffffff,0xfffdf6fb,&g_rdpState);
    rdp_setprimcolor(0,0xffffffff,&g_rdpState);
    printf("[PS2 POINT REUSE TEST] GS points, fog, perspective, fade, decals, clipping and invalidation failures=%d\n",failures);

    /* Color-only actor meshes use the same ordering and vertex values as
     * the immediate path, including fog tags and per-triangle decal bias. */
    for (unsigned fixture=0;fixture<7;fixture++) {
        g_rspState=pointState; g_rspState.texEnabled=false;
        g_rspState.geometryMode=(fixture==6 ? 0x600u : 0)|(fixture==1 ? 0x10000u : 0);
        g_rspState.fogMul=100; g_rspState.fogOff=120;
        rdp_setothermode(fixture==1 ? 1u<<20 : fixture==2 ? 2u<<20 : 0,
            fixture==1 ? 0xc8000030u : fixture==3 ? 0xc10u : 0x30u,&g_rdpState);
        rdp_setcombine(fixture==5 ? 0xfc119623u : 0xfcffffffu,
            fixture==5 ? 0xff2fffffu : 0xfffdf6fbu,&g_rdpState);
        rdp_setprimcolor(0,0x792acf71u,&g_rdpState);
        if (fixture==4) { g_rspState.vtx[0].cz=-2*g_rspState.vtx[0].cw; g_rspState.vtx[0].clipFlags=16; }
        f3dex2_test_capture(true);
        for (unsigned i=0;i<21;i++) f3dex2_tri2(0x06000204,0x00000204,&g_rspState,&g_rdpState);
        unsigned referenceCount,cachedCount;
        const GSPRIMSTQPOINT* points=f3dex2_test_points(&referenceCount);
        if (fixture==1) {
            if (!referenceCount || !ps2_fog_enabled(&g_rdpState,&g_rspState)) failures++;
            for (unsigned i=0;i<referenceCount;i++) if (points[i].xyz2.tag!=GS_XYZF2) failures++;
        }
        memcpy(referencePoints,points,referenceCount*sizeof(*points));
        f3dex2_test_capture(true);
        u32 builds=g_rdpState.stats.colorVertexBuilds,hits=g_rdpState.stats.colorVertexHits;
        u32 packets=g_rdpState.stats.colorPackets;
        if (!f3dex2_batch_begin(&g_rspState,&g_rdpState)) failures++;
        for (unsigned i=0;i<21;i++) f3dex2_tri2(0x06000204,0x00000204,&g_rspState,&g_rdpState);
        f3dex2_batch_end(&g_rdpState);
        points=f3dex2_test_points(&cachedCount);
        if (cachedCount!=referenceCount || memcmp(referencePoints,points,cachedCount*sizeof(*points))) failures++;
        if (fixture!=4 && fixture!=6 && (g_rdpState.stats.colorVertexBuilds-builds!=3 ||
            g_rdpState.stats.colorVertexHits-hits!=123 || g_rdpState.stats.colorPackets-packets!=3)) failures++;
        f3dex2_test_capture(false);
        /* A new run must recalculate colors after shade/material changes. */
        g_rspState.vtx[0].cr^=0x81; g_rspState.vtx[0].ca^=0x55;
        rdp_setprimcolor(0,0xbaf03129u,&g_rdpState);
        f3dex2_test_capture(true);
        f3dex2_tri1(0x05000204,0,&g_rspState,&g_rdpState);
        points=f3dex2_test_points(&referenceCount);
        memcpy(referencePoints,points,referenceCount*sizeof(*points));
        f3dex2_test_capture(true);
        f3dex2_batch_begin(&g_rspState,&g_rdpState);
        f3dex2_tri1(0x05000204,0,&g_rspState,&g_rdpState);
        f3dex2_batch_end(&g_rdpState);
        points=f3dex2_test_points(&cachedCount);
        if (cachedCount!=referenceCount || memcmp(referencePoints,points,cachedCount*sizeof(*points))) failures++;
        f3dex2_test_capture(false);
    }
    g_rspState=pointState; g_rspState.texEnabled=false; g_rspState.geometryMode=0;
    rdp_setothermode(0,0x30,&g_rdpState);
    u32 packets=g_rdpState.stats.colorPackets;
    rdp_to_gs_exec((const Gfx*)boundaryDL);
    if (g_rdpState.stats.colorPackets-packets!=3) failures++;
    u32 start=ps2_gfx_cycles();
    for (unsigned i=0;i<100;i++) f3dex2_tri2(0x06000204,0x00000204,&g_rspState,&g_rdpState);
    u32 immediate=ps2_gfx_cycles()-start;
    start=ps2_gfx_cycles();
    f3dex2_batch_begin(&g_rspState,&g_rdpState);
    for (unsigned i=0;i<100;i++) f3dex2_tri2(0x06000204,0x00000204,&g_rspState,&g_rdpState);
    f3dex2_batch_end(&g_rdpState);
    u32 batched=ps2_gfx_cycles()-start;
    printf("[PS2 COLOR RUN TEST] immediateCycles=%u batchedCycles=%u; color, alpha, fog, decals, clipping, culling, capacity and invalidation failures=%d\n",
        immediate,batched,failures);
    g_rspState=pointState;
    rdp_setothermode(1u<<20,0xc8112078u,&g_rdpState);
    rdp_setcombine(0xfcffffff,0xfffdf6fb,&g_rdpState);
    rdp_setprimcolor(0,0xffffffff,&g_rdpState);
#endif
    /* Compare identical EE work with the old per-TRI2 path and native runs. */
    beforePackets = g_rdpState.stats.texturePackets;
    u32 benchStart = ps2_gfx_cycles();
    for (int i = 0; i < 100; i++) f3dex2_tri2(0x06000204,0x00000204,&g_rspState,&g_rdpState);
    u32 immediateCycles = ps2_gfx_cycles() - benchStart;
    u32 immediatePackets = g_rdpState.stats.texturePackets - beforePackets;
    beforePackets = g_rdpState.stats.texturePackets;
    benchStart = ps2_gfx_cycles();
    if (!f3dex2_batch_begin(&g_rspState, &g_rdpState)) failures++;
    for (int i = 0; i < 100; i++) f3dex2_tri2(0x06000204,0x00000204,&g_rspState,&g_rdpState);
    f3dex2_batch_end(&g_rdpState);
    u32 batchedCycles = ps2_gfx_cycles() - benchStart;
    u32 batchedPackets = g_rdpState.stats.texturePackets - beforePackets;
    if (immediatePackets != 100 || batchedPackets != 10) failures++;
    printf("[PS2 TRI RUN BENCH] immediateCycles=%u batchedCycles=%u packets=%u,%u failures=%d\n",
        immediateCycles,batchedCycles,immediatePackets,batchedPackets,failures);
    static u8 inventoryIcons[64][32*32*2] __attribute__((aligned(128)));
    for (unsigned i = 0; i < sizeof(inventoryIcons); i++)
        ((u8*)inventoryIcons)[i] = (i * 29 + 7) & 255;
    for (int variant = 0; variant < 4; variant++) {
        const Gfx* background = ps2_prerender_fixture(variant);
        ps2_texture_end_frame(&g_rdpState);
        u32 uploads = g_rdpState.stats.textureUploads;
        rdp_to_gs_exec(background);
        u32 firstUploads = g_rdpState.stats.textureUploads - uploads;
        if (firstUploads != 8) failures++;
        /* The background must survive other inventory textures, with index
         * collisions, slot replacements and VRAM eviction from prior cases. */
        TileDescriptor icon = {.width=32,.height=32,.fmt=G_IM_FMT_RGBA,.siz=G_IM_SIZ_16b};
        for (int i = 0; i < 64; i++) {
            g_rdpState.textureLoads[0].source = inventoryIcons[i];
            g_rdpState.textureLoads[0].stride = 0;
            ps2_texture_upload(&icon, &g_rdpState);
        }
        ps2_texture_end_frame(&g_rdpState);
        uploads = g_rdpState.stats.textureUploads;
        rdp_to_gs_exec(background);
        u32 repeatUploads = g_rdpState.stats.textureUploads - uploads;
        if (repeatUploads) failures++;
        ps2_prerender_mutate();
        ps2_texture_end_frame(&g_rdpState);
        uploads = g_rdpState.stats.textureUploads;
        rdp_to_gs_exec(background);
        u32 mutatedUploads = g_rdpState.stats.textureUploads - uploads;
        if (mutatedUploads != 1) failures++;
        printf("[PS2 PRERENDER CACHE TEST] variant=%d first=%u repeat=%u changed=%u failures=%d\n",
            variant,firstUploads,repeatUploads,mutatedUploads,failures);
    }
    failures += ps2_prerender_failures();
    /* Exhaustive 5-5-5-1 input coverage compares the fused decoder against
     * the independently tested 8-bit LUT, including transparent inputs. */
    static u8 rgba16All[65536*2] __attribute__((aligned(128)));
    for (unsigned raw = 0; raw < 65536; raw++) {
        rgba16All[raw*2] = raw >> 8; rgba16All[raw*2+1] = raw;
    }
    const u32 mapCases[][4] = {
        {2u<<20,0,0,0xffffffff},
        {0,0xfc119623,0xff2fffff,0x8f734f81},
        {1u<<20,0xfcffffff,0xfffdf6fb,0x39275340}
    };
    for (unsigned material=0;material<3;material++) {
        rdp_setothermode(mapCases[material][0],0,&g_rdpState);
        rdp_setcombine(mapCases[material][1],mapCases[material][2],&g_rdpState);
        rdp_setprimcolor(0,mapCases[material][3],&g_rdpState);
        u8 reference[4][256];
        ps2_combiner_build_lookup(&g_rdpState,reference);
        TileDescriptor exhaustive = {.width=256,.height=256,.fmt=G_IM_FMT_RGBA,.siz=G_IM_SIZ_16b};
        g_rdpState.textureLoads[0].source = rgba16All;
        g_rdpState.textureLoads[0].stride = 0;
        ps2_texture_upload(&exhaustive,&g_rdpState);
        if (!exhaustive.gsTextureLoaded || exhaustive.gsInvWidth != 1.0f/256 || exhaustive.gsInvHeight != 1.0f/256) failures++;
        const u8* converted = (const u8*)exhaustive.gsTexture.Mem;
        for (unsigned raw=0;raw<65536;raw++) {
            unsigned channels[4] = {raw>>11,(raw>>6)&31,(raw>>1)&31,(raw&1)?255:0};
            for (unsigned channel=0;channel<3;channel++) channels[channel] = (channels[channel]<<3)|(channels[channel]>>2);
            for (unsigned channel=0;channel<4;channel++)
                if (converted[raw*4+channel] != reference[channel][channels[channel]]) failures++;
        }
    }
    printf("[PS2 RGBA16 FUSED TEST] 65536 inputs, copy, tint, two-cycle and alpha failures=%d\n",failures);
    /* Compare every compact input with the previous expand-then-map path.
     * IA16 covers all 65536 intensity/alpha pairs; CI4 exercises separate
     * palette banks. Both transparent and opaque entries are intentional. */
    static u8 compactBytes[512] __attribute__((aligned(128)));
    static u8 compactPalette[512] __attribute__((aligned(128)));
    for (unsigned i=0;i<256;i++) {
        compactBytes[i] = i;
        unsigned raw = (i*293u + 0x1020u) & 65535;
        compactPalette[i*2] = raw >> 8; compactPalette[i*2+1] = raw;
    }
    ps2_texture_load_tlut(compactPalette,256);
    const u8 compactFormats[][3] = {
        {G_IM_FMT_I,G_IM_SIZ_4b,0},{G_IM_FMT_I,G_IM_SIZ_8b,0},
        {G_IM_FMT_IA,G_IM_SIZ_4b,0},{G_IM_FMT_IA,G_IM_SIZ_8b,0},
        {G_IM_FMT_IA,G_IM_SIZ_16b,0},{G_IM_FMT_CI,G_IM_SIZ_8b,0},
        {G_IM_FMT_CI,G_IM_SIZ_4b,0},{G_IM_FMT_CI,G_IM_SIZ_4b,3},
        {G_IM_FMT_CI,G_IM_SIZ_4b,15}
    };
    for (unsigned material=0;material<5;material++) {
        rdp_setothermode(material == 0 ? 2u<<20 : material == 4 ? 1u<<20 : 0,0,&g_rdpState);
        rdp_setcombine(materials[material+2][0],materials[material+2][1],&g_rdpState);
        rdp_setprimcolor(0,0x8f734f81u + material*0x13250719u,&g_rdpState);
        rdp_setenvcolor(0,0x2468bcd1u + material*0x19230b17u,&g_rdpState);
        u8 reference[4][256];
        ps2_combiner_build_lookup(&g_rdpState,reference);
        for (unsigned format=0;format<9;format++) {
            unsigned fmt=compactFormats[format][0], siz=compactFormats[format][1];
            unsigned count=siz == G_IM_SIZ_16b ? 65536 : siz == G_IM_SIZ_4b ? 512 : 256;
            const u8* input=siz == G_IM_SIZ_16b ? rgba16All : compactBytes;
            TileDescriptor compact = {.width=siz == G_IM_SIZ_16b ? 256 : 16,
                .height=count/(siz == G_IM_SIZ_16b ? 256 : 16),.fmt=fmt,.siz=siz,
                .palette=compactFormats[format][2]};
            g_rdpState.textureLoads[0].source=input;
            ps2_texture_upload(&compact,&g_rdpState);
            if (!compact.gsTextureLoaded) failures++;
            const u8* output=(const u8*)compact.gsTexture.Mem;
            for (unsigned i=0;i<count;i++) {
                unsigned value=siz == G_IM_SIZ_4b ? ((i&1)?input[i/2]&15:input[i/2]>>4) : input[i];
                unsigned channels[4];
                if (fmt == G_IM_FMT_CI) {
                    unsigned index=siz == G_IM_SIZ_4b ? compact.palette*16+value : value;
                    unsigned raw=(compactPalette[index*2]<<8)|compactPalette[index*2+1];
                    for (unsigned c=0;c<3;c++) {
                        unsigned v=(raw>>(11-c*5))&31;
                        channels[c]=(v<<3)|(v>>2);
                    }
                    channels[3]=(raw&1)?128:0;
                } else {
                    unsigned intensity, alpha;
                    if (fmt == G_IM_FMT_I) {
                        intensity=siz == G_IM_SIZ_4b ? value*17 : value; alpha=intensity>>1;
                    } else if (siz == G_IM_SIZ_16b) {
                        intensity=input[i*2]; alpha=input[i*2+1]*128u/255;
                    } else if (siz == G_IM_SIZ_8b) {
                        intensity=(value>>4)*17; alpha=(value&15)*17*128u/255;
                    } else {
                        intensity=value>>1; intensity=(intensity<<5)|(intensity<<2)|(intensity>>1);
                        alpha=(value&1)?128:0;
                    }
                    channels[0]=channels[1]=channels[2]=intensity; channels[3]=alpha;
                }
                channels[3]=channels[3] == 128 ? 255 : channels[3]*2;
                for (unsigned c=0;c<4;c++) if (output[i*4+c] != reference[c][channels[c]]) failures++;
            }
        }
    }
    printf("[PS2 COMPACT FUSED TEST] I4/I8/IA4/IA8/IA16/CI4/CI8 exhaustive inputs, five materials and palette banks failures=%d\n",failures);
    failures += ps2_offscreen_smoke();
    failures += ps2_frame_capture_smoke();
    failures += ps2_flame_smoke();
    failures += ps2_token_flame_smoke();
    failures += ps2_jewel_smoke();
    failures += ps2_vertex_index_smoke();
    printf("[PS2 RENDER TEST] %s: matrices, viewport, othermode, textures and palettes (%d)\n",
           failures ? "FAIL" : "PASS", failures);
    return failures;
}
