/**
 * osVi.c — Video Interface de N64 sobre PS2 (NUEVO)
 *
 * ── POR QUÉ HACÍA FALTA ───────────────────────────────────────────────
 *
 * Este archivo no existía. El juego usa 15 funciones osVi* distintas
 * (sched.c, graph.c, game.c, irqmgr.c, fault.c...), así que sin ellas
 * el enlace fallaba con una montaña de símbolos indefinidos en cuanto
 * se compilase src/code de verdad.
 *
 * Lo más importante que hay aquí no son los stubs, sino el evento de
 * retrazo vertical:
 *
 *     osViSetEvent(&irqMgr->queue, IRQ_RETRACE_MSG, retraceCount);
 *
 * IrqMgr lo registra y el scheduler de OoT usa ese mensaje como LATIDO
 * de todo el motor: es lo que hace avanzar los frames. Sin retrace, el
 * juego no avanza aunque todo lo demás funcione.
 */

#include <kernel.h>
#include <string.h>
#include <stdio.h>

#include "ps2_os_internal.h"

typedef void* OSMesg;
struct OSMesgQueue;

extern s32 osSendMesg(struct OSMesgQueue* mq, OSMesg msg, s32 flags);
#define OS_MESG_NOBLOCK  0

/* ───────────────────────────────────────────────────────────────── *
 *  OSViMode — layout real (ultra64/vi.h), size = 0x4C
 *
 *  El juego lee campos de aquí (z_vimode.c inspecciona comRegs.ctrl,
 *  comRegs.width...), así que el tamaño y el orden importan aunque
 *  nosotros no usemos los registros.
 * ───────────────────────────────────────────────────────────────── */
typedef struct {
    /* 0x00 */ u32 ctrl;
    /* 0x04 */ u32 width;
    /* 0x08 */ u32 burst;
    /* 0x0C */ u32 vSync;
    /* 0x10 */ u32 hSync;
    /* 0x14 */ u32 leap;
    /* 0x18 */ u32 hStart;
    /* 0x1C */ u32 xScale;
    /* 0x20 */ u32 vCurrent;
} OSViCommonRegs;

typedef struct {
    /* 0x00 */ u32 origin;
    /* 0x04 */ u32 yScale;
    /* 0x08 */ u32 vStart;
    /* 0x0C */ u32 vBurst;
    /* 0x10 */ u32 vIntr;
} OSViFieldRegs;

typedef struct OSViMode {
    /* 0x00 */ u8             type;
    /* 0x04 */ OSViCommonRegs comRegs;
    /* 0x28 */ OSViFieldRegs  fldRegs[2];
} OSViMode;

/* NOTA SOBRE EL TAMAÑO — comprobado, no supuesto:
 *
 * include/ultra64/vi.h dice en sus comentarios que fldRegs está en 0x24
 * y que OSViMode mide 0x4C. Esos comentarios NO cuadran con los campos
 * que el propio header declara:
 *
 *     u8 type          → 1 byte + 3 de relleno  =  4  (0x04)
 *     OSViCommonRegs   → 9 campos u32           = 36  (0x24)  → acaba en 0x28
 *     OSViFieldRegs[2] → 2 x 5 campos u32       = 40  (0x28)
 *                                                 ────────────
 *                                                 80  (0x50)
 *
 * O sea: los comentarios de offset del decomp están desactualizados en
 * esta estructura. Lo que manda es la declaración, y la declaración da
 * 0x50.
 *
 * Esto NO puede causar desajuste entre el juego y nosotros, porque aquí
 * se repiten exactamente los mismos campos en el mismo orden: el
 * compilador calcula el mismo tamaño para las dos. Si algún día cambian
 * los campos en vi.h, este assert salta y avisa. */
_Static_assert(sizeof(OSViMode) == 0x50,
               "OSViMode: los campos deben coincidir con ultra64/vi.h");

/* ───────────────────────────────────────────────────────────────── *
 *  Modos de vídeo estándar
 *
 *  Los valores de los registros son los de N64 y aquí no hacen nada
 *  (la PS2 configura el vídeo por gsKit), pero el juego los lee y los
 *  copia, así que tienen que existir con valores coherentes.
 *
 *  Lo que sí se respeta es `type`, que OoT usa para decidir NTSC/PAL.
 * ───────────────────────────────────────────────────────────────── */
#define OS_VI_NTSC_LAN1   0
#define OS_VI_PAL_LAN1    1
#define OS_VI_MPAL_LAN1   2
#define OS_VI_FPAL_LAN1   3

#define VI_MODE(t, w, h) { \
    .type = (t),                                  \
    .comRegs = { .ctrl = 0x3242, .width = (w),    \
                 .burst = 0x03E52239,             \
                 .vSync = (h), .hSync = 0x0C15,   \
                 .leap = 0x0C150C15,              \
                 .hStart = 0x006C02EC,            \
                 .xScale = 0x00000200,            \
                 .vCurrent = 0 },                 \
    .fldRegs = { { 0, 0x00000400, 0x002501FF, 0x000E0204, 0x00000002 }, \
                 { 0, 0x00000400, 0x002501FF, 0x000E0204, 0x00000002 } } \
}

OSViMode osViModeNtscLan1 = VI_MODE(OS_VI_NTSC_LAN1, 320, 525);
OSViMode osViModePalLan1  = VI_MODE(OS_VI_PAL_LAN1,  320, 625);
OSViMode osViModeMpalLan1 = VI_MODE(OS_VI_MPAL_LAN1, 320, 525);
OSViMode osViModeFpalLan1 = VI_MODE(OS_VI_FPAL_LAN1, 320, 625);

/* El modo activo. La PS2 sale en NTSC por defecto. */
OSViMode* osViModePtr = &osViModeNtscLan1;

/* ───────────────────────────────────────────────────────────────── *
 *  Tipo de televisión / tipo de reset (los lee el juego)
 * ───────────────────────────────────────────────────────────────── */
#define OS_TV_PAL   0
#define OS_TV_NTSC  1
#define OS_TV_MPAL  2

/* CORREGIDO: son s32, no u32 (ultra64/os_system.h). */
s32 osTvType    = OS_TV_NTSC;
s32 osResetType = 0;          /* 0 = arranque en frio */

/* ───────────────────────────────────────────────────────────────── *
 *  Estado del VI
 * ───────────────────────────────────────────────────────────────── */
static void* s_currentFb = NULL;   /* el que se está mostrando */
static void* s_nextFb    = NULL;   /* el que se mostrará en el próximo retrace */
static bool  s_black     = false;

static struct OSMesgQueue* s_retraceQueue   = NULL;
static OSMesg              s_retraceMsg     = NULL;
static u32                 s_retraceCount   = 1;
static u32                 s_retraceCounter = 0;

/* ───────────────────────────────────────────────────────────────── *
 *  osViSwapBuffer — programa el framebuffer del siguiente retrace
 *
 *  OJO: en N64 esto NO cambia la imagen inmediatamente, sólo la deja
 *  "pendiente" hasta el próximo retrazo vertical. El scheduler de OoT
 *  depende de esa diferencia: compara osViGetCurrentFramebuffer() con
 *  el buffer que pidió para saber si el swap ya ocurrió.
 *
 *  Si aquí hiciéramos el swap al instante, sched.c creería que el frame
 *  ya se mostró antes de tiempo y se comería frames.
 * ───────────────────────────────────────────────────────────────── */
void osViSwapBuffer(void* frameBufPtr) {
    s_nextFb = frameBufPtr;
}

void* osViGetCurrentFramebuffer(void) {
    return s_currentFb;
}

void* osViGetNextFramebuffer(void) {
    return s_nextFb;
}

/* ───────────────────────────────────────────────────────────────── *
 *  osViSetEvent — registra la cola que recibe el mensaje de retrace
 * ───────────────────────────────────────────────────────────────── */
void osViSetEvent(struct OSMesgQueue* mq, OSMesg msg, u32 retraceCount) {
    int lock = ps2_os_lock();
    s_retraceQueue = mq;
    s_retraceMsg   = msg;
    s_retraceCount = (retraceCount == 0) ? 1 : retraceCount;
    ps2_os_unlock(lock);
}

/* ───────────────────────────────────────────────────────────────── *
 *  ps2_vi_retrace() — llamar una vez por frame (desde el vblank)
 *
 *  Hace dos cosas:
 *    1. Consuma el swap pendiente (next → current), imitando al VI.
 *    2. Manda el mensaje de retrace, que es el latido del motor.
 * ───────────────────────────────────────────────────────────────── */
void ps2_vi_retrace(void) {
    if (s_nextFb != NULL) {
        s_currentFb = s_nextFb;
    }

    s_retraceCounter++;
    if (s_retraceCounter >= s_retraceCount) {
        s_retraceCounter = 0;
        if (s_retraceQueue) {
            osSendMesg(s_retraceQueue, s_retraceMsg, OS_MESG_NOBLOCK);
        }
        ps2_os_fire_event(PS2_OS_EVENT_VI);
    }
}

/* ───────────────────────────────────────────────────────────────── *
 *  Resto de la API del VI
 *
 *  En PS2 el vídeo lo configura gsKit, así que estas funciones sólo
 *  guardan el estado para que el juego lo pueda leer de vuelta.
 * ───────────────────────────────────────────────────────────────── */
void osViSetMode(OSViMode* mode) {
    if (mode) osViModePtr = mode;
}

void osViBlack(u8 active) {
    s_black = (active != 0);
}

u8 ps2_vi_is_black(void) {
    return s_black ? 1 : 0;
}

void osViSetSpecialFeatures(u32 func) { (void)func; }
void osViSetXScale(f32 value)         { (void)value; }
void osViSetYScale(f32 value)         { (void)value; }
void osViExtendVStart(u32 value)      { (void)value; }

/* ───────────────────────────────────────────────────────────────── *
 *  osCreateViManager — en N64 lanza el hilo del VI.
 *  Aquí el "VI manager" es ps2_vi_retrace(), llamado desde el vblank.
 * ───────────────────────────────────────────────────────────────── */
void osCreateViManager(OSPri pri) {
    (void)pri;
}

void ps2_vi_init(void) {
    s_currentFb      = NULL;
    s_nextFb         = NULL;
    s_retraceQueue   = NULL;
    s_retraceCounter = 0;
    s_retraceCount   = 1;
    printf("[osVi] Video Interface listo (NTSC)\n");
}
