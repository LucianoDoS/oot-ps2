/**
 * osMessage.c — Colas de mensajes de libultra sobre semáforos del EE
 *
 * ── QUÉ ESTABA MAL ────────────────────────────────────────────────────
 *
 * El stub anterior redefinía OSMesgQueue añadiéndole 5 campos (dos
 * semáforos, un mutex, head y tail). El struct real mide 0x18 = 24
 * bytes; el inflado rondaba los 44. Y luego hacía:
 *
 *     memset(mq, 0, sizeof(OSMesgQueue));   // 44 bytes
 *
 * sobre objetos que el juego declaró de 24. Cada creación de cola
 * arrasaba ~20 bytes de la variable siguiente. Como OoT crea colas por
 * todas partes (scheduler, padmgr, audio, dmamgr, irqmgr...), el
 * resultado eran fallos aleatorios imposibles de depurar.
 *
 * Además ignoraba el campo `first`, que es el que usan las macros del
 * propio juego (MQ_IS_EMPTY / MQ_GET_COUNT / MQ_IS_FULL).
 *
 * ── CÓMO SE ARREGLA ───────────────────────────────────────────────────
 *
 * - OSMesgQueue queda EXACTAMENTE con su layout de N64.
 * - Los semáforos viven en una tabla lateral indexada por puntero.
 * - Se respeta la semántica original: `first` es el índice del primer
 *   mensaje válido y `validCount` cuántos hay. Así las macros del juego
 *   siguen funcionando.
 */

#include <kernel.h>
#include <string.h>
#include <stdio.h>

#include "ps2_os_internal.h"
#include "ps2_queue.h"

typedef void* OSMesg;
struct OSThread;

/* ───────────────────────────────────────────────────────────────── *
 *  OSMesgQueue — layout EXACTO de libultra (include/ultra64/message.h)
 *  size = 0x18. Ni un byte más.
 * ───────────────────────────────────────────────────────────────── */
typedef struct OSMesgQueue {
    /* 0x00 */ struct OSThread* mtqueue;
    /* 0x04 */ struct OSThread* fullqueue;
    /* 0x08 */ s32     validCount;
    /* 0x0C */ s32     first;
    /* 0x10 */ s32     msgCount;
    /* 0x14 */ OSMesg* msg;
} OSMesgQueue;

_Static_assert(sizeof(OSMesgQueue) == 0x18,
               "OSMesgQueue debe medir 0x18 bytes como en N64");

/* Flags de libultra */
#define OS_MESG_NOBLOCK  0
#define OS_MESG_BLOCK    1

/* ───────────────────────────────────────────────────────────────── *
 *  Tabla lateral de semáforos
 * ───────────────────────────────────────────────────────────────── */
typedef struct {
    OSMesgQueue* key;        /* NULL = libre                          */
    int sem_space;           /* huecos libres para enviar             */
    int sem_items;           /* mensajes disponibles para recibir     */
    int sem_mutex;           /* exclusión mutua sobre el anillo       */
} PS2QueueEntry;

static PS2QueueEntry s_queues[PS2_OS_MAX_QUEUES];

static PS2QueueEntry* queue_find(OSMesgQueue* mq) {
    for (int i = 0; i < PS2_OS_MAX_QUEUES; i++) {
        if (s_queues[i].key == mq) return &s_queues[i];
    }
    return NULL;
}

static int make_sema(int init, int max) {
    ee_sema_t s;
    memset(&s, 0, sizeof(s));
    s.init_count = init;
    s.max_count  = max;
    s.option     = 0;
    return CreateSema(&s);
}

static void queue_release(PS2QueueEntry* e) {
    if (e->sem_space >= 0) DeleteSema(e->sem_space);
    if (e->sem_items >= 0) DeleteSema(e->sem_items);
    if (e->sem_mutex >= 0) DeleteSema(e->sem_mutex);
    e->sem_space = e->sem_items = e->sem_mutex = -1;
    e->key = NULL;
}

void ps2_os_release_queues(const void* start, unsigned int size) {
    uintptr_t base = (uintptr_t)start;
    if (!start || !size) return;
    int lock = ps2_os_lock();
    for (int i = 0; i < PS2_OS_MAX_QUEUES; i++) {
        uintptr_t address = (uintptr_t)s_queues[i].key;
        if (s_queues[i].key && address >= base && address - base < size)
            queue_release(&s_queues[i]);
    }
    ps2_os_unlock(lock);
}

static PS2QueueEntry* queue_alloc(OSMesgQueue* mq, int count) {
    /* Si la cola ya existía (el juego reutiliza estructuras), la
     * reciclamos en lugar de filtrar semáforos. */
    PS2QueueEntry* e = queue_find(mq);
    if (e) {
        queue_release(e);
    } else {
        for (int i = 0; i < PS2_OS_MAX_QUEUES; i++) {
            if (s_queues[i].key == NULL) { e = &s_queues[i]; break; }
        }
    }
    if (!e) return NULL;

    e->key       = mq;
    e->sem_space = make_sema(count, count);
    e->sem_items = make_sema(0, count);
    e->sem_mutex = make_sema(1, 1);

    if (e->sem_space < 0 || e->sem_items < 0 || e->sem_mutex < 0) {
        printf("[osMesg] ERROR: no se pudieron crear semaforos\n");
        queue_release(e);
        return NULL;
    }
    return e;
}

/* ───────────────────────────────────────────────────────────────── *
 *  osCreateMesgQueue
 * ───────────────────────────────────────────────────────────────── */
void osCreateMesgQueue(OSMesgQueue* mq, OSMesg* msg, s32 count) {
    if (!mq || !msg || count <= 0) return;

    /* Ahora sí: el memset cubre exactamente los 24 bytes del struct. */
    mq->mtqueue    = NULL;
    mq->fullqueue  = NULL;
    mq->validCount = 0;
    mq->first      = 0;
    mq->msgCount   = count;
    mq->msg        = msg;

    int lock = ps2_os_lock();
    PS2QueueEntry* e = queue_alloc(mq, count);
    ps2_os_unlock(lock);

    if (!e) {
        printf("[osMesg] ERROR: tabla de colas llena (sube PS2_OS_MAX_QUEUES)\n");
    }
}

/* ───────────────────────────────────────────────────────────────── *
 *  osSendMesg — añade al final de la cola
 *  Devuelve 0 si se envió, -1 si estaba llena en modo NOBLOCK.
 * ───────────────────────────────────────────────────────────────── */
s32 osSendMesg(OSMesgQueue* mq, OSMesg msg, s32 flags) {
    if (!mq) return -1;
    PS2QueueEntry* e = queue_find(mq);
    if (!e) return -1;

    if (flags == OS_MESG_NOBLOCK) {
        if (PollSema(e->sem_space) < 0) return -1;
    } else {
        WaitSema(e->sem_space);
    }

    WaitSema(e->sem_mutex);
    {
        s32 idx = (mq->first + mq->validCount) % mq->msgCount;
        mq->msg[idx] = msg;
        mq->validCount++;
    }
    SignalSema(e->sem_mutex);

    SignalSema(e->sem_items);
    return 0;
}

/* Native rendering runs on the scheduler's EE thread. Keep one pending
 * retrace rather than filling its queue while it draws a slow frame; SP/DP
 * completion messages must still fit when that synchronous call returns. */
s32 ps2_os_send_coalesced(OSMesgQueue* mq, OSMesg msg) {
    if (!mq) return -1;
    PS2QueueEntry* e = queue_find(mq);
    if (!e) return -1;
    WaitSema(e->sem_mutex);
    for (s32 i = 0; i < mq->validCount; i++) {
        if (mq->msg[(mq->first + i) % mq->msgCount] == msg) {
            SignalSema(e->sem_mutex);
            return 0;
        }
    }
    if (PollSema(e->sem_space) < 0) {
        SignalSema(e->sem_mutex);
        return -1;
    }
    mq->msg[(mq->first + mq->validCount) % mq->msgCount] = msg;
    mq->validCount++;
    SignalSema(e->sem_mutex);
    SignalSema(e->sem_items);
    return 0;
}

/* ───────────────────────────────────────────────────────────────── *
 *  osJamMesg — inserta al PRINCIPIO (mensaje prioritario)
 *  OoT lo usa para reset / fault.
 * ───────────────────────────────────────────────────────────────── */
s32 osJamMesg(OSMesgQueue* mq, OSMesg msg, s32 flags) {
    if (!mq) return -1;
    PS2QueueEntry* e = queue_find(mq);
    if (!e) return -1;

    if (flags == OS_MESG_NOBLOCK) {
        if (PollSema(e->sem_space) < 0) return -1;
    } else {
        WaitSema(e->sem_space);
    }

    WaitSema(e->sem_mutex);
    {
        mq->first = (mq->first + mq->msgCount - 1) % mq->msgCount;
        mq->msg[mq->first] = msg;
        mq->validCount++;
    }
    SignalSema(e->sem_mutex);

    SignalSema(e->sem_items);
    return 0;
}

/* ───────────────────────────────────────────────────────────────── *
 *  osRecvMesg
 * ───────────────────────────────────────────────────────────────── */
s32 osRecvMesg(OSMesgQueue* mq, OSMesg* msg, s32 flags) {
    if (!mq) return -1;
    PS2QueueEntry* e = queue_find(mq);
    if (!e) return -1;

    if (flags == OS_MESG_NOBLOCK) {
        if (PollSema(e->sem_items) < 0) return -1;
    } else {
        WaitSema(e->sem_items);
    }

    WaitSema(e->sem_mutex);
    {
        if (msg) *msg = mq->msg[mq->first];
        mq->first = (mq->first + 1) % mq->msgCount;
        mq->validCount--;
    }
    SignalSema(e->sem_mutex);

    SignalSema(e->sem_space);
    return 0;
}

/* ───────────────────────────────────────────────────────────────── *
 *  ps2_mesgqueue_init()
 * ───────────────────────────────────────────────────────────────── */
void ps2_mesgqueue_init(void) {
    for (int i = 0; i < PS2_OS_MAX_QUEUES; i++) {
        s_queues[i].key       = NULL;
        s_queues[i].sem_space = -1;
        s_queues[i].sem_items = -1;
        s_queues[i].sem_mutex = -1;
    }
    printf("[osMesg] Tabla de colas lista (%d entradas)\n", PS2_OS_MAX_QUEUES);
}
