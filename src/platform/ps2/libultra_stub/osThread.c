/**
 * osThread.c — Stubs de hilos de libultra sobre el kernel del EE
 *
 * ── QUÉ ESTABA MAL EN LA VERSIÓN ANTERIOR ─────────────────────────────
 *
 * 1. Redefinía OSThread con campos extra (ee_thread_id, ee_thread). El
 *    juego declara los OSThread con el tamaño real de N64 (0x1B0), así
 *    que escribir campos "extra" pisaba memoria ajena.
 *
 * 2. Pasaba a CreateThread() el puntero de pila tal cual. libultra
 *    entrega el TOPE de la pila (dirección alta, la pila de N64 crece
 *    hacia abajo); el kernel del EE espera la BASE (dirección baja) más
 *    el tamaño. El SP inicial quedaba fuera del buffer real.
 *
 * 3. stack_size estaba fijo a 4096 ignorando el tamaño real declarado
 *    por el juego (sGraphStack son 0x1800, sSchedStack 0x600, etc.).
 *
 * ── CÓMO SE ARREGLA ───────────────────────────────────────────────────
 *
 * - OSThread se deja intacto; lo de PS2 va en una tabla lateral.
 * - Ignoramos por completo la pila que nos pasa el juego y reservamos
 *   la nuestra desde un pool estático. Así el problema tope/base/tamaño
 *   desaparece: nosotros controlamos las tres cosas.
 *
 *   Efecto secundario a tener en cuenta: StackCheck_Check() del juego
 *   seguirá inspeccionando SU buffer, que ahora nunca se usa, y dirá
 *   siempre "0 bytes usados". Es inofensivo (sólo DEBUG_FEATURES) y es
 *   un precio barato a cambio de pilas correctas.
 */

#include <kernel.h>
#include <string.h>
#include <stdio.h>

#include "ps2_os_internal.h"

/* ───────────────────────────────────────────────────────────────── *
 *  Estados de hilo de libultra
 * ───────────────────────────────────────────────────────────────── */
#define OS_STATE_STOPPED    1
#define OS_STATE_RUNNABLE   2
#define OS_STATE_RUNNING    4
#define OS_STATE_WAITING    8

/* ───────────────────────────────────────────────────────────────── *
 *  Vista mínima de OSThread.
 *
 *  Sólo declaramos los campos que realmente tocamos, con sus offsets
 *  originales. NO añadimos nada: el tamaño lo fija el juego.
 *  Ver include/ultra64/thread.h (size = 0x1B0).
 * ───────────────────────────────────────────────────────────────── */
typedef struct OSThread {
    /* 0x00 */ struct OSThread* next;
    /* 0x04 */ OSPri            priority;
    /* 0x08 */ struct OSThread** queue;
    /* 0x0C */ struct OSThread* tlnext;
    /* 0x10 */ u16              state;
    /* 0x12 */ u16              flags;
    /* 0x14 */ OSId             id;
    /* 0x18 */ s32              fp;
    /* resto: thprof + context, que nunca tocamos */
} OSThread;

/* ───────────────────────────────────────────────────────────────── *
 *  Pool de pilas
 *
 *  32 KB por hilo. En N64 las pilas eran de 1–6 KB porque la RAM era
 *  de 8 MB; el EE tiene 32 MB, así que damos margen de sobra y nos
 *  quitamos de encima los desbordamientos silenciosos.
 * ───────────────────────────────────────────────────────────────── */
#define PS2_THREAD_STACK_SIZE   (32 * 1024)

static u8 s_stackPool[PS2_OS_MAX_THREADS][PS2_THREAD_STACK_SIZE]
          __attribute__((aligned(64)));
static bool s_stackUsed[PS2_OS_MAX_THREADS];

/* ───────────────────────────────────────────────────────────────── *
 *  Tabla lateral: OSThread* → datos de PS2
 * ───────────────────────────────────────────────────────────────── */
typedef struct {
    OSThread* key;          /* NULL = entrada libre           */
    int       ee_id;        /* id de hilo del kernel EE       */
    int       stackSlot;    /* índice en s_stackPool          */
    void*     arg;          /* argumento para la entry func   */
    bool      started;
} PS2ThreadEntry;

static PS2ThreadEntry s_threads[PS2_OS_MAX_THREADS];

static PS2ThreadEntry* thread_find(OSThread* t) {
    for (int i = 0; i < PS2_OS_MAX_THREADS; i++) {
        if (s_threads[i].key == t) return &s_threads[i];
    }
    return NULL;
}

static PS2ThreadEntry* thread_alloc(OSThread* t) {
    PS2ThreadEntry* e = thread_find(t);
    if (e) return e;                      /* re-creación del mismo hilo */
    for (int i = 0; i < PS2_OS_MAX_THREADS; i++) {
        if (s_threads[i].key == NULL) {
            s_threads[i].key       = t;
            s_threads[i].ee_id     = -1;
            s_threads[i].stackSlot = -1;
            s_threads[i].arg       = NULL;
            s_threads[i].started   = false;
            return &s_threads[i];
        }
    }
    return NULL;
}

static int stack_alloc(void) {
    for (int i = 0; i < PS2_OS_MAX_THREADS; i++) {
        if (!s_stackUsed[i]) {
            s_stackUsed[i] = true;
            return i;
        }
    }
    return -1;
}

static void stack_free(int slot) {
    if (slot >= 0 && slot < PS2_OS_MAX_THREADS) {
        s_stackUsed[slot] = false;
    }
}

/* ───────────────────────────────────────────────────────────────── *
 *  osCreateThread
 *
 *  Firma libultra:
 *    void osCreateThread(OSThread*, OSId, void(*)(void*), void* arg,
 *                        void* stackTop, OSPri pri)
 * ───────────────────────────────────────────────────────────────── */
void osCreateThread(OSThread* thread, OSId id, void (*func)(void*),
                    void* arg, void* stackTop, OSPri priority) {
    if (!thread || !func) return;

    /* La pila del juego se ignora a propósito: ver cabecera del archivo. */
    (void)stackTop;

    int lock = ps2_os_lock();
    PS2ThreadEntry* e = thread_alloc(thread);
    ps2_os_unlock(lock);

    if (!e) {
        printf("[osThread] ERROR: tabla de hilos llena (id=%d)\n", (int)id);
        return;
    }

    /* Si es una re-creación, liberamos los recursos anteriores. */
    if (e->ee_id >= 0) {
        TerminateThread(e->ee_id);
        DeleteThread(e->ee_id);
        e->ee_id = -1;
    }
    if (e->stackSlot < 0) {
        e->stackSlot = stack_alloc();
        if (e->stackSlot < 0) {
            printf("[osThread] ERROR: sin pilas libres (id=%d)\n", (int)id);
            return;
        }
    }

    /* Campos visibles para el juego */
    thread->id       = id;
    thread->priority = priority;
    thread->state    = OS_STATE_STOPPED;
    thread->next     = NULL;
    thread->queue    = NULL;
    thread->tlnext   = NULL;
    thread->flags    = 0;
    thread->fp       = 0;

    ee_thread_t et;
    memset(&et, 0, sizeof(et));
    et.func             = (void*)func;
    et.stack            = s_stackPool[e->stackSlot];   /* BASE, no tope */
    et.stack_size       = PS2_THREAD_STACK_SIZE;
    et.gp_reg           = &_gp;
    et.initial_priority = ps2_os_map_priority((int)priority);
    et.attr             = 0;
    et.option           = 0;

    e->ee_id   = CreateThread(&et);
    e->started = false;

    if (e->ee_id < 0) {
        printf("[osThread] ERROR: CreateThread fallo (id=%d, ret=%d)\n",
               (int)id, e->ee_id);
        stack_free(e->stackSlot);
        e->stackSlot = -1;
        return;
    }

    /* El argumento se guarda en la tabla lateral y se entrega en
     * osStartThread, respetando el orden de libultra (crear != arrancar).
     * NUNCA en thread->next: ese campo es del juego. */
    e->arg = arg;
}

/* ───────────────────────────────────────────────────────────────── *
 *  osStartThread
 * ───────────────────────────────────────────────────────────────── */
void osStartThread(OSThread* thread) {
    if (!thread) return;

    PS2ThreadEntry* e = thread_find(thread);
    if (!e || e->ee_id < 0) {
        printf("[osThread] ERROR: osStartThread sobre hilo no creado\n");
        return;
    }

    thread->state = OS_STATE_RUNNABLE;

    int ret = StartThread(e->ee_id, e->arg);
    if (ret < 0) {
        printf("[osThread] ERROR: StartThread fallo (id=%d, ret=%d)\n",
               (int)thread->id, ret);
        return;
    }
    e->started = true;
}

/* ───────────────────────────────────────────────────────────────── *
 *  osStopThread
 * ───────────────────────────────────────────────────────────────── */
void osStopThread(OSThread* thread) {
    if (!thread) {
        /* libultra: NULL = el hilo actual */
        SuspendThread(GetThreadId());
        return;
    }
    PS2ThreadEntry* e = thread_find(thread);
    if (e && e->ee_id >= 0 && e->started) {
        SuspendThread(e->ee_id);
    }
    thread->state = OS_STATE_STOPPED;
}

/* ───────────────────────────────────────────────────────────────── *
 *  osDestroyThread
 * ───────────────────────────────────────────────────────────────── */
void osDestroyThread(OSThread* thread) {
    if (!thread) {
        ExitDeleteThread();
        return;
    }

    PS2ThreadEntry* e = thread_find(thread);
    if (!e) return;

    if (e->ee_id >= 0) {
        TerminateThread(e->ee_id);
        DeleteThread(e->ee_id);
        e->ee_id = -1;
    }
    stack_free(e->stackSlot);

    int lock = ps2_os_lock();
    e->key       = NULL;
    e->stackSlot = -1;
    e->arg       = NULL;
    e->started   = false;
    ps2_os_unlock(lock);

    thread->state = OS_STATE_STOPPED;
}

/* ───────────────────────────────────────────────────────────────── *
 *  osSetThreadPri / osGetThreadPri
 *
 *  Ojo: el juego llama osSetThreadPri(NULL, ...) para cambiar SU propia
 *  prioridad (main.c lo hace en PAL_1_0+). Hay que soportar el NULL.
 * ───────────────────────────────────────────────────────────────── */
void osSetThreadPri(OSThread* thread, OSPri priority) {
    if (thread == NULL) {
        ChangeThreadPriority(GetThreadId(), ps2_os_map_priority((int)priority));
        return;
    }
    thread->priority = priority;
    PS2ThreadEntry* e = thread_find(thread);
    if (e && e->ee_id >= 0) {
        ChangeThreadPriority(e->ee_id, ps2_os_map_priority((int)priority));
    }
}

OSPri osGetThreadPri(OSThread* thread) {
    if (thread == NULL) {
        ee_thread_status_t st;
        if (ReferThreadStatus(GetThreadId(), &st) >= 0) {
            /* invertimos el mapeo */
            int n64 = 64 - st.current_priority;
            if (n64 < 0) n64 = 0;
            return (OSPri)n64;
        }
        return 0;
    }
    return thread->priority;
}

OSId osGetThreadId(OSThread* thread) {
    if (thread == NULL) {
        /* Buscamos cuál de los nuestros es el hilo EE actual */
        int cur = GetThreadId();
        for (int i = 0; i < PS2_OS_MAX_THREADS; i++) {
            if (s_threads[i].key && s_threads[i].ee_id == cur) {
                return s_threads[i].key->id;
            }
        }
        return 0;
    }
    return thread->id;
}

void osSetThreadId(OSThread* thread, OSId id) {
    if (thread) thread->id = id;
}

/* ───────────────────────────────────────────────────────────────── *
 *  osYieldThread
 * ───────────────────────────────────────────────────────────────── */
void osYieldThread(void) {
    RotateThreadReadyQueue(0);
}

/* ───────────────────────────────────────────────────────────────── *
 *  ps2_thread_init() — llamar una vez antes de crear ningún hilo
 * ───────────────────────────────────────────────────────────────── */
void ps2_thread_init(void) {
    memset(s_threads, 0, sizeof(s_threads));
    memset(s_stackUsed, 0, sizeof(s_stackUsed));
    printf("[osThread] Pool de hilos listo (%d hilos x %d KB de pila)\n",
           PS2_OS_MAX_THREADS, PS2_THREAD_STACK_SIZE / 1024);
}
