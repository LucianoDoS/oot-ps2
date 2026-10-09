/**
 * f3dex2_ps2.c — Comandos de geometría F3DEX2 en PS2
 *
 * ══════════════════════════════════════════════════════════════════════
 *  RESUMEN DE LO QUE ESTABA MAL AQUÍ
 * ══════════════════════════════════════════════════════════════════════
 *
 * 1. VIEWPORT NUNCA SE CONFIGURABA  ← el bug que dejaba la pantalla en negro
 *
 *    gSPViewport() se codifica como G_MOVEMEM con índice 0x80. El código
 *    anterior buscaba ese 0x80 dentro de f3dex2_moveword(), que atiende
 *    a G_MOVEWORD, un opcode DISTINTO. Así que rsp->viewport[] se quedaba
 *    en ceros para siempre y en transform_vertex() eso hacía
 *    vx=vy=tx=ty=0, colapsando TODOS los vértices al punto (0,0).
 *
 * 2. DECODIFICACIÓN DE G_MOVEMEM MAL SACADA
 *
 *    gDma2p empaqueta: índice en los bits [7:0], offset/8 en [15:8] y
 *    (len-1)/8 en [23:19]. El código leía el índice de los bits [16:23],
 *    que es donde va el índice en G_MOVEWORD, no en G_MOVEMEM.
 *
 * 3. LUCES CON EL ÍNDICE EQUIVOCADO
 *
 *    Comparaba con G_MV_LIGHT = 2, que es la numeración del F3DEX
 *    ANTIGUO. En F3DEX2 las luces van de G_MV_L0 = 0x86 en adelante, de
 *    dos en dos. Nunca se cargó una luz.
 *
 * 4. G_POPMTX LEÍA LA PALABRA EQUIVOCADA
 *
 *    En F3DEX2 el número de bytes a desapilar viaja en w1, no en w0.
 *    Desapilaba basura.
 *
 * 5. GEOMETRY MODE BORRABA LOS BITS ALTOS
 *
 *    La máscara de borrado sólo cubre 24 bits; el código hacía
 *    `mode & (w0 & 0xFFFFFF)`, cargándose siempre los 8 bits altos.
 *
 * 6. DIRECCIONES SEGMENTADAS SIN TRADUCIR (ver ps2_segment.h)
 *
 * 7. MATRIZ MODELVIEW MAL APILADA
 *
 *    Al hacer PUSH copiaba la matriz y AVANZABA el puntero de pila, pero
 *    luego escribía sobre la copia, con lo que perdía el nivel anterior
 *    en cuanto había un LOAD. Ahora se apila el estado actual y se
 *    trabaja siempre sobre la cima.
 */

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <gsKit.h>
#include <gsInline.h>

#include "ps2_types.h"
#include "ps2_segment.h"
#include "rdp_to_gs.h"
#include "f3dex2_ps2.h"
#include "ps2_gfx_profile.h"
#include "ps2_texture.h"
#include "ps2_triangle_packet.h"
#include "ps2_color_scale.h"

/* ───────────────────────────────────────────────────────────────── *
 *  Bits de geometry mode (F3DEX2)
 * ───────────────────────────────────────────────────────────────── */
#define G_ZBUFFER            0x00000001
#define G_SHADE              0x00000004
#define G_CULL_FRONT         0x00000200
#define G_CULL_BACK          0x00000400
#define G_CULL_BOTH          (G_CULL_FRONT | G_CULL_BACK)
#define G_FOG                0x00010000
#define G_LIGHTING           0x00020000
#define G_TEXTURE_GEN        0x00040000
#define G_TEXTURE_GEN_LINEAR 0x00080000
#define G_SHADING_SMOOTH     0x00200000

/* ───────────────────────────────────────────────────────────────── *
 *  Índices de G_MOVEMEM
 *
 *  CUIDADO CON ESTO: gbi.h define DOS juegos de valores y es muy fácil
 *  coger el equivocado, porque el #else lleva un comentario que repite
 *  F3DEX_GBI_2 aunque esa rama sea justo la del F3DEX ANTIGUO. El
 *  comentario etiqueta qué macro se comprobó en el #ifdef, no qué rama
 *  es cuál.
 *
 *      #ifdef F3DEX_GBI_2        ← ESTA es la buena para OoT
 *          G_MV_VIEWPORT  8
 *          G_MV_LIGHT     10
 *      #else
 *          G_MV_VIEWPORT  0x80   ← F3DEX antiguo, NO vale aquí
 *          G_MV_L0        0x86
 *      #endif
 *
 *  Los valores 0x80/0x86 son los que usaba el código anterior (y los
 *  que puse yo mismo en la primera pasada de esta corrección). Son del
 *  microcódigo viejo y con ellos el viewport y las luces nunca se
 *  configuran.
 * ───────────────────────────────────────────────────────────────── */
#define G_MV_MMTX      2
#define G_MV_PMTX      6
#define G_MV_VIEWPORT  8
#define G_MV_LIGHT     10
#define G_MV_POINT     12

/* Desplazamientos dentro de la tabla de luces (G_MVO_*) */
#define G_MVO_LOOKATX  (0 * 24)
#define G_MVO_LOOKATY  (1 * 24)
#define G_MVO_L0       (2 * 24)   /* la luz 0 empieza en el offset 48 */

/* Índices de G_MOVEWORD */
#define G_MW_MATRIX     0x00
#define G_MW_NUMLIGHT   0x02
#define G_MW_CLIP       0x04
#define G_MW_SEGMENT    0x06
#define G_MW_FOG        0x08
#define G_MW_LIGHTCOL   0x0A
#define G_MW_PERSPNORM  0x0E

/* Flags de G_MTX en F3DEX2 */
#define G_MTX_MODELVIEW   0x00
#define G_MTX_PROJECTION  0x04
#define G_MTX_MUL         0x00
#define G_MTX_LOAD        0x02
#define G_MTX_NOPUSH      0x00
#define G_MTX_PUSH        0x01

/* ───────────────────────────────────────────────────────────────── *
 *  Utilidades de matrices
 * ───────────────────────────────────────────────────────────────── */

/* Mtx de N64: enteros y fracciones separados, formato s15.16 */
static void n64_mtx_to_float(const u8* src, float* m) {
    for (int i = 0; i < 16; i++) {
        u16 ip = ASSET_U16(src + i * 2);          /* parte entera   */
        u16 fp = ASSET_U16(src + 32 + i * 2);     /* parte fraccion */
        s32 v  = (s32)(((u32)ip << 16) | (u32)fp);
        m[i] = (float)v / 65536.0f;
    }
}

static void mat4_mul(float* dst, const float* a, const float* b) {
    float tmp[16];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            tmp[r*4+c] = a[r*4+0]*b[0*4+c] + a[r*4+1]*b[1*4+c]
                       + a[r*4+2]*b[2*4+c] + a[r*4+3]*b[3*4+c];
        }
    }
    memcpy(dst, tmp, sizeof(tmp));
}

static void rsp_update_mvp(RSPState* rsp) {
    /* libultra stores row-vector matrices: vertex * model * projection. */
    mat4_mul(rsp->mvp, rsp->modelview, rsp->projection);
}

/* ───────────────────────────────────────────────────────────────── *
 *  Transformación de vértice
 * ───────────────────────────────────────────────────────────────── */
#ifdef PS2_RENDERER_SMOKE_CAPTURE
/* Compare complete primitive output against the original eager viewport
 * calculation. The production renderer never carries this switch. */
static bool s_geometryReference;
void f3dex2_test_geometry_reference(bool enabled) { s_geometryReference = enabled; }
#endif

static void project_vertex(RSPVertex* v, const RSPState* rsp) {
    if (v->cw > 0.0001f || v->cw < -0.0001f) {
        float invW = 1.0f / v->cw;
        v->sx = v->cx * invW * rsp->vpScaleX + rsp->vpTransX;
        v->sy = v->cy * invW * rsp->vpScaleY + rsp->vpTransY;
        v->sz = v->cz * invW * 0.5f + 0.5f;
        if (v->sz < 0) v->sz = 0;
        if (v->sz > 1) v->sz = 1;
    } else {
        v->clipFlags = 0xFF;
        v->sx = v->sy = 0;
        v->sz = 1;
    }
}

static void transform_vertex(RSPVertex* v, const RSPState* rsp) {
    const float* mvp = rsp->mvp;
    float x = (float)v->ox, y = (float)v->oy, z = (float)v->oz;

    v->cx = mvp[0]*x + mvp[4]*y + mvp[ 8]*z + mvp[12];
    v->cy = mvp[1]*x + mvp[5]*y + mvp[ 9]*z + mvp[13];
    v->cz = mvp[2]*x + mvp[6]*y + mvp[10]*z + mvp[14];
    v->cw = mvp[3]*x + mvp[7]*y + mvp[11]*z + mvp[15];

    /* Flags de recorte ANTES de dividir (con w todavía válido) */
    v->clipFlags = 0;
    if (v->cx < -v->cw) v->clipFlags |= 0x01;
    if (v->cx >  v->cw) v->clipFlags |= 0x02;
    if (v->cy < -v->cw) v->clipFlags |= 0x04;
    if (v->cy >  v->cw) v->clipFlags |= 0x08;
    if (v->cz < -v->cw) v->clipFlags |= 0x10;
    if (v->cz >  v->cw) v->clipFlags |= 0x20;

    /* Outside vertices are either rejected using clipFlags, or clipped in
     * homogeneous space and projected afterwards. Their initial viewport
     * values are never consumed by a primitive. Retain the near-zero W
     * path: it also changes clipFlags to 0xFF, including unordered W. */
    bool project = !v->clipFlags || !(v->cw > 0.0001f || v->cw < -0.0001f);
#ifdef PS2_RENDERER_SMOKE_CAPTURE
    project = project || s_geometryReference;
#endif
    if (project) project_vertex(v, rsp);
    else { v->sx = v->sy = 0; v->sz = 1; }
}

static void generate_texture_coordinates(RSPVertex* v, const RSPState* rsp) {
    if ((rsp->geometryMode & (G_LIGHTING | G_TEXTURE_GEN)) != (G_LIGHTING | G_TEXTURE_GEN)) return;
    const float* m = rsp->modelview;
    float x = (s8)v->cr, y = (s8)v->cg, z = (s8)v->cb;
    float n[3] = {x*m[0] + y*m[4] + z*m[8],
                  x*m[1] + y*m[5] + z*m[9],
                  x*m[2] + y*m[6] + z*m[10]};
    float length = sqrtf(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
    if (length > 0.0f) for (int i = 0; i < 3; i++) n[i] /= length;
    float coords[2];
    for (int axis = 0; axis < 2; axis++) {
        float dot = n[0]*rsp->lookAt[axis][0] + n[1]*rsp->lookAt[axis][1] + n[2]*rsp->lookAt[axis][2];
        dot = fmaxf(-1.0f, fminf(1.0f, dot));
        coords[axis] = rsp->geometryMode & G_TEXTURE_GEN_LINEAR ? acosf(dot) / 3.14159265358979323846f : (dot + 1.0f) * 0.5f;
    }
    v->tc_s = (s16)(coords[0] * 32767.0f);
    v->tc_t = (s16)(coords[1] * 32767.0f);
}

/* ───────────────────────────────────────────────────────────────── *
 *  G_VTX — carga vértices en la caché del RSP
 *
 *  Codificación F3DEX2:
 *    w0[19:12] = número de vértices
 *    w0[ 7: 1] = índice final + 1 (o sea: v0 = ese valor - n)
 *    w1        = dirección SEGMENTADA del array
 * ───────────────────────────────────────────────────────────────── */
static void invalidate_run_vertices(unsigned first, unsigned count);
void f3dex2_vtx(u32 w0, u32 w1, RSPState* rsp) {
    int count = (int)((w0 >> 12) & 0xFF);
    int vend  = (int)((w0 >>  1) & 0x7F);
    int v0    = vend - count;

    /* AHORA SÍ: traducimos la dirección segmentada */
    const u8* src = (const u8*)ps2_segmented_to_virtual(w1);
    if (!src || count <= 0) return;

    if (v0 < 0) v0 = 0;
    if (v0 + count > RSP_VTX_CACHE_SIZE) {
        count = RSP_VTX_CACHE_SIZE - v0;
        if (count <= 0) return;
    }

    invalidate_run_vertices(v0, count);
    /* Assets and runtime vertices live in EE user RAM. A corrupt inventory
     * pointer must not trigger TLB misses or reuse the preceding vertices. */
    if ((uintptr_t)src < 0x100000u || (uintptr_t)src > 0x2000000u - count*16u) {
        static unsigned invalidSources;
        if (invalidSources++ < 4) {
            printf("[PS2 INVALID VTX] w0=%08x w1=%08x source=%p count=%d\n", w0,w1,src,count);
            PS2Gfx_DumpTrace();
        }
        for (int i = 0; i < count; i++) rsp->vtx[v0+i].valid = false;
        return;
    }

    ps2_light_prepare_batch(rsp);
    for (int i = 0; i < count; i++) {
        RSPVertex* v = &rsp->vtx[v0 + i];

        /* Vtx de N64 = 16 bytes:
         *   s16 x,y,z;  u16 flag;  s16 s,t;  u8 r,g,b,a  */
        v->ox   = ASSET_S16(src + 0);
        v->oy   = ASSET_S16(src + 2);
        v->oz   = ASSET_S16(src + 4);
        v->flag = ASSET_U16(src + 6);
        v->tc_s = ASSET_S16(src + 8);
        v->tc_t = ASSET_S16(src + 10);
        v->cr   = src[12];
        v->cg   = src[13];
        v->cb   = src[14];
        v->ca   = src[15];
        v->valid = true;

        u32 start = ps2_gfx_profile_enter();
        transform_vertex(v, rsp);
        ps2_gfx_profile_leave(GFX_PROFILE_TRANSFORM, start);
        start = ps2_gfx_profile_enter();
        generate_texture_coordinates(v, rsp);
        ps2_gfx_profile_leave(GFX_PROFILE_TEXGEN, start);
        start = ps2_gfx_profile_enter();
        ps2_light_apply_prepared_vertex(v, rsp);
        ps2_gfx_profile_leave(GFX_PROFILE_LIGHT, start);

        /* Bounded diagnostics for room geometry, independent of actor vertices. */
        static unsigned roomTrace;
        if (i == 0 && (w1 >> 24) == 3 && roomTrace++ < 8) {
            printf("[PS2 ROOM VTX] addr=%08x pos=%d,%d,%d clip=%f,%f,%f,%f screen=%f,%f mode=%08x\n",
                w1, v->ox, v->oy, v->oz, v->cx, v->cy, v->cz, v->cw,
                v->sx, v->sy, rsp->geometryMode);
            printf("[PS2 ROOM MVP] %f,%f,%f,%f / %f,%f,%f,%f / %f,%f,%f,%f / %f,%f,%f,%f\n",
                rsp->mvp[0], rsp->mvp[1], rsp->mvp[2], rsp->mvp[3],
                rsp->mvp[4], rsp->mvp[5], rsp->mvp[6], rsp->mvp[7],
                rsp->mvp[8], rsp->mvp[9], rsp->mvp[10], rsp->mvp[11],
                rsp->mvp[12], rsp->mvp[13], rsp->mvp[14], rsp->mvp[15]);
        }

        src += 16;
    }
}

/* ───────────────────────────────────────────────────────────────── *
 *  Envío de un triángulo al GS
 * ───────────────────────────────────────────────────────────────── */
/* TRI2 shares a single material. Submit its surviving/clipped triangles
 * together, avoiding repeated TEX/PRIM packet setup without reordering DLs. */
static bool s_tri2Batch;
static bool s_triangleRun;
static unsigned s_tri2Count;
static GSPRIMSTQPOINT s_tri2Points[60] __attribute__((aligned(8)));
static GSPRIMPOINT s_triColorPoints[60] __attribute__((aligned(8)));
static bool s_runColored;
static u64 s_runColorValid;
static u64 s_runColors[RSP_VTX_CACHE_SIZE];
static GSTEXTURE s_tri2Texture;
static bool s_tri2Fog;
static bool s_runTileBound;
static bool s_runRgbShade, s_runAlphaShade, s_runFog;
static u64 s_runVertexValid;
#if PS2_GFX_SCRATCHPAD
#define s_runVertices (PS2_GFX_WORKSPACE.vertices)
#else
static GSPRIMSTQPOINT s_runVertices[RSP_VTX_CACHE_SIZE] __attribute__((aligned(8)));
#endif
static bool s_runGround;
#ifdef PS2_FLOOR_DIAGNOSTIC
static volatile unsigned s_floorRequested;
static unsigned s_floorMode, s_floorFrame;
static bool s_floorSampled;
static unsigned s_floorTriangles, s_floorRebased, s_floorOutsideBefore, s_floorOutsideAfter;
static float s_floorMaxBefore, s_floorMaxAfter, s_floorMaxSpan;
void ps2_floor_request_mode(unsigned mode) {
    if (mode <= PS2_FLOOR_LEGACY) s_floorRequested = mode;
}
void ps2_floor_begin_frame(void) {
    if (s_floorFrame && s_floorFrame % 60 == 0 && s_floorTriangles) {
        printf("[PS2 FLOOR UV] frame=%u mode=%u triangles=%u rebased=%u beforeMax=%.6g afterMax=%.6g beforeOutside=%u afterOutside=%u maxSpan=%.6g\n",
            s_floorFrame, s_floorMode, s_floorTriangles, s_floorRebased,
            s_floorMaxBefore, s_floorMaxAfter, s_floorOutsideBefore, s_floorOutsideAfter, s_floorMaxSpan);
    }
    s_floorTriangles=s_floorRebased=s_floorOutsideBefore=s_floorOutsideAfter=0;
    s_floorMaxBefore=s_floorMaxAfter=s_floorMaxSpan=0;
    s_floorMode = s_floorRequested;
    s_floorFrame++;
    s_floorSampled = false;
}
unsigned ps2_floor_mode(void) { return s_floorMode; }
static bool floor_trace(const RSPState* rsp, const RDPState* rdp,
                        const TileDescriptor* tile, const RSPVertex* const* vertices,
                        const GSPRIMSTQPOINT* points) {
    if (s_floorSampled || s_floorFrame % 60 || !ps2_combiner_texture_lerp(rdp)) return false;
    s_floorSampled = true;
    printf("[PS2 FLOOR] frame=%u mode=%u hi=%08x lo=%08x env=%08x size=%ux%u scale=%.6g,%.6g shift=%u,%u\n",
        s_floorFrame, s_floorMode, rdp->otherMode.hi, rdp->otherMode.lo,
        rdp->envColor, (unsigned)tile->gsTexture.Width, (unsigned)tile->gsTexture.Height,
        rsp->texScaleS, rsp->texScaleT, (unsigned)tile->shifts, (unsigned)tile->shiftt);
    for (unsigned i=0; i<3; i++) {
        const RSPVertex* v=vertices[i];
        printf("[PS2 FLOOR V] i=%u uv=%.6g,%.6g w=%.6g xy=%.6g,%.6g stq=%.9g,%.9g,%.9g gs=%u,%u,%u\n",
            i, v->tc_s, v->tc_t, v->cw, v->sx, v->sy,
            points[i].stq.st.s, points[i].stq.st.t, points[i].rgbaq.color.q,
            (unsigned)points[i].xyz2.xyz.x, (unsigned)points[i].xyz2.xyz.y,
            (unsigned)points[i].xyz2.xyz.z);
    }
    return true;
}
#endif
static u64 s_runGroundVertexValid;
#if PS2_GFX_SCRATCHPAD
#define s_runGroundVertices (PS2_GFX_WORKSPACE.groundVertices)
#else
static GSPRIMSTQPOINT s_runGroundVertices[RSP_VTX_CACHE_SIZE] __attribute__((aligned(8)));
#endif
static u32 s_runGroundColors[RSP_VTX_CACHE_SIZE], s_runGroundDepths[RSP_VTX_CACHE_SIZE];
static PS2TextureVertexSetup s_runPointSetup[2];
static bool s_runPointSetupValid[2];
static u64 s_runGroundUvValid;
static float s_runGroundUv[RSP_VTX_CACHE_SIZE][2];
static void invalidate_run_vertices(unsigned first, unsigned count) {
    if (!s_triangleRun) return;
    /* Full-cache loads must not shift by 64. Keep unaffected slots, material
     * setup and copied queued points; invalidate both ground layers and UVs. */
    u64 mask = count == RSP_VTX_CACHE_SIZE ? ~(u64)0 :
               (((1ull << count) - 1) << first);
    s_runVertexValid &= ~mask;
    s_runGroundVertexValid &= ~mask;
    s_runGroundUvValid &= ~mask;
    s_runColorValid &= ~mask;
}
#ifdef PS2_RENDERER_SMOKE_CAPTURE
static bool s_captureEnabled;
static unsigned s_captureCount;
static GSPRIMSTQPOINT s_capturePoints[600];
void f3dex2_test_capture(bool enabled) { s_captureEnabled = enabled; s_captureCount = 0; }
const GSPRIMSTQPOINT* f3dex2_test_points(unsigned* count) { *count = s_captureCount; return s_capturePoints; }
static void capture_points(const GSPRIMSTQPOINT* points) {
    if (s_captureEnabled && s_captureCount + 3 <= 600) {
        memcpy(s_capturePoints+s_captureCount, points, 3*sizeof(*points)); s_captureCount += 3;
    }
}
static void capture_color_points(const GSPRIMPOINT* points) {
    GSPRIMSTQPOINT canonical[3];
    memset(canonical,0,sizeof(canonical));
    for (unsigned i=0;i<3;i++) {
        canonical[i].rgbaq=points[i].rgbaq;
        canonical[i].xyz2=points[i].xyz2;
    }
    capture_points(canonical);
}
#endif

#ifdef PS2_RENDERER_SMOKE_CAPTURE
static bool s_indexReference;
void f3dex2_test_index_reference(bool enabled) { s_indexReference = enabled; }
static bool s_batchCopyReference;
void f3dex2_test_batch_copy_reference(bool enabled) { s_batchCopyReference = enabled; }
/* The old pointer/stride calculation is kept only for the EE output oracle. */
static int reference_vertex_slot(const RSPState* rsp, const RSPVertex* vertex) {
    uintptr_t address=(uintptr_t)vertex, base=(uintptr_t)rsp->vtx;
    if (!s_triangleRun || address<base || address>=base+sizeof(rsp->vtx) ||
        (address-base)%sizeof(*vertex)) return -1;
    return (address-base)/sizeof(*vertex);
}
#endif

static void prepare_texture_point(const TileDescriptor* tile, const RSPState* rsp,
    RDPState* rdp, const RSPVertex* vertex, u32 color, u32 depth, bool fog,
    int slot, bool second, GSPRIMSTQPOINT* output) {
    /* A triangle may itself repeat a vertex index. The earlier lookup ran
     * before any of its points were built, so retain this local check. */
    u64* valid=second?&s_runGroundVertexValid:&s_runVertexValid;
    GSPRIMSTQPOINT* cached=second?s_runGroundVertices:s_runVertices;
    if (slot>=0 && (*valid & (1ull<<slot))) {
        *output=cached[slot]; rdp->stats.textureVertexHits++; return;
    }
    u32 start = ps2_gfx_profile_enter();
    if (s_triangleRun) {
        unsigned layer = second ? 1 : 0;
        if (!s_runPointSetupValid[layer]) {
            ps2_texture_prepare_vertices(&s_runPointSetup[layer], tile, rsp, rdp);
            s_runPointSetupValid[layer] = true;
        }
        ps2_texture_build_vertex_prepared(&s_runPointSetup[layer], vertex, color, depth, output);
    } else {
        ps2_texture_build_vertex(tile,rsp,rdp,vertex,color,depth,output);
    }
    if (fog) ps2_fog_apply_xyz(&output->xyz2,vertex->cz/vertex->cw,rsp);
    ps2_gfx_profile_leave(GFX_PROFILE_POINT,start);
    rdp->stats.textureVertexBuilds++;
    if (slot >= 0) {
        cached[slot]=*output; *valid|=1ull<<slot;
        if (s_runGround && !second) { s_runGroundColors[slot]=color; s_runGroundDepths[slot]=depth; }
    }
}
static bool s_runFilterValid;
static u32 s_runFilter;
static bool s_runPacketValid[2];
static PS2TriangleTextureSetup s_runPacketSetup[2];
static bool s_runTileSetupValid[2];
static PS2TextureTileSetup s_runTileSetup[2];
static void apply_texture_tile(TileDescriptor* tile, RDPState* rdp, bool second) {
    u32 start = ps2_gfx_profile_enter();
    if (!s_triangleRun) {
        ps2_texture_apply_tile(tile,rdp);
        ps2_gfx_profile_leave(GFX_PROFILE_TILE,start);
        return;
    }
    unsigned layer=second?1:0;
    if (!s_runTileSetupValid[layer]) {
        ps2_texture_prepare_tile(&s_runTileSetup[layer],tile,rdp);
        s_runTileSetupValid[layer]=true;
    }
    ps2_texture_apply_tile_prepared(tile,rdp,&s_runTileSetup[layer]);
    ps2_gfx_profile_leave(GFX_PROFILE_TILE,start);
}
static void submit_texture_packet(GSGLOBAL* gs, GSTEXTURE* texture, int count,
    const GSPRIMSTQPOINT* points, bool second) {
    /* Only primitives and draw-state-preserving traversal execute in a run.
     * CLAMP/ALPHA changes and
     * texture uploads don't change TEX1. Every state command ends the run. */
    bool setFilter = !s_triangleRun || !s_runFilterValid || s_runFilter != texture->Filter;
    if (s_triangleRun) {
        unsigned layer = second ? 1 : 0;
        if (!s_runPacketValid[layer]) {
            ps2_triangle_prepare_texture(&s_runPacketSetup[layer], gs, texture);
            s_runPacketValid[layer] = true;
        }
        ps2_prim_trusted_textured_triangles(gs, &s_runPacketSetup[layer], count, points,
            setFilter, gs->PrimFogEnable ? GS_XYZF2 : GS_XYZ2);
    } else {
        PS2TriangleTextureSetup setup;
        ps2_triangle_prepare_texture(&setup, gs, texture);
        ps2_prim_trusted_textured_triangles(gs, &setup, count, points, setFilter,
            gs->PrimFogEnable ? GS_XYZF2 : GS_XYZ2);
    }
    if (s_triangleRun) { s_runFilter = texture->Filter; s_runFilterValid = true; }
}
static void flush_tri2(RDPState* rdp) {
    if (!s_tri2Count) return;
    int previousFog = rdp->gs->PrimFogEnable;
    rdp->gs->PrimFogEnable = s_tri2Fog;
    if (s_runColored) {
        ps2_prim_trusted_colored_triangles(rdp->gs,s_tri2Count,s_triColorPoints,
            s_tri2Fog ? GS_XYZF2 : GS_XYZ2);
        rdp->stats.colorPackets++;
    } else {
        submit_texture_packet(rdp->gs, &s_tri2Texture, s_tri2Count, s_tri2Points, false);
        rdp->stats.texturePackets++;
    }
    rdp->gs->PrimFogEnable = previousFog;
    s_tri2Count = 0;
}

bool f3dex2_batch_begin(RSPState* rsp, RDPState* rdp) {
    /* Ground passes retain base/overlay order for EACH triangle. Reuse both
     * sets of points within the run, but never group their GS draws. */
    s_runGround=rsp->texEnabled && ps2_combiner_texture_lerp(rdp);
    s_triangleRun = true;
    s_tri2Batch = !s_runGround;
    s_tri2Count = 0;
    s_runTileBound = false;
    s_runVertexValid = 0;
    s_runGroundVertexValid = 0;
    s_runGroundUvValid = 0;
    s_runColorValid = 0;
    s_runPointSetupValid[0] = s_runPointSetupValid[1] = false;
    s_runFilterValid = false;
    s_runPacketValid[0] = s_runPacketValid[1] = false;
    s_runTileSetupValid[0] = s_runTileSetupValid[1] = false;
    s_runColored = !rsp->texEnabled;
    s_runRgbShade = rsp->texEnabled && ps2_combiner_uses_shade(rdp,false);
    s_runAlphaShade = rsp->texEnabled && ps2_combiner_uses_shade(rdp,true);
    s_runFog = ps2_fog_enabled(rdp,rsp);
    return true;
}

void f3dex2_batch_end(RDPState* rdp) {
    flush_tri2(rdp);
    s_triangleRun = false;
    s_tri2Batch = false;
    s_runGround = false;
}

static void submit_vertices(RSPVertex* v0, RSPVertex* v1, RSPVertex* v2,
                            RSPState* rsp, RDPState* rdp, int i0, int i1, int i2) {
    GSGLOBAL* gs = rdp->gs;
    if (!gs) return;
    u32 setupStart = ps2_gfx_profile_enter();

    /* ── Culling ──────────────────────────────────────────────────
     * Antes sólo se miraba G_CULL_BACK y con el signo fijo. El signo
     * correcto depende de si el viewport invierte Y (que es el caso:
     * vpScaleY es negativo), así que se calcula el área con signo y se
     * comparan las dos banderas por separado. */
    float ex = v1->sx - v0->sx, ey = v1->sy - v0->sy;
    float fx = v2->sx - v0->sx, fy = v2->sy - v0->sy;
    float area = ex * fy - ey * fx;

    u32 cull = rsp->geometryMode & G_CULL_BOTH;
    if (cull == G_CULL_BOTH) {
        rdp->stats.culled++;
        ps2_gfx_profile_leave(GFX_PROFILE_DRAW_SETUP,setupStart);
        return;
    }
    /* N64 front faces are CCW before the viewport's Y inversion. */
    if ((cull == G_CULL_BACK && area >= 0.0f) ||
        (cull == G_CULL_FRONT && area <= 0.0f)) {
        rdp->stats.culled++;
        ps2_gfx_profile_leave(GFX_PROFILE_DRAW_SETUP,setupStart);
        return;
    }

    /* ── Color ────────────────────────────────────────────────────── */
    u64 c0, c1, c2;
    u64* colors[3] = {&c0, &c1, &c2};
    RSPVertex* vertices[3] = {v0, v1, v2};
    /* TRI already validated these indices. Avoid recovering them from
     * pointers with a range check, remainder and division for every corner.
     * Clipping supplies -1: interpolated vertices must never reuse a slot. */
    int cacheSlots[3] = {s_triangleRun ? i0 : -1,
                        s_triangleRun ? i1 : -1,
                        s_triangleRun ? i2 : -1};
#ifdef PS2_RENDERER_SMOKE_CAPTURE
    if (s_indexReference) for (unsigned i = 0; i < 3; i++)
        cacheSlots[i] = reference_vertex_slot(rsp, vertices[i]);
#endif
    bool decal = (rdp->otherMode.lo & 0xc30) == 0xc10;
    bool pointReady[3] = {false,false,false};
    int pointSlots[3] = {-1,-1,-1};
    GSPRIMSTQPOINT texturePoints[3] __attribute__((aligned(8)));
    GSPRIMSTQPOINT* points=texturePoints;
    /* The unused batch tail is safe until the count commits below. Keep the
     * temporary for overflow so the existing flush stays in draw order. */
    if (rsp->texEnabled && s_tri2Batch && !s_runGround && s_tri2Count+3<=60
#ifdef PS2_RENDERER_SMOKE_CAPTURE
        && !s_batchCopyReference
#endif
    ) points=s_tri2Points+s_tri2Count;
    /* The cached GS point already includes shade, alpha and depth. Look it
     * up before calculating those inputs again. Clipped temporaries and
     * per-triangle decal slopes still use the complete preparation path. */
    if (rsp->texEnabled && s_triangleRun && !decal) {
        for (unsigned i=0;i<3;i++) {
            int slot=pointSlots[i]=cacheSlots[i];
            if (slot>=0 && (s_runVertexValid & (1ull<<slot))) {
                points[i]=s_runVertices[slot]; pointReady[i]=true;
                if (s_runGround) *colors[i]=s_runGroundColors[slot];
                rdp->stats.textureVertexHits++;
            }
        }
    }
    bool rgbShade = s_triangleRun ? s_runRgbShade :
        rsp->texEnabled && ps2_combiner_uses_shade(rdp, false);
    bool alphaShade = s_triangleRun ? s_runAlphaShade :
        rsp->texEnabled && ps2_combiner_uses_shade(rdp, true);
    for (int i = 0; i < 3; i++) {
        if (pointReady[i]) continue;
        RSPVertex* v = vertices[i];
        int colorSlot = rsp->texEnabled ? -1 : cacheSlots[i];
        if (colorSlot>=0 && (s_runColorValid & (1ull<<colorSlot))) {
            *colors[i]=s_runColors[colorSlot];
            rdp->stats.colorVertexHits++;
            continue;
        }
        u8 shade[4] = {v->cr, v->cg, v->cb, v->ca}, color[4];
        if (rsp->texEnabled) {
            color[0] = rgbShade ? ps2_color_scale_byte(shade[0]) : 128;
            color[1] = rgbShade ? ps2_color_scale_byte(shade[1]) : 128;
            color[2] = rgbShade ? ps2_color_scale_byte(shade[2]) : 128;
            color[3] = alphaShade ? ps2_color_scale_byte(shade[3]) : 128;
        } else {
            u32 start = ps2_gfx_profile_enter();
            ps2_combiner_vertex_color(rdp, shade, color);
            ps2_gfx_profile_leave(GFX_PROFILE_VERTEX_COLOR, start);
            color[3] = ps2_color_scale_byte(color[3]);
            rdp->stats.colorVertexBuilds++;
        }
        *colors[i] = GS_SETREG_RGBAQ(color[0], color[1], color[2], color[3], 1.0f);
        if (colorSlot>=0) {
            s_runColors[colorSlot]=*colors[i]; s_runColorValid |= 1ull<<colorSlot;
        }
    }

    /* ── Profundidad ──────────────────────────────────────────────
     * El GS usa 0 = lejos y 0xFFFFFF = cerca (al revés que N64), por
     * eso se invierte. Antes se mandaba sin invertir, con lo que el
     * z-test habría descartado justo lo que tocaba dibujar. */
    u32 z0 = pointReady[0] ? (s_runGround?s_runGroundDepths[pointSlots[0]]:0) : ps2_zbuffer_value(rdp, v0->sz);
    u32 z1 = pointReady[1] ? (s_runGround?s_runGroundDepths[pointSlots[1]]:0) : ps2_zbuffer_value(rdp, v1->sz);
    u32 z2 = pointReady[2] ? (s_runGround?s_runGroundDepths[pointSlots[2]]:0) : ps2_zbuffer_value(rdp, v2->sz);
    if (decal) {
        u32 depthsDecal[3] = {z0, z1, z2};
        ps2_zbuffer_decal_bias(rdp, v0, v1, v2, depthsDecal);
        z0 = depthsDecal[0]; z1 = depthsDecal[1]; z2 = depthsDecal[2];
    }
    bool fog = s_triangleRun ? s_runFog : ps2_fog_enabled(rdp, rsp);
    ps2_gfx_profile_leave(GFX_PROFILE_DRAW_SETUP,setupStart);

    if (rsp->texEnabled) {
        TileDescriptor* tile = &rdp->tile[rsp->texTile & 7];
        if (!tile->gsTextureLoaded) {
            s_runPacketValid[0] = false;
            s_runTileSetupValid[0] = false;
            ps2_texture_upload(tile, rdp);
        }
        if (!tile->gsTextureLoaded) { rdp->stats.textureFailed++; return; }

        if (!s_triangleRun || !s_runTileBound || s_runGround) {
            apply_texture_tile(tile, rdp, false);
            if (s_triangleRun) s_runTileBound = true;
        }
        if (!pointReady[0]) prepare_texture_point(tile,rsp,rdp,v0,(u32)c0,z0,fog,pointSlots[0],false,&points[0]);
        if (!pointReady[1]) prepare_texture_point(tile,rsp,rdp,v1,(u32)c1,z1,fog,pointSlots[1],false,&points[1]);
        if (!pointReady[2]) prepare_texture_point(tile,rsp,rdp,v2,(u32)c2,z2,fog,pointSlots[2],false,&points[2]);
#ifdef PS2_FLOOR_DIAGNOSTIC
        const RSPVertex* floorVertices[3]={v0,v1,v2};
        bool traceFloor=floor_trace(rsp,rdp,tile,floorVertices,points);
#endif
#ifdef PS2_RENDERER_SMOKE_CAPTURE
        capture_points(points);
#endif
        int previousFog = gs->PrimFogEnable;
        gs->PrimFogEnable = fog;
        if (s_tri2Batch) {
            if (s_tri2Count + 3 > 60) flush_tri2(rdp);
            if (!s_tri2Count) { s_tri2Texture = tile->gsTexture; s_tri2Fog = fog; }
            if (points != s_tri2Points+s_tri2Count)
                memcpy(s_tri2Points+s_tri2Count, points, sizeof(texturePoints));
            s_tri2Count += 3;
        } else {
            submit_texture_packet(gs, &tile->gsTexture, 3, points, false);
            rdp->stats.texturePackets++;
        }
        gs->PrimFogEnable = previousFog;
        /* Kokiri ground uses (TEXEL1-TEXEL0)*ENV_ALPHA+TEXEL0, then
         * SHADE. Match that opaque material using a second GS pass with
         * its own tile coordinates, and the same depth and fog factors. */
        if ((s_triangleRun?s_runGround:ps2_combiner_texture_lerp(rdp)) && (rdp->envColor & 255)
#ifdef PS2_FLOOR_DIAGNOSTIC
            && ps2_floor_mode() != PS2_FLOOR_BASE_ONLY
#endif
        ) {
            TileDescriptor* second = &rdp->tile[(rsp->texTile + 1) & 7];
            if (!second->gsTextureLoaded) {
                s_runPacketValid[1] = false;
                s_runTileSetupValid[1] = false;
                ps2_texture_upload(second, rdp);
            }
            if (second->gsTextureLoaded) {
                u32 depths[3] = {z0,z1,z2};
                u32 alpha = ((rdp->envColor & 255) * 128 + 127) / 255;
                apply_texture_tile(second, rdp, true);
                for (int i = 0; i < 3; i++) {
                    u32 color = ((u32)*colors[i] & 0xffffffu) | (alpha << 24);
                    prepare_texture_point(second,rsp,rdp,vertices[i],color,depths[i],fog,
                                          pointSlots[i],true,&points[i]);
                }
                /* Work on draw copies, never the shared vertex cache. A whole
                 * triangle moves by the same integer number of wrap periods. */
                const RSPVertex* wrapVertices[3]={v0,v1,v2};
                PS2TextureVertexSetup localSetup;
                const PS2TextureVertexSetup* wrapSetup;
                if (s_triangleRun) wrapSetup=&s_runPointSetup[1];
                else {
                    ps2_texture_prepare_vertices(&localSetup,second,rsp,rdp);
                    wrapSetup=&localSetup;
                }
                float wrapUv[2][3];
                for (unsigned i=0;i<3;i++) {
                    int slot=pointSlots[i];
                    float uv[2];
                    if (slot>=0 && (s_runGroundUvValid & (1ull<<slot))) {
                        uv[0]=s_runGroundUv[slot][0]; uv[1]=s_runGroundUv[slot][1];
                    } else {
                        ps2_texture_vertex_coordinates(wrapSetup,wrapVertices[i],uv);
                        if (slot>=0) {
                            s_runGroundUv[slot][0]=uv[0]; s_runGroundUv[slot][1]=uv[1];
                            s_runGroundUvValid |= 1ull<<slot;
                        }
                    }
                    wrapUv[0][i]=uv[0]; wrapUv[1][i]=uv[1];
                }
#ifdef PS2_FLOOR_DIAGNOSTIC
                PS2TextureWrapResult wrap;
                ps2_texture_rebase_prepared_triangle(wrapSetup,wrapUv,points,
                    ps2_floor_mode()==PS2_FLOOR_NORMAL,&wrap);
                s_floorTriangles++;
                s_floorRebased += !!(wrap.offsetS || wrap.offsetT);
                s_floorOutsideBefore += wrap.outsideBefore; s_floorOutsideAfter += wrap.outsideAfter;
                if (wrap.maxBefore>s_floorMaxBefore) s_floorMaxBefore=wrap.maxBefore;
                if (wrap.maxAfter>s_floorMaxAfter) s_floorMaxAfter=wrap.maxAfter;
                if (wrap.maxSpan>s_floorMaxSpan) s_floorMaxSpan=wrap.maxSpan;
#else
                ps2_texture_rebase_prepared_triangle(wrapSetup,wrapUv,points,true,NULL);
#endif
#ifdef PS2_RENDERER_SMOKE_CAPTURE
                capture_points(points);
#endif
                ps2_gs_set_alpha(rdp, GS_SETREG_ALPHA(0,1,0,1,0x80));
                gs->PrimFogEnable = fog;
                submit_texture_packet(gs, &second->gsTexture, 3, points, true);
                rdp->stats.texturePackets++;
#ifdef PS2_FLOOR_DIAGNOSTIC
                if (traceFloor) {
                    printf("[PS2 FLOOR LAYER] mode=%u writes=%u compare=%u shift=%u,%u depth=%u,%u,%u size=%ux%u wrap=%u,%u mask=%u,%u offset=%.6g,%.6g\n",
                        ps2_floor_mode(),rdp->zWriteEnabled,!!(rdp->otherMode.lo&0x10),
                        (unsigned)second->shifts,(unsigned)second->shiftt,
                        (unsigned)depths[0],(unsigned)depths[1],(unsigned)depths[2],
                        (unsigned)second->gsTexture.Width,(unsigned)second->gsTexture.Height,
                        (unsigned)second->cms,(unsigned)second->cmt,(unsigned)second->masks,(unsigned)second->maskt,
                        wrap.offsetS,wrap.offsetT);
                    for (unsigned i=0;i<3;i++) printf("[PS2 FLOOR OVERLAY] i=%u stq=%.9g,%.9g,%.9g xyz=%u,%u,%u\n",
                        i,points[i].stq.st.s,points[i].stq.st.t,points[i].rgbaq.color.q,
                        (unsigned)points[i].xyz2.xyz.x,(unsigned)points[i].xyz2.xyz.y,(unsigned)points[i].xyz2.xyz.z);
                }
#endif
                gs->PrimFogEnable = previousFog;
                ps2_gs_set_alpha(rdp, rdp->gs_alpha);
                rdp->stats.submitted++;
            } else rdp->stats.textureFailed++;
        }
    } else {
        GSPRIMPOINT points[3] __attribute__((aligned(8)));
        u32 depths[3] = {z0,z1,z2};
        for (int i = 0; i < 3; i++) {
            points[i].rgbaq = rgba_to_RGBAQ((u32)*colors[i], 1);
            points[i].xyz2 = vertex_to_XYZ2(gs, vertices[i]->sx, vertices[i]->sy, depths[i]);
            if (fog) ps2_fog_apply_xyz(&points[i].xyz2, vertices[i]->cz / vertices[i]->cw, rsp);
        }
#ifdef PS2_RENDERER_SMOKE_CAPTURE
        capture_color_points(points);
#endif
        int previousFog = gs->PrimFogEnable;
        gs->PrimFogEnable = fog;
        if (s_tri2Batch) {
            if (s_tri2Count+3>60) flush_tri2(rdp);
            if (!s_tri2Count) s_tri2Fog=fog;
            memcpy(s_triColorPoints+s_tri2Count,points,sizeof(points)); s_tri2Count+=3;
        } else {
            ps2_prim_trusted_colored_triangles(gs,3,points, fog ? GS_XYZF2 : GS_XYZ2);
            rdp->stats.colorPackets++;
        }
        gs->PrimFogEnable = previousFog;
    }
    rdp->stats.submitted++;
}

static inline __attribute__((always_inline)) float clip_distance(const RSPVertex* v, int plane) {
    switch (plane) {
        case 0: return v->cw + v->cx;
        case 1: return v->cw - v->cx;
        case 2: return v->cw + v->cy;
        case 3: return v->cw - v->cy;
        case 4: return v->cw + v->cz;
        default: return v->cw - v->cz;
    }
}

static void profiled_submit(RSPVertex* a, RSPVertex* b, RSPVertex* c,
                            RSPState* rsp, RDPState* rdp, int i0, int i1, int i2) {
    u32 start = ps2_gfx_profile_enter();
    submit_vertices(a,b,c,rsp,rdp,i0,i1,i2);
    ps2_gfx_profile_leave(GFX_PROFILE_SUBMIT,start);
}

/* Clip before dividing by W. Projecting a vertex behind the camera directly
 * produces enormous inverted polygons even when the GS scissor is correct. */
int ps2_clip_triangle(const RSPVertex* a, const RSPVertex* b, const RSPVertex* c,
                      RSPVertex* output) {
    RSPVertex polygons[2][12];
    polygons[0][0] = *a; polygons[0][1] = *b; polygons[0][2] = *c;
    int count = 3, current = 0;
    for (int plane = 0; plane < 6 && count; plane++) {
        RSPVertex* src = polygons[current];
        RSPVertex* dst = polygons[current ^ 1];
        float distances[12];
        int inside = 0;
        for (int i = 0; i < count; i++) {
            distances[i] = clip_distance(&src[i], plane);
            inside += distances[i] >= 0;
        }
        /* The original all-inside pass copied the polygon without changing
         * its order or any field. Test the current interpolated polygon,
         * rather than initial flags: rounding can cross a later plane. */
        if (inside == count) continue;
        if (!inside) { count = 0; break; }
        int nextCount = 0;
        for (int i = 0; i < count; i++) {
            int next = i + 1 < count ? i + 1 : 0;
            const RSPVertex* first = &src[i];
            const RSPVertex* last = &src[next];
            float d0 = distances[i], d1 = distances[next];
            if (d0 >= 0) dst[nextCount++] = *first;
            if ((d0 >= 0) != (d1 >= 0)) {
                float t = d0 / (d0 - d1);
                RSPVertex* v = &dst[nextCount++];
                *v = *first;
#define INTERPOLATE(field) v->field = first->field + (last->field - first->field) * t
                INTERPOLATE(cx); INTERPOLATE(cy); INTERPOLATE(cz); INTERPOLATE(cw);
                INTERPOLATE(tc_s); INTERPOLATE(tc_t);
                INTERPOLATE(cr); INTERPOLATE(cg); INTERPOLATE(cb); INTERPOLATE(ca);
#undef INTERPOLATE
                v->clipFlags = 0;
            }
        }
        count = nextCount;
        current ^= 1;
    }
    memcpy(output, polygons[current], count * sizeof(*output));
    return count;
}

static void submit_triangle(int i0, int i1, int i2, RSPState* rsp, RDPState* rdp) {
    rdp->stats.triangles++;
    if (i0 >= RSP_VTX_CACHE_SIZE || i1 >= RSP_VTX_CACHE_SIZE || i2 >= RSP_VTX_CACHE_SIZE) {
        rdp->stats.invalid++; return;
    }
    RSPVertex *a = &rsp->vtx[i0], *b = &rsp->vtx[i1], *c = &rsp->vtx[i2];
    if (!a->valid || !b->valid || !c->valid) { rdp->stats.invalid++; return; }
    if (a->clipFlags & b->clipFlags & c->clipFlags) { rdp->stats.clipped++; return; }
    if (!(a->clipFlags | b->clipFlags | c->clipFlags)) {
        profiled_submit(a, b, c, rsp, rdp, i0, i1, i2); return;
    }
    RSPVertex polygon[12];
    u32 start = ps2_gfx_profile_enter();
    int count = ps2_clip_triangle(a, b, c, polygon);
    ps2_gfx_profile_leave(GFX_PROFILE_CLIP,start);
    if (count < 3) { rdp->stats.clipped++; return; }
    for (int i = 0; i < count; i++) project_vertex(&polygon[i], rsp);
    for (int i = 1; i + 1 < count; i++)
        profiled_submit(&polygon[0], &polygon[i], &polygon[i + 1], rsp, rdp, -1, -1, -1);
}

/* ───────────────────────────────────────────────────────────────── *
 *  G_TRI1 / G_TRI2
 *  Los índices vienen multiplicados por 2 (por eso el >>1).
 * ───────────────────────────────────────────────────────────────── */
void f3dex2_tri1(u32 w0, u32 w1, RSPState* rsp, RDPState* rdp) {
    (void)w1;
    int i0 = (int)((w0 >> 17) & 0x7F);
    int i1 = (int)((w0 >>  9) & 0x7F);
    int i2 = (int)((w0 >>  1) & 0x7F);
    submit_triangle(i0, i1, i2, rsp, rdp);
}

void f3dex2_tri2(u32 w0, u32 w1, RSPState* rsp, RDPState* rdp) {
    if (!s_triangleRun) {
        s_tri2Batch = rsp->texEnabled && !ps2_combiner_texture_lerp(rdp);
        s_runColored = !rsp->texEnabled;
        s_tri2Count = 0;
    }
    int i0 = (int)((w0 >> 17) & 0x7F);
    int i1 = (int)((w0 >>  9) & 0x7F);
    int i2 = (int)((w0 >>  1) & 0x7F);
    int i3 = (int)((w1 >> 17) & 0x7F);
    int i4 = (int)((w1 >>  9) & 0x7F);
    int i5 = (int)((w1 >>  1) & 0x7F);
    submit_triangle(i0, i1, i2, rsp, rdp);
    submit_triangle(i3, i4, i5, rsp, rdp);
    if (!s_triangleRun) {
        flush_tri2(rdp);
        s_tri2Batch = false;
    }
}

/* ───────────────────────────────────────────────────────────────── *
 *  G_MTX — cargar o multiplicar matriz
 *
 *  F3DEX2 pone los flags en w0[7:0] y con el bit PUSH INVERTIDO
 *  (gSPMatrix hace `(p) ^ G_MTX_PUSH`).
 * ───────────────────────────────────────────────────────────────── */
void f3dex2_mtx(u32 w0, u32 w1, RSPState* rsp) {
    u8 flags = (u8)((w0 & 0xFF) ^ G_MTX_PUSH);

    const u8* src = (const u8*)ps2_segmented_to_virtual(w1);
    if (!src) return;

    float m[16];
    n64_mtx_to_float(src, m);

    if (flags & G_MTX_PROJECTION) {
        if (flags & G_MTX_LOAD) {
            memcpy(rsp->projection, m, sizeof(m));
        } else {
            mat4_mul(rsp->projection, m, rsp->projection);
        }
    } else {
        /* PUSH: guardamos el estado ACTUAL en la pila y seguimos
         * trabajando sobre modelview. Así el POP restaura de verdad. */
        if (flags & G_MTX_PUSH) {
            if (rsp->mtxStackDepth < RSP_MTX_STACK_DEPTH - 1) {
                memcpy(rsp->mtxStack[rsp->mtxStackDepth],
                       rsp->modelview, sizeof(rsp->modelview));
                rsp->mtxStackDepth++;
            }
        }

        if (flags & G_MTX_LOAD) {
            memcpy(rsp->modelview, m, sizeof(m));
        } else {
            mat4_mul(rsp->modelview, m, rsp->modelview);
        }
    }

    rsp_update_mvp(rsp);
}

/* ───────────────────────────────────────────────────────────────── *
 *  G_POPMTX
 *
 *  En F3DEX2 el número de BYTES a desapilar viene en w1 (gDma2p mete
 *  (num)*64 en el campo de dirección). El código anterior lo leía de
 *  w0 y desapilaba una cantidad arbitraria.
 * ───────────────────────────────────────────────────────────────── */
void f3dex2_popmtx(u32 w0, u32 w1, RSPState* rsp) {
    (void)w0;
    int pops = (int)(w1 / 64);
    if (pops < 1) pops = 1;

    rsp->mtxStackDepth -= pops;
    if (rsp->mtxStackDepth < 0) rsp->mtxStackDepth = 0;

    memcpy(rsp->modelview, rsp->mtxStack[rsp->mtxStackDepth],
           sizeof(rsp->modelview));
    rsp_update_mvp(rsp);
}

/* ───────────────────────────────────────────────────────────────── *
 *  G_GEOMETRYMODE
 *
 *  w0[23:0] = ~(bits a borrar);  w1 = bits a poner.
 *  Hay que forzar a 1 los 8 bits altos de la máscara para no borrarlos
 *  sin querer (fallo de la versión anterior).
 * ───────────────────────────────────────────────────────────────── */
void f3dex2_geometrymode(u32 w0, u32 w1, RSPState* rsp) {
    u32 keepMask = (w0 & 0x00FFFFFF) | 0xFF000000;
    rsp->geometryMode = (rsp->geometryMode & keepMask) | w1;
}

/* ───────────────────────────────────────────────────────────────── *
 *  G_TEXTURE
 *    w0[13:11] = nivel de mipmap
 *    w0[10: 8] = tile
 *    w0[ 7: 1] = on/off
 *    w1[31:16] = escala S   w1[15:0] = escala T   (0.16 fijo, 1.0 = 0xFFFF)
 * ───────────────────────────────────────────────────────────────── */
void f3dex2_texture(u32 w0, u32 w1, RSPState* rsp, RDPState* rdp) {
    rsp->texEnabled = (((w0 >> 1) & 0x7F) != 0);
    rsp->texTile    = (u8)((w0 >> 8) & 0x7);
    rdp->textureTile = rsp->texTile;

    /* 0xFFFF representa 1.0, no 0x7FFF. La versión anterior dividía
     * entre 32767 y por tanto duplicaba la escala de TODAS las
     * texturas. */
    rsp->texScaleS = (float)((w1 >> 16) & 0xFFFF) / 65535.0f;
    rsp->texScaleT = (float)( w1        & 0xFFFF) / 65535.0f;
}

/* ───────────────────────────────────────────────────────────────── *
 *  G_MOVEWORD
 *    w0[23:16] = índice   w0[15:0] = offset   w1 = dato
 * ───────────────────────────────────────────────────────────────── */
void f3dex2_moveword(u32 w0, u32 w1, RSPState* rsp) {
    u8  index  = (u8)((w0 >> 16) & 0xFF);
    u16 offset = (u16)(w0 & 0xFFFF);

    switch (index) {
        case G_MW_NUMLIGHT:
            /* F3DEX2: NUML(n) = n * 24. Se despeja el número de luces. */
            rsp->numLights = w1 / 24;
            if (rsp->numLights > 7) rsp->numLights = 7;
            break;

        case G_MW_SEGMENT:
            /* El juego también configura segmentos desde la DL.
             * offset = segmento * 4 */
            {
                u32 seg = offset / 4;
                if (seg < PS2_NUM_SEGMENTS) {
                    /* G_MW_SEGMENT writes a physical base, not a segmented
                     * pointer. Never modify the CPU's segment table here. */
                    gRspSegments[seg] = (uintptr_t)(w1 & 0x1FFFFFFF);
                }
            }
            break;

        case G_MW_FOG:
            /* w1[31:16] = multiplicador, w1[15:0] = desplazamiento */
            rsp->fogMul = (s16)((w1 >> 16) & 0xFFFF);
            rsp->fogOff = (s16)( w1        & 0xFFFF);
            break;

        case G_MW_LIGHTCOL:
            /* Cambia el color de una luz ya cargada.
             * offset: a0=0x00, b0=0x04, a1=0x18... → luz = offset/24 */
            {
                u32 li = offset / 24;
                if (li < 8) {
                    rsp->lights[li].r = (u8)((w1 >> 24) & 0xFF);
                    rsp->lights[li].g = (u8)((w1 >> 16) & 0xFF);
                    rsp->lights[li].b = (u8)((w1 >>  8) & 0xFF);
                }
            }
            break;

        default:
            break;
    }
}

/* ───────────────────────────────────────────────────────────────── *
 *  G_MOVEMEM  ← AQUÍ ESTABA EL BUG DEL VIEWPORT
 *
 *  Codificación real (gDma2p):
 *    w0[23:19] = (longitud - 1) / 8
 *    w0[15: 8] = offset / 8
 *    w0[ 7: 0] = índice        ← el código anterior lo leía de [23:16]
 *    w1        = dirección segmentada
 * ───────────────────────────────────────────────────────────────── */
void f3dex2_movemem(u32 w0, u32 w1, RSPState* rsp) {
    u8  index  = (u8)(w0 & 0xFF);
    u32 offset = ((w0 >> 8) & 0xFF) * 8;
    u32 length = (((w0 >> 19) & 0x1F) + 1) * 8;
    (void)length;

    const u8* src = (const u8*)ps2_segmented_to_virtual(w1);
    if (!src) return;

    if (index == G_MV_VIEWPORT) {
        /* gSPViewport() → G_MOVEMEM, índice 8, offset 0 */
        /* Vp = { s16 vscale[4]; s16 vtrans[4]; } en formato 2.10 fijo.
         * Dividir entre 4 lo pasa a píxeles. */
        float sx = (float)ASSET_S16(src + 0)  / 4.0f;
        float sy = (float)ASSET_S16(src + 2)  / 4.0f;
        float tx = (float)ASSET_S16(src + 8)  / 4.0f;
        float ty = (float)ASSET_S16(src + 10) / 4.0f;

        /* El eje Y del GS crece hacia abajo igual que en N64, y el
         * vscale de N64 ya viene negativo para eso, así que se respeta
         * tal cual. */
        float outputX = ps2_render_scale_x(&g_rdpState);
        float outputY = ps2_render_scale_y(&g_rdpState);
        rsp->vpScaleX = sx * outputX;
        rsp->vpScaleY = -sy * outputY;
        rsp->vpTransX = tx * outputX;
        rsp->vpTransY = ty * outputY;

        /* Copia cruda por si algo más la consulta */
        rsp->viewport[0] = (u16)ASSET_S16(src + 0);
        rsp->viewport[1] = (u16)ASSET_S16(src + 2);
        rsp->viewport[2] = (u16)ASSET_S16(src + 8);
        rsp->viewport[3] = (u16)ASSET_S16(src + 10);

    } else if (index == G_MV_LIGHT) {
        /* En F3DEX2 TODAS las luces comparten el índice 10; lo que
         * distingue una de otra es el OFFSET:
         *     gSPLight(l, LIGHT_n) → offset = n * 24 + 24
         * y como G_MVO_L0 = 48, la luz sale de (offset - 48) / 24.
         *
         * Los offsets 0 y 24 son lookat X e Y, no luces. */
        if (offset >= G_MVO_L0) {
            u32 li = (offset - G_MVO_L0) / 24;
            if (li < 8) {
                ps2_light_load(li, src, rsp);
            }
        } else {
            u32 axis = offset / 24;
            if (axis < 2) {
                float x = (s8)src[8], y = (s8)src[9], z = (s8)src[10];
                float length = sqrtf(x*x + y*y + z*z);
                rsp->lookAt[axis][0] = length > 0.0f ? x / length : 0.0f;
                rsp->lookAt[axis][1] = length > 0.0f ? y / length : 0.0f;
                rsp->lookAt[axis][2] = length > 0.0f ? z / length : 0.0f;
            }
        }
    }
}
