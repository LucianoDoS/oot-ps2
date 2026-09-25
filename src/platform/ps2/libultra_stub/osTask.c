/**
 * osTask.c — Tareas del RSP (gráficos y audio) sobre el EE
 *
 * ── QUÉ ESTABA MAL ────────────────────────────────────────────────────
 *
 * osSpTaskStart() interpretaba la display list y volvía sin avisar a
 * nadie. El comentario del propio archivo lo admitía:
 *
 *     "En un motor final, enviaríamos un mensaje a la cola del
 *      scheduler para avisarle de que la tarea grafica ha terminado"
 *
 * Eso no era un detalle pendiente, era un deadlock: Graph_ThreadEntry
 * manda la tarea y se bloquea en osRecvMesg esperando RSP_DONE/RDP_DONE.
 * Sin el aviso, el juego se congela en el primer frame.
 *
 * ── CÓMO SE ARREGLA ───────────────────────────────────────────────────
 *
 * Al terminar de ejecutar la lista disparamos los eventos OS_EVENT_SP y
 * OS_EVENT_DP (ver osEvent.c), que es justo lo que haría el hardware.
 *
 * También se corrige el struct OSTask. El anterior era inventado y
 * además estaba desplazado. El real (ultra64/sptask.h) es una UNION que
 * contiene un OSTask_t de 0x40 bytes, con data_ptr en el offset 0x30.
 *
 * Y ojo: en libultra osSpTaskStart NO es una función, es una macro que
 * expande a osSpTaskLoad() + osSpTaskStartGo(). Por eso aquí sólo se
 * definen esas dos; definir un osSpTaskStart() propio no lo llamaría
 * nadie.
 */

#include <string.h>
#include <stdio.h>

#include "ps2_types.h"
#include "ps2_os_internal.h"
#include "rdp_to_gs.h"

/* ───────────────────────────────────────────────────────────────── *
 *  OSTask — layout real de libultra
 *  (ultra64/sptask.h: OSTask = { u32 type; u32 flags; OSTask_t t; })
 * ───────────────────────────────────────────────────────────────── */
typedef struct OSTask_t {
    /* 0x00 */ u32   type;
    /* 0x04 */ u32   flags;
    /* 0x08 */ u64*  ucode_boot;
    /* 0x0C */ u32   ucode_boot_size;
    /* 0x10 */ u64*  ucode;
    /* 0x14 */ u32   ucode_size;
    /* 0x18 */ u64*  ucode_data;
    /* 0x1C */ u32   ucode_data_size;
    /* 0x20 */ u64*  dram_stack;
    /* 0x24 */ u32   dram_stack_size;
    /* 0x28 */ u64*  output_buff;
    /* 0x2C */ u64*  output_buff_size;
    /* 0x30 */ u64*  data_ptr;        /* Gfx* o lista de audio */
    /* 0x34 */ u32   data_size;
    /* 0x38 */ u64*  yield_data_ptr;
    /* 0x3C */ u32   yield_data_size;
} OSTask_t;   /* size = 0x40 */

_Static_assert(sizeof(OSTask_t) == 0x40, "OSTask_t debe medir 0x40");

/* OSTask es una UNION, no un struct con cabecera: t empieza en 0x00 */
typedef union OSTask {
    OSTask_t  t;
    long long force_structure_alignment;
} OSTask;

/* Tipos de tarea (ultra64/sptask.h) */
#define M_GFXTASK   1
#define M_AUDTASK   2

static OSTask* s_currentTask = NULL;

/* Implementado en ps2_audio_driver.c */
extern void PS2Audio_ProcessTask(void* cmdList, u32 size);

/* ───────────────────────────────────────────────────────────────── *
 *  osSpTaskLoad
 * ───────────────────────────────────────────────────────────────── */
void osSpTaskLoad(OSTask* task) {
    s_currentTask = task;
}

/* ───────────────────────────────────────────────────────────────── *
 *  osSpTaskStartGo / osSpTaskStart
 * ───────────────────────────────────────────────────────────────── */
void osSpTaskStartGo(OSTask* task) {
    if (task) s_currentTask = task;
    if (!s_currentTask) return;

    OSTask* t = s_currentTask;
    u32 type = t->t.type;      /* el tipo real vive en t.type */

    if (type == M_GFXTASK) {
        rdp_to_gs_exec((const Gfx*)t->t.data_ptr);
        rdp_to_gs_flip();

        /* El RSP terminó de procesar la lista... */
        ps2_os_fire_event(PS2_OS_EVENT_SP);
        /* ...y el RDP terminó de rasterizar.
         * En PS2 ambas cosas ocurren dentro de rdp_to_gs_exec, así que
         * los dos eventos se disparan seguidos. El scheduler de OoT
         * espera exactamente estos dos mensajes. */
        ps2_os_fire_event(PS2_OS_EVENT_DP);

    } else if (type == M_AUDTASK) {
        PS2Audio_ProcessTask(t->t.data_ptr, t->t.data_size);
        ps2_os_fire_event(PS2_OS_EVENT_SP);
    } else {
        /* Tarea desconocida: avisamos igual para no bloquear a nadie */
        ps2_os_fire_event(PS2_OS_EVENT_SP);
    }

    s_currentTask = NULL;
}

/* ───────────────────────────────────────────────────────────────── *
 *  osSpTaskYield / osSpTaskYielded
 *
 *  En PS2 las tareas son síncronas: para cuando alguien podría pedir un
 *  yield, la tarea ya terminó. Devolvemos 0 = "no se quedó a medias".
 * ───────────────────────────────────────────────────────────────── */
void osSpTaskYield(void) {
}

s32 osSpTaskYielded(OSTask* task) {
    (void)task;
    return 0;
}
