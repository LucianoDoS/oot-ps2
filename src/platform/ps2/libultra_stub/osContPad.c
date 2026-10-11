/**
 * osContPad.c — Controles: DualShock 2 → formato de mando de N64
 *
 * ── QUÉ ESTABA MAL ────────────────────────────────────────────────────
 *
 * 1. DOBLE INICIALIZACIÓN. ps2_main.c llamaba a ps2_init_pad() y este
 *    archivo a osContInit(), y AMBOS hacían padPortOpen() sobre los
 *    mismos puertos con buffers DMA distintos. libpad no tolera eso:
 *    el segundo open deja el primero apuntando a memoria que ya no se
 *    actualiza. Ahora hay un único punto de apertura, aquí.
 *
 * 2. MAPEO DE BOTONES ROTO. Circle y R3 estaban los dos asignados a
 *    C-Right, y L3 a C-Left, de modo que pulsar el stick derecho hacía
 *    cosas raras. Además el propio archivo declaraba una zona muerta
 *    (ANALOG_DEAD_ZONE en ps2_controller.c) que nunca se aplicaba.
 *
 * 3. No se esperaba a que el mando estuviera listo antes de leerlo, ni
 *    se configuraba el modo analógico: sin padSetMainMode() el stick
 *    izquierdo devuelve 128,128 fijo (modo digital).
 *
 * ── MAPEO NUEVO ───────────────────────────────────────────────────────
 *
 *   N64          DualShock 2
 *   A            Cruz
 *   B            Cuadrado
 *   Z            L2
 *   Start        Start
 *   D-Pad        D-Pad
 *   L / R        L1 / R1
 *   C-Up         Triángulo
 *   C-Down       Círculo
 *   C-Left/Right stick derecho (analógico)   + L3/R3 como respaldo
 *
 *   Los botones C se leen sobre todo del stick derecho, que es como se
 *   juega OoT hoy en día, y Círculo/Triángulo quedan de apoyo.
 */

#include <libpad.h>
#include <kernel.h>
#include <string.h>
#include <stdio.h>

#include "ps2_types.h"
#include "ps2_os_internal.h"
#include "ps2_pad_internal.h"

struct OSMesgQueue;

/* ───────────────────────────────────────────────────────────────── *
 *  Bits del mando de N64 (ultra64/controller.h)
 * ───────────────────────────────────────────────────────────────── */
#define N64_A_BUTTON     0x8000
#define N64_B_BUTTON     0x4000
#define N64_Z_TRIG       0x2000
#define N64_START        0x1000
#define N64_D_UP         0x0800
#define N64_D_DOWN       0x0400
#define N64_D_LEFT       0x0200
#define N64_D_RIGHT      0x0100
#define N64_L_TRIG       0x0020
#define N64_R_TRIG       0x0010
#define N64_C_UP         0x0008
#define N64_C_DOWN       0x0004
#define N64_C_LEFT       0x0002
#define N64_C_RIGHT      0x0001

/* Códigos de error de libultra */
#define CONT_NO_RESPONSE_ERROR  0x08
#define CONT_CARD_ON            0x01

/* ───────────────────────────────────────────────────────────────── *
 *  Structs de libultra — layout EXACTO (ultra64/controller.h)
 * ───────────────────────────────────────────────────────────────── */
typedef struct OSContStatus {
    /* 0x00 */ u16 type;
    /* 0x02 */ u8  status;
    /* 0x03 */ u8  errno;
} OSContStatus;   /* size = 0x04 */

typedef struct OSContPad {
    /* 0x00 */ u16 button;
    /* 0x02 */ s8  stick_x;
    /* 0x03 */ s8  stick_y;
    /* 0x04 */ u8  errno;
} OSContPad;      /* size = 0x06 con padding */

/* ───────────────────────────────────────────────────────────────── *
 *  Configuración
 * ───────────────────────────────────────────────────────────────── */
#define MAX_CONTROLLERS   4
#define PS2_PORTS         2      /* la PS2 tiene 2 puertos físicos */
#define ANALOG_DEAD_ZONE  10     /* sobre 128 */
#define C_STICK_THRESHOLD 48     /* cuánto hay que mover el stick derecho */

/* Tipo de mando que reportamos: mando estándar de N64 con Rumble Pak */
#define CONT_TYPE_NORMAL  0x0005

/* libpad exige buffers alineados a 64 bytes */
static u8 s_padBuf[PS2_PORTS][256] __attribute__((aligned(64)));
static struct padButtonStatus s_padStatus[PS2_PORTS];
static bool s_portOpen[PS2_PORTS];
static bool s_padInitDone = false;

typedef enum PadConfig {
    PAD_CONFIG_DISCOVER,
    PAD_CONFIG_WAIT_MODE,
    PAD_CONFIG_ALIGN,
    PAD_CONFIG_WAIT_ALIGN,
    PAD_CONFIG_STOP,
    PAD_CONFIG_READY,
    PAD_CONFIG_NO_MOTOR
} PadConfig;

static PadConfig s_padConfig[PS2_PORTS];
static u8 s_configRetry[PS2_PORTS];
static bool s_motorOn[PS2_PORTS];
static bool s_stopPending[PS2_PORTS];
static const char s_motorOff[6] = {0, 0, 0, 0, 0, 0};

static void pad_stop_motor(int port) {
    if (s_motorOn[port] || s_stopPending[port]) {
        /* Clear PADMAN's cached command as well as the local flag. An
         * unplugged pad may reject this; negotiation sends off again
         * before rumble is exposed after reconnection. */
        s_stopPending[port] = padSetActDirect(port, 0, s_motorOff) != 1;
        if (!s_stopPending[port]) s_motorOn[port] = false;
    }
}

static void pad_forget(int port) {
    s_padConfig[port] = PAD_CONFIG_DISCOVER;
    s_configRetry[port] = 0;
    s_motorOn[port] = false;
    s_stopPending[port] = false;
    memset(&s_padStatus[port], 0, sizeof(s_padStatus[port]));
}

/* ───────────────────────────────────────────────────────────────── *
 *  Advance negotiation once per call; PadMgr polls us on each retrace.
 *  The SDK requests are asynchronous: STABLE alone does not confirm a
 *  mode/alignment request. Check its request status on a later poll.
 *  No busy wait may hold up PadMgr, audio, or a disconnected controller.
 * ───────────────────────────────────────────────────────────────── */
static bool pad_poll(int port) {
    if (port < 0 || port >= PS2_PORTS || !s_portOpen[port]) return false;

    int state = padGetState(port, 0);
    if (state == PAD_STATE_DISCONN || state == PAD_STATE_ERROR || state == PAD_STATE_FINDPAD) {
        pad_stop_motor(port);
        pad_forget(port);
        return false;
    }
    if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1) return false;

    if (s_configRetry[port]) {
        --s_configRetry[port];
        return true;
    }
    int request = padGetReqState(port, 0);
    if (request == PAD_RSTAT_BUSY) return true;

    switch (s_padConfig[port]) {
        case PAD_CONFIG_DISCOVER: {
            int modes = padInfoMode(port, 0, PAD_MODETABLE, -1);
            bool dualshock = false;
            for (int i = 0; i < modes; ++i) {
                if (padInfoMode(port, 0, PAD_MODETABLE, i) == PAD_TYPE_DUALSHOCK)
                    dualshock = true;
            }
            if (!dualshock || !padInfoMode(port, 0, PAD_MODECUREXID, 0)) {
                s_padConfig[port] = PAD_CONFIG_NO_MOTOR;
            } else if (padSetMainMode(port, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK) == 1) {
                s_padConfig[port] = PAD_CONFIG_WAIT_MODE;
            } else {
                s_configRetry[port] = 60;
            }
            break;
        }
        case PAD_CONFIG_WAIT_MODE:
            if (request == PAD_RSTAT_COMPLETE) {
                s_padConfig[port] = PAD_CONFIG_ALIGN;
            } else {
                s_padConfig[port] = PAD_CONFIG_DISCOVER;
                s_configRetry[port] = 60;
            }
            break;
        case PAD_CONFIG_ALIGN: {
            /* DualShock's motor slots are 0 (digital) and 1 (analog). */
            int actuators = padInfoAct(port, 0, -1, 0);
            if (actuators < 2 || actuators > 6) {
                s_padConfig[port] = PAD_CONFIG_NO_MOTOR;
                break;
            }
            const char align[6] = {0, 1, -1, -1, -1, -1};
            if (padSetActAlign(port, 0, align) == 1) {
                s_padConfig[port] = PAD_CONFIG_WAIT_ALIGN;
            } else {
                s_configRetry[port] = 60;
            }
            break;
        }
        case PAD_CONFIG_WAIT_ALIGN:
            if (request == PAD_RSTAT_COMPLETE) {
                s_padConfig[port] = PAD_CONFIG_STOP;
            } else {
                s_padConfig[port] = PAD_CONFIG_ALIGN;
                s_configRetry[port] = 60;
            }
            break;
        case PAD_CONFIG_STOP:
            /* A reconnected controller must start with both motors off. */
            if (padSetActDirect(port, 0, s_motorOff) == 1) {
                s_motorOn[port] = false;
                s_stopPending[port] = false;
                s_padConfig[port] = PAD_CONFIG_READY;
                printf("[PS2 PAD] port=%d DualShock rumble ready\n", port);
            } else {
                s_configRetry[port] = 60;
            }
            break;
        case PAD_CONFIG_READY:
            /* If a motor write failed, clear it before advertising again. */
            if (s_stopPending[port] && padSetActDirect(port, 0, s_motorOff) == 1) {
                s_motorOn[port] = false;
                s_stopPending[port] = false;
            }
            break;
        case PAD_CONFIG_NO_MOTOR:
            break;
    }
    return true;
}

static void pad_query(int port, OSContStatus* status) {
    bool present = pad_poll(port);
    status->type = present ? CONT_TYPE_NORMAL : 0;
    status->status = present && s_padConfig[port] == PAD_CONFIG_READY &&
                     !s_stopPending[port] ? CONT_CARD_ON : 0;
    status->errno = present ? 0 : CONT_NO_RESPONSE_ERROR;
}

/* ───────────────────────────────────────────────────────────────── *
 *  osContInit — ÚNICO punto donde se abren los puertos
 * ───────────────────────────────────────────────────────────────── */
/* CORREGIDO: el primer parámetro es OSMesgQueue*, no void*, y
 * osContStartReadData/Query/SetCh devuelven s32, no void.
 * Verificado compilando contra include/ultra64.h. */
s32 osContInit(struct OSMesgQueue* mq, u8* numControllers, OSContStatus* status) {
    (void)mq;

    if (numControllers) *numControllers = 0;

    if (!s_padInitDone) {
        s_padInitDone = (padInit(0) == 1);
    }

    for (int i = 0; i < PS2_PORTS; i++) {
        if (s_padInitDone && !s_portOpen[i]) {
            memset(s_padBuf[i], 0, sizeof(s_padBuf[i]));
            s_portOpen[i] = (padPortOpen(i, 0, s_padBuf[i]) != 0);
            if (s_portOpen[i]) {
                pad_forget(i);
            }
        }
        /* libultra returns a port bitmask, not a controller count. */
        OSContStatus portStatus;
        pad_query(i, &portStatus);
        if (numControllers && !portStatus.errno) *numControllers |= (1u << i);
        if (status) status[i] = portStatus;
    }

    /* El juego pregunta por 4 puertos (los de N64); los 2 que no existen
     * se reportan como desconectados. */
    if (status) {
        for (int i = PS2_PORTS; i < MAX_CONTROLLERS; i++) {
            pad_query(i, &status[i]);
        }
    }

    printf("[osContPad] mascara de puertos: 0x%02x\n",
           numControllers ? *numControllers : 0);
    return 0;
}

/* ───────────────────────────────────────────────────────────────── *
 *  Zona muerta
 * ───────────────────────────────────────────────────────────────── */
static s8 apply_deadzone(int v) {
    if (v > -ANALOG_DEAD_ZONE && v < ANALOG_DEAD_ZONE) return 0;
    if (v >  127) v =  127;
    if (v < -128) v = -128;
    return (s8)v;
}

/* ───────────────────────────────────────────────────────────────── *
 *  osContStartReadData / osContGetReadData
 * ───────────────────────────────────────────────────────────────── */
s32 osContStartReadData(struct OSMesgQueue* mq) {
    (void)mq;
    /* PadMgr waits for SI completion even with synchronous libpad reads. */
    ps2_os_fire_event(PS2_OS_EVENT_SI);
    return 0;
}

void osContGetReadData(OSContPad* pad) {
    if (!pad) return;

    for (int port = 0; port < MAX_CONTROLLERS; port++) {
        pad[port].button  = 0;
        pad[port].stick_x = 0;
        pad[port].stick_y = 0;
        pad[port].errno   = 0;

        if (!pad_poll(port)) {
            pad[port].errno = CONT_NO_RESPONSE_ERROR;
            continue;
        }

        if (padRead(port, 0, &s_padStatus[port]) == 0) {
            pad_stop_motor(port);
            pad[port].errno = CONT_NO_RESPONSE_ERROR;
            continue;
        }

        /* libpad invierte la lógica: 0 = pulsado */
        u32 btns = (~s_padStatus[port].btns) & 0xFFFF;
        u16 n64  = 0;

        /* ── Botones de acción ──────────────────────────────── */
        if (btns & PAD_CROSS)     n64 |= N64_A_BUTTON;
        if (btns & PAD_SQUARE)    n64 |= N64_B_BUTTON;
        if (btns & PAD_L2)        n64 |= N64_Z_TRIG;
        if (btns & PAD_START)     n64 |= N64_START;

        /* ── D-Pad ──────────────────────────────────────────── */
        if (btns & PAD_UP)        n64 |= N64_D_UP;
        if (btns & PAD_DOWN)      n64 |= N64_D_DOWN;
        if (btns & PAD_LEFT)      n64 |= N64_D_LEFT;
        if (btns & PAD_RIGHT)     n64 |= N64_D_RIGHT;

        /* ── Gatillos L / R ─────────────────────────────────── */
        if (btns & PAD_L1)        n64 |= N64_L_TRIG;
        if (btns & PAD_R1)        n64 |= N64_R_TRIG;

        /* ── Botones C desde el stick derecho ───────────────── */
        bool analog = (s_padStatus[port].mode & 0x0F) >= 3;
        int rx = analog ? (int)s_padStatus[port].rjoy_h - 128 : 0;
        int ry = analog ? (int)s_padStatus[port].rjoy_v - 128 : 0;
        if (rx >  C_STICK_THRESHOLD) n64 |= N64_C_RIGHT;
        if (rx < -C_STICK_THRESHOLD) n64 |= N64_C_LEFT;
        if (ry >  C_STICK_THRESHOLD) n64 |= N64_C_DOWN;
        if (ry < -C_STICK_THRESHOLD) n64 |= N64_C_UP;

        /* ── Botones C de respaldo (sin conflictos) ─────────── */
        if (btns & PAD_TRIANGLE)  n64 |= N64_C_UP;
        if (btns & PAD_CIRCLE)    n64 |= N64_C_DOWN;
        if (btns & PAD_R2)        n64 |= N64_C_RIGHT;
        /* L3/R3 se dejan libres: antes chocaban con C-Left/C-Right y
         * hacían que pulsar los sticks disparase acciones. */

        pad[port].button = n64;
        static u16 previousButtons[PS2_PORTS];
        static unsigned buttonTraces;
        if (n64 != previousButtons[port] && buttonTraces++ < 64)
            printf("[PS2 INPUT] port=%d ps2=%04x n64=%04x\n", port, (unsigned)btns, n64);
        previousButtons[port] = n64;

        /* ── Stick izquierdo ────────────────────────────────── */
        /* PS2: 0..255, centro 128. N64: -128..127, Y hacia arriba. */
        pad[port].stick_x = analog ? apply_deadzone((int)s_padStatus[port].ljoy_h - 128) : 0;
        pad[port].stick_y = analog ? apply_deadzone(128 - (int)s_padStatus[port].ljoy_v) : 0;
    }
}

/* ───────────────────────────────────────────────────────────────── *
 *  osContStartQuery / osContGetQuery
 * ───────────────────────────────────────────────────────────────── */
s32 osContStartQuery(struct OSMesgQueue* mq) {
    (void)mq;
    ps2_os_fire_event(PS2_OS_EVENT_SI);
    return 0;
}

void osContGetQuery(OSContStatus* status) {
    if (!status) return;
    for (int i = 0; i < MAX_CONTROLLERS; i++) {
        pad_query(i, &status[i]);
    }
}

s32 osContSetCh(u8 ch) {
    (void)ch;
    return 0;
}

/* ───────────────────────────────────────────────────────────────── *
 *  Acceso para osMotorInit / __osMotorAccess (osMisc.c)
 * ───────────────────────────────────────────────────────────────── */
bool ps2_pad_rumble_ready(int port) {
    return pad_poll(port) && s_padConfig[port] == PAD_CONFIG_READY && !s_stopPending[port];
}

s32 ps2_pad_set_rumble(int port, bool on) {
    if (!ps2_pad_rumble_ready(port)) return 1; /* PFS_ERR_NOPACK */
    if (s_motorOn[port] == on) return 0;

    /* RumbleMgr already turns N64 strength into on/off pulses. */
    const char actuators[6] = {on ? 1 : 0, on ? (char)0xFF : 0, 0, 0, 0, 0};
    if (padSetActDirect(port, 0, actuators) != 1) {
        s_stopPending[port] = true;
        return 4; /* PFS_ERR_CONTRFAIL */
    }
    s_motorOn[port] = on;
    return 0;
}

void ps2_pad_shutdown(void) {
    for (int i = 0; i < PS2_PORTS; i++) {
        if (s_portOpen[i]) {
            /* Do not leave an active command behind when ending PADMAN. */
            padSetActDirect(i, 0, s_motorOff);
            padPortClose(i, 0);
            s_portOpen[i] = false;
            pad_forget(i);
        }
    }
    if (s_padInitDone) {
        padEnd();
        s_padInitDone = false;
    }
}
