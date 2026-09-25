/**
 * osEvent.c — osSetEventMesg y despacho de eventos (NUEVO)
 *
 * ── POR QUÉ HACÍA FALTA ───────────────────────────────────────────────
 *
 * Este archivo no existía, y su ausencia era la causa del deadlock del
 * primer frame.
 *
 * En N64, cuando el RSP o el RDP terminan su trabajo levantan una
 * interrupción, y libultra convierte esa interrupción en un mensaje que
 * manda a una cola registrada previamente con osSetEventMesg().
 *
 * El scheduler de OoT depende de eso literalmente (src/code/sched.c):
 *
 *     osSetEventMesg(OS_EVENT_SP, &sc->interruptQueue, RSP_DONE_MSG);
 *     osSetEventMesg(OS_EVENT_DP, &sc->interruptQueue, RDP_DONE_MSG);
 *
 * Manda la tarea gráfica y se bloquea en osRecvMesg esperando esos
 * mensajes. El osSpTaskStart anterior ejecutaba la display list y no
 * avisaba a nadie, así que el hilo gráfico se quedaba dormido para
 * siempre en el frame 1.
 *
 * Aquí registramos las colas, y osTask.c dispara los eventos SP y DP en
 * cuanto termina de interpretar la lista. Como en PS2 el trabajo es
 * síncrono (lo hace el EE), "la tarea ha terminado" es simplemente
 * "hemos vuelto de rdp_to_gs_exec".
 */

#include <kernel.h>
#include <string.h>
#include <stdio.h>

#include "ps2_os_internal.h"

typedef void* OSMesg;
struct OSMesgQueue;

/* Declarado en osMessage.c */
extern s32 osSendMesg(struct OSMesgQueue* mq, OSMesg msg, s32 flags);

#define OS_MESG_NOBLOCK  0

/* ───────────────────────────────────────────────────────────────── *
 *  Tabla de eventos
 * ───────────────────────────────────────────────────────────────── */
typedef struct {
    struct OSMesgQueue* mq;
    OSMesg              msg;
} PS2EventSlot;

static PS2EventSlot s_events[PS2_OS_NUM_EVENTS];

/* ───────────────────────────────────────────────────────────────── *
 *  osSetEventMesg — registra a quién avisar cuando ocurra un evento
 * ───────────────────────────────────────────────────────────────── */
void osSetEventMesg(s32 event, struct OSMesgQueue* mq, OSMesg msg) {
    if (event < 0 || event >= PS2_OS_NUM_EVENTS) return;

    int lock = ps2_os_lock();
    s_events[event].mq  = mq;
    s_events[event].msg = msg;
    ps2_os_unlock(lock);
}

/* ───────────────────────────────────────────────────────────────── *
 *  ps2_os_fire_event — dispara el evento
 *
 *  Siempre NOBLOCK: esto imita una interrupción, y una interrupción
 *  jamás puede quedarse bloqueada esperando hueco en una cola. Si la
 *  cola está llena, el mensaje se pierde, que es exactamente lo que
 *  pasa en el hardware real.
 * ───────────────────────────────────────────────────────────────── */
void ps2_os_fire_event(int event) {
    if (event < 0 || event >= PS2_OS_NUM_EVENTS) return;

    struct OSMesgQueue* mq = s_events[event].mq;
    if (!mq) return;   /* nadie escucha este evento */

    osSendMesg(mq, s_events[event].msg, OS_MESG_NOBLOCK);
}

/* ───────────────────────────────────────────────────────────────── *
 *  ps2_event_init()
 * ───────────────────────────────────────────────────────────────── */
void ps2_event_init(void) {
    memset(s_events, 0, sizeof(s_events));
    printf("[osEvent] Tabla de eventos lista\n");
}
