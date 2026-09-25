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
#include "ps2_types.h"
#include "rdp_to_gs.h"

void ps2_zbuffer_update_test(RDPState* rdp) {
    /* Punto único de escritura del registro TEST */
    rdp_apply_othermode(rdp);
}

/**
 * ps2_zbuffer_set_writes() — activa o desactiva la escritura en Z
 *
 * El bit Z_UPD del other mode. En el GS se controla con ZMSK dentro del
 * registro FRAME, no con TEST, así que va aparte.
 */
void ps2_zbuffer_set_writes(RDPState* rdp, bool enable) {
    if (!rdp || !rdp->gs) return;
    /* gsKit gestiona FRAME internamente; lo dejamos anotado para que
     * el renderer lo consulte al montar el paquete de dibujo. */
    rdp->zWriteEnabled = enable;
}
