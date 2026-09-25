/**
 * osTimer.c — Tiempo y temporizadores de libultra sobre el Timer 0 del EE
 *
 * ── QUÉ ESTABA MAL ────────────────────────────────────────────────────
 *
 * 1. osSetTimer() escribía los datos en el OSTimer del llamante, pero
 *    NUNCA lo metía en la tabla s_timers[] que osTimer_tick() recorre.
 *    La tabla estaba permanentemente vacía: ningún temporizador disparó
 *    jamás. Encima osTimer_tick() no lo llamaba nadie.
 *
 * 2. El struct OSTimer era inventado. El real (ultra64/time.h) es una
 *    lista doblemente enlazada: { next, prev, interval, value, mq, msg }
 *    con size = 0x20.
 *
 * 3. osGetTime() hacía `s_tickCounter | low` cuando debía ser una SUMA.
 *    Un OR da resultados erróneos en cuanto los bits bajos se solapan.
 *
 * 4. Leía el contador sin protección frente al desbordamiento: si el
 *    timer daba la vuelta entre leer el contador alto y el bajo, el
 *    tiempo saltaba ~29 segundos hacia atrás.
 *
 * ── CÓMO SE ARREGLA ───────────────────────────────────────────────────
 *
 * - OSTimer conserva su layout original y los temporizadores activos se
 *   enlazan en una lista real.
 * - Lectura del contador con doble muestreo para evitar la carrera.
 * - Aritmética de 64 bits exacta (multiplicar antes de dividir).
 */

#include <kernel.h>
#include <timer.h>
#include <string.h>
#include <stdio.h>

#include "ps2_os_internal.h"

typedef void* OSMesg;
struct OSMesgQueue;

extern s32 osSendMesg(struct OSMesgQueue* mq, OSMesg msg, s32 flags);
#define OS_MESG_NOBLOCK  0

/* ───────────────────────────────────────────────────────────────── *
 *  OSTimer — layout EXACTO de libultra (ultra64/time.h), size = 0x20
 * ───────────────────────────────────────────────────────────────── */
typedef struct OSTimer {
    /* 0x00 */ struct OSTimer*     next;
    /* 0x04 */ struct OSTimer*     prev;
    /* 0x08 */ OSTime              interval;
    /* 0x10 */ OSTime              value;
    /* 0x18 */ struct OSMesgQueue* mq;
    /* 0x1C */ OSMesg              msg;
} OSTimer;

_Static_assert(sizeof(OSTimer) == 0x20, "OSTimer debe medir 0x20 bytes");

/* ───────────────────────────────────────────────────────────────── *
 *  Relojes
 *
 *  N64: el contador de libultra corre a 46.875 MHz.
 *  PS2: el Timer 0 del EE corre a BUSCLK = 147.456 MHz.
 *
 *  Para que las conversiones OS_CYCLES_TO_USEC del juego sigan dando
 *  valores con sentido, sintetizamos un OSTime en "ticks de N64".
 * ───────────────────────────────────────────────────────────────── */
#define N64_COUNTER_HZ    46875000ULL
#define PS2_T0_HZ        147456000ULL

static volatile u64 s_tickHigh = 0;    /* extensión a 64 bits          */
static int          s_handlerId = -1;

/* El Timer 0 del EE sólo cuenta 16 bits en modo directo; usamos el
 * modo de desbordamiento para acumular. Overflow ≈ cada 0.44 ms. */
static int _timer_overflow_handler(int id) {
    (void)id;
    s_tickHigh += 0x10000ULL;
    /* Limpiar el flag de overflow para que vuelva a dispararse */
    *T0_MODE |= (1 << 11);
    ExitHandler();
    return 0;
}

/* ───────────────────────────────────────────────────────────────── *
 *  ps2_timer_init()
 * ───────────────────────────────────────────────────────────────── */
void ps2_timer_init(void) {
    /* CLKS=0 (bus clock), CUE=1 (contar), OVFE=1 (int. por overflow) */
    SetTimerMode(0, 0);
    SetTimerCounter(0, 0);

    s_tickHigh  = 0;
    s_handlerId = AddIntcHandler(INTC_TIM0, _timer_overflow_handler, 0);
    if (s_handlerId < 0) {
        printf("[osTimer] AVISO: no se pudo instalar el handler de TIM0\n");
    } else {
        EnableIntc(INTC_TIM0);
    }

    /* CUE | OVFE */
    SetTimerMode(0, (1 << 7) | (1 << 9));
    StartTimer(0);

    printf("[osTimer] EE Timer 0 en marcha\n");
}

/* ───────────────────────────────────────────────────────────────── *
 *  Lectura segura del contador de 64 bits
 *
 *  Doble muestreo: si la parte alta cambió mientras leíamos la baja,
 *  repetimos. Evita el salto hacia atrás en el instante del overflow.
 * ───────────────────────────────────────────────────────────────── */
static u64 read_ticks(void) {
    u64 hi1, hi2;
    u32 low;
    do {
        hi1 = s_tickHigh;
        low = (u32)GetTimerCounter(0) & 0xFFFF;
        hi2 = s_tickHigh;
    } while (hi1 != hi2);
    return hi1 + (u64)low;     /* SUMA, no OR */
}

/* ───────────────────────────────────────────────────────────────── *
 *  osGetTime — OSTime en ticks equivalentes de N64
 * ───────────────────────────────────────────────────────────────── */
OSTime osGetTime(void) {
    u64 t = read_ticks();
    /* Multiplicar primero y dividir después conserva la precisión.
     * 46875000/147456000 exacto, sin la aproximación 3178/10000. */
    return (OSTime)((t * N64_COUNTER_HZ) / PS2_T0_HZ);
}

/* ───────────────────────────────────────────────────────────────── *
 *  osGetCount — contador crudo de 32 bits (equivale a CP0 Count)
 * ───────────────────────────────────────────────────────────────── */
u32 osGetCount(void) {
    return (u32)read_ticks();
}

void osSetTime(OSTime time) {
    (void)time;   /* no soportado: nuestro reloj es monótono */
}

/* ───────────────────────────────────────────────────────────────── *
 *  Lista de temporizadores activos
 * ───────────────────────────────────────────────────────────────── */
static OSTimer* s_activeList = NULL;

static void timer_unlink(OSTimer* t) {
    if (t->prev) t->prev->next = t->next;
    else if (s_activeList == t) s_activeList = t->next;
    if (t->next) t->next->prev = t->prev;
    t->next = t->prev = NULL;
}

static bool timer_is_linked(OSTimer* t) {
    if (s_activeList == t) return true;
    return (t->next != NULL || t->prev != NULL);
}

/* ───────────────────────────────────────────────────────────────── *
 *  osSetTimer — ahora sí registra el temporizador
 * ───────────────────────────────────────────────────────────────── */
s32 osSetTimer(OSTimer* timer, OSTime countdown, OSTime interval,
               struct OSMesgQueue* mq, OSMesg msg) {
    if (!timer) return -1;

    int lock = ps2_os_lock();

    if (timer_is_linked(timer)) timer_unlink(timer);

    timer->value    = osGetTime() + countdown;   /* momento de disparo */
    timer->interval = interval;
    timer->mq       = mq;
    timer->msg      = msg;

    /* Enlazar al principio de la lista activa */
    timer->prev = NULL;
    timer->next = s_activeList;
    if (s_activeList) s_activeList->prev = timer;
    s_activeList = timer;

    ps2_os_unlock(lock);
    return 0;
}

/* ───────────────────────────────────────────────────────────────── *
 *  osStopTimer
 * ───────────────────────────────────────────────────────────────── */
s32 osStopTimer(OSTimer* timer) {
    if (!timer) return -1;
    int lock = ps2_os_lock();
    if (timer_is_linked(timer)) timer_unlink(timer);
    ps2_os_unlock(lock);
    return 0;
}

/* ───────────────────────────────────────────────────────────────── *
 *  ps2_timer_tick() — llamar una vez por frame desde el bucle del juego
 *
 *  Antes se llamaba osTimer_tick() y no la invocaba nadie. Ahora se
 *  llama desde ps2_vblank_tick() (ps2_boot.c), que corre cada frame.
 * ───────────────────────────────────────────────────────────────── */
void ps2_timer_tick(void) {
    OSTime now = osGetTime();

    int lock = ps2_os_lock();
    OSTimer* t = s_activeList;
    while (t) {
        OSTimer* next = t->next;   /* t puede desenlazarse abajo */

        if (now >= t->value) {
            if (t->mq) {
                osSendMesg(t->mq, t->msg, OS_MESG_NOBLOCK);
            }
            if (t->interval > 0) {
                t->value = now + t->interval;   /* reprogramar */
            } else {
                timer_unlink(t);                /* one-shot: fuera */
            }
        }
        t = next;
    }
    ps2_os_unlock(lock);
}
