/**
 * ps2_zbuffer.c — Profundidad
 *
 * ── QUÉ ESTABA MAL ────────────────────────────────────────────────────
 *
 * Este archivo y rdp_color.c escribían los DOS en el registro TEST del
 * GS, cada uno por su cuenta y con valores distintos. Como TEST es un
 * único registro que contiene a la vez el test de alfa Y el de
 * profundidad, el último en escribir borraba la configuración del otro.
 * El resultado dependía del orden de los comandos en la display list,
 * que cambia en cada frame: fallos intermitentes imposibles de atar.
 *
 * Además mezclaba dos APIs incompatibles de gsKit en la misma función
 * (GS_SET_TEST con un valor crudo y gsKit_set_test con una constante).
 *
 * ── CÓMO SE ARREGLA ───────────────────────────────────────────────────
 *
 * Ahora hay UN SOLO punto que escribe TEST: rdp_apply_othermode() en
 * rdp_color.c, que compone los dos tests juntos. Esta función se
 * mantiene por compatibilidad y delega ahí.
 *
 * ── SOBRE EL SENTIDO DE LA PROFUNDIDAD ────────────────────────────────
 *
 * Importante y fácil de equivocar: en N64, Z crece hacia el fondo
 * (0 = cerca). En el GS de PS2 es al revés: el valor ALTO está cerca.
 * Por eso f3dex2_ps2.c invierte la Z al enviar cada vértice
 * ((1 - sz) * 0xFFFFFF) y aquí el test es GEQUAL. Antes se mandaba la Z
 * sin invertir y se comparaba con GEQUAL, o sea que el z-test
 * descartaba justo la geometría que debía verse.
 */

#include <gsKit.h>
#include <gsInline.h>
#include <math.h>
#include "ps2_types.h"
#include "rdp_to_gs.h"

/* Use the initialized depth format, not the requested one: gsKit may
 * change it. Sending 24-bit values to Z16 truncates them and wraps Z. */
u32 ps2_zbuffer_value(const RDPState* rdp, float normalizedDepth) {
    u32 maximum = (rdp->gs->PSMZ == GS_PSMZ_16 || rdp->gs->PSMZ == GS_PSMZ_16S) ?
        0xffffu : 0xffffffu;
    if (normalizedDepth <= 0.0f) return maximum;
    if (!(normalizedDepth < 1.0f)) return 0;
    return (u32)((1.0f - normalizedDepth) * maximum);
}

void ps2_zbuffer_decal_bias(const RDPState* rdp, const RSPVertex* a,
    const RSPVertex* b, const RSPVertex* c, u32 depth[3]) {
    /* N64 decal comparison accepts a delta-Z interval around its receiver.
     * GS GEQUAL has no interval. Approximate it with slope-scaled bias only
     * on depth-tested decals without depth writes, retaining occlusion. */
    if ((rdp->otherMode.lo & 0xc30) != 0xc10) return;
    float dx1 = b->sx - a->sx, dy1 = b->sy - a->sy;
    float dx2 = c->sx - a->sx, dy2 = c->sy - a->sy;
    float area = dx1 * dy2 - dy1 * dx2;
    if (fabsf(area) < 0.0001f) return;
    float dz1 = (float)depth[1] - depth[0], dz2 = (float)depth[2] - depth[0];
    float slope = fabsf((dz1 * dy2 - dz2 * dy1) / area) +
                  fabsf((dx1 * dz2 - dx2 * dz1) / area);
    u32 maximum = (rdp->gs->PSMZ == GS_PSMZ_16 || rdp->gs->PSMZ == GS_PSMZ_16S) ? 0xffffu : 0xffffffu;
    float offset = ceilf(slope * 2.0f) + 2.0f;
    u32 limit = maximum / 4096;
    u32 bias = offset < limit ? (u32)offset : limit;
    for (int i = 0; i < 3; i++)
        depth[i] = depth[i] > maximum - bias ? maximum : depth[i] + bias;
}

void ps2_zbuffer_update_test(RDPState* rdp) {
    /* Punto único de escritura del registro TEST */
    rdp_apply_othermode(rdp);
}

/**
 * ps2_zbuffer_set_writes() — activa o desactiva la escritura en Z
 *
 * El bit Z_UPD del other mode se traduce a ZMSK en el registro ZBUF.
 */
void ps2_zbuffer_set_writes(RDPState* rdp, bool enable) {
    if (!rdp || !rdp->gs) return;
    rdp->zWriteEnabled = enable;
    GSGLOBAL* gs = rdp->gs;
    u64* packet = gsKit_heap_alloc(gs, 1, 16, GIF_AD);
    packet[0] = GIF_TAG_AD(1);
    packet[1] = GIF_AD;
    packet[2] = GS_SETREG_ZBUF(gs->ZBuffer / 8192, gs->PSMZ & 0xF, !enable);
    packet[3] = GS_ZBUF_1 + gs->PrimContext;
}
