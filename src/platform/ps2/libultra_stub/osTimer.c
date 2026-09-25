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
    *T0_MODE = 0;
    *T0_COUNT = 0;

    s_tickHigh  = 0;
    s_handlerId = AddIntcHandler(INTC_TIM0, _timer_overflow_handler, 0);
    if (s_handlerId < 0) {
        printf("[osTimer] AVISO: no se pudo instalar el handler de TIM0\n");
    } else {
        EnableIntc(INTC_TIM0);
    }

    /* CUE | OVFE */
    *T0_MODE = Tn_MODE(0, 0, 0, 0, 0, 1, 0, 1, 1, 1);

    printf("[osTimer] EE Timer 0 en marcha\n");
}

/* ───────────────────────────────────────────────────────────────── *
 *  Lectura segura del contador de 64 bits
 *
 *  Doble muestreo: si la parte alta cambió mientras leíamos la baja,
 *  repetimos. Evita el salto hacia atrás en el instante del overflow.
 * ───────────────────────────────────────────────────────────────── */
static u64 read_ticks(void) {
    int lock = ps2_os_lock();
    u64 high = s_tickHigh;
    u32 low = *T0_COUNT & 0xFFFF;
    /* An overflow may be pending while interrupts are disabled. */
    if (*T0_MODE & (1 << 11)) {
        high += 0x10000ULL;
        low = *T0_COUNT & 0xFFFF;
    }
    ps2_os_unlock(lock);
    return high + low;
}

/* ───────────────────────────────────────────────────────────────── *
 *  osGetTime — OSTime en ticks equivalentes de N64
 * ───────────────────────────────────────────────────────────────── */
OSTime osGetTime(void) {
    u64 t = read_ticks();
    /* Multiplicar primero y dividir después conserva la precisión.
     * 46875000/147456000 exacto, sin la aproximación 3178/10000. */
    return (t / PS2_T0_HZ) * N64_COUNTER_HZ
         + ((t % PS2_T0_HZ) * N64_COUNTER_HZ) / PS2_T0_HZ;
}

/* ───────────────────────────────────────────────────────────────── *
 *  osGetCount — contador crudo de 32 bits (equivale a CP0 Count)
 * ───────────────────────────────────────────────────────────────── */
u32 osGetCount(void) {
    return (u32)osGetTime();
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
    /* libultra allows the caller to pass an uninitialized stack timer. */
    for (OSTimer* active = s_activeList; active; active = active->next) {
        if (active == t) return true;
    }
    return false;
}

/* ───────────────────────────────────────────────────────────────── *
 *  osSetTimer — ahora sí registra el temporizador
 * ───────────────────────────────────────────────────────────────── */
s32 osSetTimer(OSTimer* timer, OSTime countdown, OSTime interval,
               struct OSMesgQueue* mq, OSMesg msg) {
    if (!timer) return -1;

    int lock = ps2_os_lock();

    if (timer_is_linked(timer)) timer_unlink(timer);

    timer->value    = osGetTime() + (countdown ? countdown : interval);
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
    for (;;) {
        int lock = ps2_os_lock();
        OSTimer* t = s_activeList;
        while (t && now < t->value) t = t->next;
        if (!t) {
            ps2_os_unlock(lock);
            break;
        }
        struct OSMesgQueue* mq = t->mq;
        OSMesg msg = t->msg;
        if (t->interval > 0) {
            t->value += ((now - t->value) / t->interval + 1) * t->interval;
        } else {
            timer_unlink(t);
        }
        ps2_os_unlock(lock);
        /* Queue operations may schedule another thread. Never hold the
         * interrupt lock while sending, nor retain t across the send. */
        if (mq) osSendMesg(mq, msg, OS_MESG_NOBLOCK);
    }
}
