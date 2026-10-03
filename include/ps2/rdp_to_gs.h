/**
 * rdp_to_gs.h — Renderer State & API for OoT PS2 Port
 */

#ifndef RDP_TO_GS_H
#define RDP_TO_GS_H

#include <gsKit.h>
#include "ps2_types.h"

/* ─────────────────────────────────────────────────────────── *
 *  Texture descriptor (mirrors N64 tile state)
 * ─────────────────────────────────────────────────────────── */
#define MAX_TILES  8   /* RDP supports 8 tile descriptors */

/* N64 texture formats */
typedef enum {
    G_IM_FMT_RGBA = 0,
    G_IM_FMT_YUV  = 1,
    G_IM_FMT_CI   = 2,   /* color-index (palette) */
    G_IM_FMT_IA   = 3,   /* intensity + alpha */
    G_IM_FMT_I    = 4,   /* intensity only */
} N64TexFmt;

/* N64 texture bit depths */
typedef enum {
    G_IM_SIZ_4b  = 0,
    G_IM_SIZ_8b  = 1,
    G_IM_SIZ_16b = 2,
    G_IM_SIZ_32b = 3,
} N64TexSiz;

typedef struct {
    void*      addr;        /* pointer to raw N64 texture data     */
    u16        width;       /* texture width in texels             */
    u16        height;      /* texture height in texels            */
    N64TexFmt  fmt;         /* N64 format (RGBA, CI, IA, I, YUV)  */
    N64TexSiz  siz;         /* bit depth (4b, 8b, 16b, 32b)       */
    u16        uls, ult;    /* upper-left (10.2 fixed)             */
    u16        lrs, lrt;    /* lower-right (10.2 fixed)            */
    u8         palette;     /* palette index (for CI textures)     */
    u8         cms, cmt;    /* wrap/mirror flags S/T               */
    u8         masks, maskt;/* mask (log2 of tile dimension)       */
    u8         shifts, shiftt;
    u16        tmem;        /* N64 texture-memory address, in 64-bit words */
    u16        line;

    /* PS2-specific: converted GS texture (uploaded to VRAM) */
    GSTEXTURE  gsTexture;
    float      gsInvWidth, gsInvHeight; /* reciprocal GS power-of-two extents */
    bool       gsTextureLoaded;
    u16        cacheSlot;
} TileDescriptor;

/* ─────────────────────────────────────────────────────────── *
 *  RDP Color Combiner state
 *
 *  The RDP has a 2-cycle color combiner with 4 inputs (A,B,C,D)
 *  per cycle: out = (A - B) * C + D.
 *  On PS2 we approximate with the GS's fixed alpha/color pipeline.
 * ─────────────────────────────────────────────────────────── */
typedef struct {
    u32  w0, w1;        /* raw G_SETCOMBINE words     */
    u8   r, g, b, a;   /* resolved prim color        */
} CombinerState;

/* ─────────────────────────────────────────────────────────── *
 *  Other Mode bits (G_RDPSETOTHERMODE)
 * ─────────────────────────────────────────────────────────── */
typedef struct {
    u32 lo;   /* cycle type, pipeline, texture filter, etc. */
    u32 hi;   /* alpha compare, depth source, blend formula  */
} OtherMode;

/* ─────────────────────────────────────────────────────────── *
 *  RDP State — everything the RDP "remembers" between commands
 * ─────────────────────────────────────────────────────────── */
typedef struct {
    GSGLOBAL*     gs;

    /* Framebuffer & Z-buffer pointers */
    void*         colorImage;
    void*         zImage;
    u32           fbWidth;
    u32           fbFmt;

    /* Tile descriptors */
    TileDescriptor tile[MAX_TILES];
    u8             textureTile;  /* currently selected tile index */
    void*          textureImage;
    N64TexFmt      textureImageFmt;
    N64TexSiz      textureImageSiz;
    u32            textureImageWidth;
    struct {
        const u8* source;
        u32 stride; /* source bytes per row; zero for packed LOADBLOCK */
    } textureLoads[512];

    /* Colors */
    u32            primColor;     /* RGBA packed */
    u8             primLodFrac;
    u32            envColor;
    u32            fogColor;
    u32            blendColor;
    u32            fillColor;

    /* Combiner */
    CombinerState  combiner;

    /* Other modes */
    OtherMode      otherMode;

    /* Scissor rect (10.2 fixed-point) */
    u16            scissorX0, scissorY0;
    u16            scissorX1, scissorY1;

    /* Escritura en el Z-buffer (bit Z_UPD del other mode) */
    bool           zWriteEnabled;

    /* GS state cache (to minimize redundant GS register writes) */
    u64            gs_tex0;
    u64            gs_tex1;
    u64            gs_alpha;
    u64            gs_test;
    u64            gs_clamp;
    u8             gs_clamp_context;
    bool           gs_clamp_valid;
    struct {
        u32 triangles, clipped, culled, invalid, textureFailed, submitted, backgrounds;
        u32 textureHits, textureUploads, textureWaits, texturePackets;
        u32 textureVertexBuilds, textureVertexHits;
        u32 colorPackets, colorVertexBuilds, colorVertexHits;
        u32 stateWrites, stateReuse;
    } stats;
} RDPState;

/* ─────────────────────────────────────────────────────────── *
 *  RSP Vertex Cache (F3DEX2 supports 64 vertices)
 * ─────────────────────────────────────────────────────────── */
#define RSP_VTX_CACHE_SIZE  64
#define RSP_MTX_STACK_DEPTH 11  /* N64 max matrix stack depth */

typedef struct {
    /* Object-space coordinates (from G_VTX) */
    s16  ox, oy, oz;
    u16  flag;
    s16  tc_s, tc_t;   /* texture coordinates (10.5 fixed) */
    u8   cr, cg, cb, ca; /* vertex color / normal */

    /* Clip-space coordinates (after MVP transform) */
    float cx, cy, cz, cw;

    /* Screen-space (after perspective divide + viewport) */
    float sx, sy, sz;

    /* Clip flags */
    u8   clipFlags;
    bool valid;
} RSPVertex;

/* ─────────────────────────────────────────────────────────── *
 *  RSP Lighting Cache (up to 8 lights: 7 directional + 1 ambient)
 * ─────────────────────────────────────────────────────────── */
typedef struct {
    u8 r, g, b;     /* Color */
    float dx, dy, dz; /* Normalized direction */
} RSPLight;

typedef struct {
    RSPVertex    vtx[RSP_VTX_CACHE_SIZE];
    float        mtxStack[RSP_MTX_STACK_DEPTH][16];
    int          mtxStackDepth;
    float        modelview[16];
    float        projection[16];
    float        mvp[16];  /* cached MVP */
    u32          geometryMode;
    bool         texEnabled;
    float        texScaleS, texScaleT;
    u8           texTile;

    /* ── Viewport ──────────────────────────────────────────────────
     * ANTES: sólo existía `u16 viewport[4]`, que nunca se rellenaba
     * porque el viewport se buscaba en el comando equivocado, y además
     * al ser u16 no podía guardar la escala Y negativa que usa N64
     * para invertir el eje vertical. Ahora se guardan en float. */
    float        vpScaleX, vpScaleY;
    float        vpTransX, vpTransY;
    u16          viewport[4];   /* copia cruda, sólo informativa */

    /* ── Niebla (G_MW_FOG) ─────────────────────────────────────── */
    s16          fogMul, fogOff;

    /* ── Iluminación ───────────────────────────────────────────── */
    RSPLight     lights[8];
    u32          numLights;
    float        lookAt[2][3];
} RSPState;

/* ─────────────────────────────────────────────────────────── *
 *  Global state (extern, defined in rdp_to_gs.c)
 * ─────────────────────────────────────────────────────────── */
extern RDPState g_rdpState;
extern RSPState g_rspState;

/* ─────────────────────────────────────────────────────────── *
 *  Public API
 * ─────────────────────────────────────────────────────────── */
void rdp_to_gs_init(GSGLOBAL* gs);
void rdp_to_gs_exec(const Gfx* dl);
void rdp_to_gs_flip(void);
/* Submit and retire all GS reads before releasing CPU task data. Present
 * remains serialized by the graphics worker and may wait for vblank. */
void rdp_to_gs_submit(void);
void rdp_to_gs_present(void);

/* RDP command handlers (implemented in rdp_to_gs.c and f3dex2_ps2.c) */
void rdp_settimg(u32 w0, u32 w1, RDPState* rdp);
void rdp_settile(u32 w0, u32 w1, RDPState* rdp);
void rdp_settilesize(u32 w0, u32 w1, RDPState* rdp);
void rdp_loadtexture(u8 op, u32 w0, u32 w1, RDPState* rdp);
void rdp_loadtlut(u32 w0, u32 w1, RDPState* rdp);
void rdp_setcombine(u32 w0, u32 w1, RDPState* rdp);
void ps2_combiner_color(const RDPState* rdp, const u8 texel[4], const u8 shade[4], u8 output[4]);
void ps2_combiner_vertex_color(const RDPState* rdp, const u8 shade[4], u8 output[4]);
void ps2_combiner_build_lookup(const RDPState* rdp, u8 values[4][256]);
bool ps2_combiner_uses_shade(const RDPState* rdp, bool alpha);
void ps2_combiner_constants(const RDPState* rdp, u32* prim, u32* env);
bool ps2_combiner_dynamic_alpha(const RDPState* rdp);
bool ps2_combiner_texture_lerp(const RDPState* rdp);
bool ps2_combiner_path_lerp(const RDPState* rdp);
void rdp_setprimcolor(u32 w0, u32 w1, RDPState* rdp);
void rdp_setenvcolor(u32 w0, u32 w1, RDPState* rdp);
void rdp_setfogcolor(u32 w0, u32 w1, RDPState* rdp);
void rdp_setblendcolor(u32 w0, u32 w1, RDPState* rdp);
void rdp_setfillcolor(u32 w0, u32 w1, RDPState* rdp);
void rdp_setothermode(u32 w0, u32 w1, RDPState* rdp);
void rdp_fillrect(u32 w0, u32 w1, RDPState* rdp);
void rdp_texrect(u8 op, u32 w0, u32 w1, const Gfx* pc, RDPState* rdp);
void rdp_setscissor(u32 w0, u32 w1, RDPState* rdp);
void rdp_setcimg(u32 w0, u32 w1, RDPState* rdp);
void rdp_setzimg(u32 w0, u32 w1, RDPState* rdp);

/* Versiones parciales de SETOTHERMODE. FALTABAN: OoT las usa
 * constantemente (gDPSetRenderMode, gDPSetCycleType, gDPSetTextureFilter
 * y compañía se codifican así, no con el comando completo). */
void rdp_apply_othermode(RDPState* rdp);
/* Track emitted GS state independently of copied RDP material snapshots. */
void ps2_gs_state_reset(void);
void ps2_gs_set_alpha(RDPState* rdp, u64 alpha);
void ps2_gs_set_test(RDPState* rdp);
void ps2_gs_set_zbuf(RDPState* rdp, u64 zbuf);
void rdp_setothermode_h(u32 w0, u32 w1, RDPState* rdp);
void rdp_setothermode_l(u32 w0, u32 w1, RDPState* rdp);

/* Por frame */
void ps2_rdp_begin_frame(RDPState* rdp);
void PS2Gfx_DumpTrace(void);

/* Iluminación (ps2_lighting.c) */
void ps2_light_load(u32 index, const u8* src, RSPState* rsp);
void ps2_light_set_count(u32 numLights, RSPState* rsp);
void ps2_light_apply_vertex(RSPVertex* v, const RSPState* rsp);
/* Prepare once before processing a G_VTX load; the light table must remain
 * unchanged until its prepared vertices have all been processed. */
void ps2_light_prepare_batch(const RSPState* rsp);
void ps2_light_apply_prepared_vertex(RSPVertex* v, const RSPState* rsp);

/* Niebla (ps2_fog.c) */
void ps2_fog_update(RDPState* rdp);
u8   ps2_fog_calculate_factor(float z, const RSPState* rsp);
bool ps2_fog_enabled(const RDPState* rdp, const RSPState* rsp);
void ps2_fog_apply_xyz(gs_xyz2* xyz, float z, const RSPState* rsp);

/* Z-buffer (ps2_zbuffer.c) */
void ps2_zbuffer_update_test(RDPState* rdp);
void ps2_zbuffer_set_writes(RDPState* rdp, bool enable);
u32 ps2_zbuffer_value(const RDPState* rdp, float normalizedDepth);
void ps2_zbuffer_decal_bias(const RDPState* rdp, const RSPVertex* a,
    const RSPVertex* b, const RSPVertex* c, u32 depth[3]);

#endif /* RDP_TO_GS_H */
