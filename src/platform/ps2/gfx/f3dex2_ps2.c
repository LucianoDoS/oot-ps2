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

#include "ps2_types.h"
#include "ps2_segment.h"
#include "rdp_to_gs.h"
#include "f3dex2_ps2.h"
#include "ps2_texture.h"

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

    /* División perspectiva + viewport.
     * El viewport llega en formato 2.10 fijo: scale y trans van
     * multiplicados por 4 (de ahí el /4 al guardarlos). */
    project_vertex(v, rsp);
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

        transform_vertex(v, rsp);
        generate_texture_coordinates(v, rsp);
        ps2_light_apply_vertex(v, rsp);

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
static void submit_vertices(RSPVertex* v0, RSPVertex* v1, RSPVertex* v2,
                            RSPState* rsp, RDPState* rdp) {
    GSGLOBAL* gs = rdp->gs;
    if (!gs) return;

    /* ── Culling ──────────────────────────────────────────────────
     * Antes sólo se miraba G_CULL_BACK y con el signo fijo. El signo
     * correcto depende de si el viewport invierte Y (que es el caso:
     * vpScaleY es negativo), así que se calcula el área con signo y se
     * comparan las dos banderas por separado. */
    float ex = v1->sx - v0->sx, ey = v1->sy - v0->sy;
    float fx = v2->sx - v0->sx, fy = v2->sy - v0->sy;
    float area = ex * fy - ey * fx;

    u32 cull = rsp->geometryMode & G_CULL_BOTH;
    if (cull == G_CULL_BOTH) { rdp->stats.culled++; return; }
    /* N64 front faces are CCW before the viewport's Y inversion. */
    if ((cull == G_CULL_BACK && area >= 0.0f) ||
        (cull == G_CULL_FRONT && area <= 0.0f)) { rdp->stats.culled++; return; }

    /* ── Color ────────────────────────────────────────────────────── */
    u64 c0, c1, c2;
    u64* colors[3] = {&c0, &c1, &c2};
    RSPVertex* vertices[3] = {v0, v1, v2};
    for (int i = 0; i < 3; i++) {
        RSPVertex* v = vertices[i];
        u8 shade[4] = {v->cr, v->cg, v->cb, v->ca}, color[4];
        if (rsp->texEnabled) {
            bool rgbShade = ps2_combiner_uses_shade(rdp, false);
            bool alphaShade = ps2_combiner_uses_shade(rdp, true);
            color[0] = rgbShade ? (shade[0] * 128u / 255) : 128;
            color[1] = rgbShade ? (shade[1] * 128u / 255) : 128;
            color[2] = rgbShade ? (shade[2] * 128u / 255) : 128;
            color[3] = alphaShade ? (shade[3] * 128u / 255) : 128;
        } else {
            const u8 texel[4] = {255,255,255,255};
            ps2_combiner_color(rdp, texel, shade, color);
            color[3] = color[3] * 128u / 255;
        }
        *colors[i] = GS_SETREG_RGBAQ(color[0], color[1], color[2], color[3], 1.0f);
    }

    /* ── Profundidad ──────────────────────────────────────────────
     * El GS usa 0 = lejos y 0xFFFFFF = cerca (al revés que N64), por
     * eso se invierte. Antes se mandaba sin invertir, con lo que el
     * z-test habría descartado justo lo que tocaba dibujar. */
    u32 z0 = (u32)((1.0f - v0->sz) * 16777215.0f);
    u32 z1 = (u32)((1.0f - v1->sz) * 16777215.0f);
    u32 z2 = (u32)((1.0f - v2->sz) * 16777215.0f);

    if (rsp->texEnabled) {
        TileDescriptor* tile = &rdp->tile[rsp->texTile & 7];
        if (!tile->gsTextureLoaded) {
            ps2_texture_upload(tile, rdp);
        }
        if (!tile->gsTextureLoaded) { rdp->stats.textureFailed++; return; }

        GSPRIMSTQPOINT points[3];
        ps2_texture_apply_tile(tile, rdp);
        ps2_texture_build_vertex(tile, rsp, rdp, v0, (u32)c0, z0, &points[0]);
        ps2_texture_build_vertex(tile, rsp, rdp, v1, (u32)c1, z1, &points[1]);
        ps2_texture_build_vertex(tile, rsp, rdp, v2, (u32)c2, z2, &points[2]);
        gsKit_prim_list_triangle_goraud_texture_stq_3d(gs, &tile->gsTexture, 3, points);
    } else {
        gsKit_prim_triangle_gouraud_3d(
            gs,
            v0->sx, v0->sy, z0,
            v1->sx, v1->sy, z1,
            v2->sx, v2->sy, z2, c0, c1, c2);
    }
    rdp->stats.submitted++;
}

static float clip_distance(const RSPVertex* v, int plane) {
    switch (plane) {
        case 0: return v->cw + v->cx;
        case 1: return v->cw - v->cx;
        case 2: return v->cw + v->cy;
        case 3: return v->cw - v->cy;
        case 4: return v->cw + v->cz;
        default: return v->cw - v->cz;
    }
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
        int nextCount = 0;
        for (int i = 0; i < count; i++) {
            const RSPVertex* first = &src[i];
            const RSPVertex* last = &src[(i + 1) % count];
            float d0 = clip_distance(first, plane), d1 = clip_distance(last, plane);
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
        submit_vertices(a, b, c, rsp, rdp); return;
    }
    RSPVertex polygon[12];
    int count = ps2_clip_triangle(a, b, c, polygon);
    if (count < 3) { rdp->stats.clipped++; return; }
    for (int i = 0; i < count; i++) project_vertex(&polygon[i], rsp);
    for (int i = 1; i + 1 < count; i++)
        submit_vertices(&polygon[0], &polygon[i], &polygon[i + 1], rsp, rdp);
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
    int i0 = (int)((w0 >> 17) & 0x7F);
    int i1 = (int)((w0 >>  9) & 0x7F);
    int i2 = (int)((w0 >>  1) & 0x7F);
    int i3 = (int)((w1 >> 17) & 0x7F);
    int i4 = (int)((w1 >>  9) & 0x7F);
    int i5 = (int)((w1 >>  1) & 0x7F);
    submit_triangle(i0, i1, i2, rsp, rdp);
    submit_triangle(i3, i4, i5, rsp, rdp);
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
    (void)rdp;
    rsp->texEnabled = (((w0 >> 1) & 0x7F) != 0);
    rsp->texTile    = (u8)((w0 >> 8) & 0x7);

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
        float outputX = g_rdpState.gs ? g_rdpState.gs->Width / 320.0f : 1.0f;
        float outputY = g_rdpState.gs ? g_rdpState.gs->Height / 240.0f : 1.0f;
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
