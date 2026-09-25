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

static void mat4_identity(float* m) {
    memset(m, 0, sizeof(float) * 16);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
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
    mat4_mul(rsp->mvp, rsp->projection, rsp->modelview);
}

/* ───────────────────────────────────────────────────────────────── *
 *  Transformación de vértice
 * ───────────────────────────────────────────────────────────────── */
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
    if (v->cw > 0.0001f || v->cw < -0.0001f) {
        float invW = 1.0f / v->cw;

        v->sx = (v->cx * invW) * rsp->vpScaleX + rsp->vpTransX;
        v->sy = (v->cy * invW) * rsp->vpScaleY + rsp->vpTransY;
        v->sz =  v->cz * invW * 0.5f + 0.5f;    /* a rango 0..1 */

        if (v->sz < 0.0f) v->sz = 0.0f;
        if (v->sz > 1.0f) v->sz = 1.0f;
    } else {
        /* w degenerado: marcamos el vértice como recortado */
        v->clipFlags = 0xFF;
        v->sx = v->sy = 0.0f;
        v->sz = 1.0f;
    }
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
        ps2_light_apply_vertex(v, rsp);

        src += 16;
    }
}

/* ───────────────────────────────────────────────────────────────── *
 *  Envío de un triángulo al GS
 * ───────────────────────────────────────────────────────────────── */
static void submit_triangle(int i0, int i1, int i2,
                            RSPState* rsp, RDPState* rdp) {
    if (i0 >= RSP_VTX_CACHE_SIZE || i1 >= RSP_VTX_CACHE_SIZE ||
        i2 >= RSP_VTX_CACHE_SIZE) {
        return;
    }

    RSPVertex* v0 = &rsp->vtx[i0];
    RSPVertex* v1 = &rsp->vtx[i1];
    RSPVertex* v2 = &rsp->vtx[i2];

    if (!v0->valid || !v1->valid || !v2->valid) return;

    /* Rechazo trivial: los tres fuera del mismo plano */
    if ((v0->clipFlags & v1->clipFlags & v2->clipFlags) != 0) return;

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
    if (cull == G_CULL_BOTH) return;               /* nada que dibujar */
    if (cull == G_CULL_BACK  && area < 0.0f) return;
    if (cull == G_CULL_FRONT && area > 0.0f) return;

    /* ── Color ────────────────────────────────────────────────────── */
    u64 c0, c1, c2;
    if (rsp->geometryMode & G_SHADE) {
        c0 = GS_SETREG_RGBAQ(v0->cr, v0->cg, v0->cb, v0->ca >> 1, 1.0f);
        c1 = GS_SETREG_RGBAQ(v1->cr, v1->cg, v1->cb, v1->ca >> 1, 1.0f);
        c2 = GS_SETREG_RGBAQ(v2->cr, v2->cg, v2->cb, v2->ca >> 1, 1.0f);
    } else {
        /* Sin sombreado: color primitivo constante.
         * Nota: el GS usa alfa 0..128, no 0..255, de ahí el >>1. */
        u32 pc = rdp->primColor;
        u8 pr = (pc >> 24) & 0xFF, pg = (pc >> 16) & 0xFF;
        u8 pb = (pc >>  8) & 0xFF, pa = (pc      ) & 0xFF;
        c0 = c1 = c2 = GS_SETREG_RGBAQ(pr, pg, pb, pa >> 1, 1.0f);
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
        if (!tile->gsTextureLoaded) return;   /* la subida falló */

        float tw = (float)(tile->width  ? tile->width  : 1);
        float th = (float)(tile->height ? tile->height : 1);

        /* Coordenadas S/T en 10.5 fijo → texel, aplicando la escala
         * de G_TEXTURE. */
        float su = rsp->texScaleS / 32.0f;
        float sv = rsp->texScaleT / 32.0f;

        float u0 = (float)v0->tc_s * su, t0 = (float)v0->tc_t * sv;
        float u1 = (float)v1->tc_s * su, t1 = (float)v1->tc_t * sv;
        float u2 = (float)v2->tc_s * su, t2 = (float)v2->tc_t * sv;

        /* El origen del tile desplaza las coordenadas */
        float offU = (float)(tile->uls >> 2);
        float offV = (float)(tile->ult >> 2);
        u0 -= offU; u1 -= offU; u2 -= offU;
        t0 -= offV; t1 -= offV; t2 -= offV;

        (void)tw; (void)th;

        gsKit_prim_triangle_goraud_texture_3d(
            gs, &tile->gsTexture,
            v0->sx, v0->sy, z0, u0, t0, c0,
            v1->sx, v1->sy, z1, u1, t1, c1,
            v2->sx, v2->sy, z2, u2, t2, c2);
    } else {
        gsKit_prim_triangle_goraud_3d(
            gs,
            v0->sx, v0->sy, z0, c0,
            v1->sx, v1->sy, z1, c1,
            v2->sx, v2->sy, z2, c2);
    }
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
            mat4_mul(rsp->projection, rsp->projection, m);
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
            mat4_mul(rsp->modelview, rsp->modelview, m);
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
                    gSegments[seg] = (uintptr_t)ps2_segmented_to_virtual(w1);
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
        rsp->vpScaleX = sx;
        rsp->vpScaleY = sy;
        rsp->vpTransX = tx;
        rsp->vpTransY = ty;

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
            /* lookat: sólo hace falta para texture-gen (reflejos).
             * Pendiente. */
        }
    }
}
