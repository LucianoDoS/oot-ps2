/**
 * osMisc.c — Stubs varios de libultra (NUEVO)
 *
 * Reúne las funciones sueltas que el juego referencia y que no encajan
 * en ningún otro archivo. Sin ellas el enlace falla con símbolos
 * indefinidos en cuanto se compila src/code de verdad (que es justo lo
 * que el Makefile anterior no hacía, por eso el problema no se veía).
 *
 * Aquí está también la vibración, que antes vivía en io/ps2_controller.c
 * bajo nombres propios (ps2_controller_set_rumble) que NADIE llamaba.
 * El juego usa osMotorInit / osMotorStart / osMotorStop; el estado de
 * conexión y la negociación de los motores pertenecen a osContPad.c.
 */

#include <kernel.h>
#include <libpad.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include "ps2_os_internal.h"
#include "ps2_pad_internal.h"
#include "ps2_runtime_trace.h"
#ifdef PS2_USB_BOOT
#include "ps2_boot_trace.h"
#endif

typedef void* OSMesg;
struct OSMesgQueue;

/* ───────────────────────────────────────────────────────────────── *
 *  osInitialize — en N64 arranca el sistema operativo.
 *  Aquí todo eso ya lo hizo ps2_init_all() antes de llegar al juego.
 * ───────────────────────────────────────────────────────────────── */
void osInitialize(void) {
}

void __attribute__((noreturn)) ps2_report_fatal(const char* first, const char* second) {
#ifdef PS2_USB_BOOT
    char message[160];
    snprintf(message, sizeof(message), "[PS2 FATAL] %s: %s", first ? first : "", second ? second : "");
    ps2_boot_trace_game_message(message);
#endif
    printf("[PS2 FATAL] %s: %s\n", first ? first : "", second ? second : "");
    fflush(stdout);
    for (;;) SleepThread();
}

/* ───────────────────────────────────────────────────────────────── *
 *  Máscara de interrupciones
 *
 *  El juego la usa para secciones críticas cortas (osSetIntMask(OS_IM_NONE)
 *  ... restaurar). Lo traducimos a DIntr/EIntr del EE, que es el
 *  equivalente más fiel.
 * ───────────────────────────────────────────────────────────────── */
#define OS_IM_NONE  0x00000001
#define OS_IM_ALL   0x003FFF01

static u32 s_intMask = OS_IM_ALL;

u32 osGetIntMask(void) {
    return s_intMask;
}

u32 osSetIntMask(u32 mask) {
    u32 prev = s_intMask;
    s_intMask = mask;

    if (mask == OS_IM_NONE) {
        DIntr();
    } else {
        EIntr();
    }
    return prev;
}

/* ───────────────────────────────────────────────────────────────── *
 *  osSyncPrintf — la macro PRINTF del juego acaba aquí
 * ───────────────────────────────────────────────────────────────── */
s32 osSyncPrintfEnabled = 1;

void osSyncPrintf(const char* fmt, ...) {
    if (!osSyncPrintfEnabled) return;
    va_list args;
    va_start(args, fmt);
#ifdef PS2_USB_BOOT
    if (strncmp(fmt, "[PS2 STATE]", 11) == 0 || strncmp(fmt, "[PS2 FRAME]", 11) == 0
#ifdef PS2_RUNTIME_DIAGNOSTIC
        || strncmp(fmt, "[PS2 ", 5) == 0
#endif
        ) {
        char message[160];
        va_list copy;
        va_copy(copy, args);
        vsnprintf(message, sizeof(message), fmt, copy);
        va_end(copy);
        ps2_boot_trace_game_message(message);
        ps2_runtime_event("%s", message);
    }
#endif
    vprintf(fmt, args);
    va_end(args);
}

/* ───────────────────────────────────────────────────────────────── *
 *  Reset / NMI
 * ───────────────────────────────────────────────────────────────── */
/* CORREGIDO: devuelve s32, no void (ultra64.h:173). Verificado
 * compilando contra los headers reales del juego. */
s32 osAfterPreNMI(void) {
    return 0;
}

/* ───────────────────────────────────────────────────────────────── *
 *  Rumble Pak → motores del DualShock 2
 *
 *  PadMgr descubre el motor mediante CONT_CARD_ON, inicializa OSPfs y
 *  envía los pulsos calculados por RumbleMgr a __osMotorAccess. El dueño
 *  del puerto confirma alineación/conexión antes de aceptar comandos.
 * ───────────────────────────────────────────────────────────────── */
#define PFS_MOTOR_INITIALIZED 0x08
#define PFS_ERR_INVALID       5

/* OSPfs — layout EXACTO de ultra64/pfs.h, size = 0x68.
 * Comprobado con _Static_assert contra el header real. */
typedef struct OSPfs {
    /* 0x00 */ s32                 status;
    /* 0x04 */ struct OSMesgQueue* queue;
    /* 0x08 */ s32                 channel;
    /* 0x0C */ u8                  id[32];
    /* 0x2C */ u8                  label[32];
    /* 0x4C */ s32                 version;
    /* 0x50 */ s32                 dir_size;
    /* 0x54 */ s32                 inode_table;
    /* 0x58 */ s32                 minode_table;
    /* 0x5C */ s32                 dir_table;
    /* 0x60 */ s32                 inodeStartPage;
    /* 0x64 */ u8                  banks;
    /* 0x65 */ u8                  activebank;
} OSPfs;

_Static_assert(sizeof(OSPfs) == 0x68, "OSPfs debe medir 0x68");

s32 osMotorInit(struct OSMesgQueue* ctrlrqueue, OSPfs* pfs, s32 channel) {
    if (!pfs) return PFS_ERR_INVALID;
    memset(pfs, 0, sizeof(*pfs));
    pfs->channel = channel;
    pfs->queue = ctrlrqueue;
    if (!ps2_pad_rumble_ready(channel)) return 1; /* PFS_ERR_NOPACK */
    pfs->status = PFS_MOTOR_INITIALIZED;
    return 0;
}

/* ─────────────────────────────────────────────────────────────────
 *  IMPORTANTE: osMotorStart y osMotorStop NO son funciones.
 *
 *  En ultra64/motor.h son MACROS:
 *      #define osMotorStart(x) __osMotorAccess((x), MOTOR_START)
 *      #define osMotorStop(x)  __osMotorAccess((x), MOTOR_STOP)
 *
 *  O sea que implementar osMotorStart()/osMotorStop() como funciones
 *  (que es lo que hice en la primera pasada) no sirve de nada: el
 *  preprocesador las sustituye antes y el enlazador se queda pidiendo
 *  __osMotorAccess. Lo que hay que implementar es ESTO.
 * ───────────────────────────────────────────────────────────────── */
#define MOTOR_STOP   0
#define MOTOR_START  1

s32 __osMotorAccess(OSPfs* pfs, s32 vibrate) {
    if (!pfs || !(pfs->status & PFS_MOTOR_INITIALIZED) ||
        (vibrate != MOTOR_START && vibrate != MOTOR_STOP)) return PFS_ERR_INVALID;
    return ps2_pad_set_rumble(pfs->channel, vibrate == MOTOR_START);
}

/* ───────────────────────────────────────────────────────────────── *
 *  Controller Pak (osPfs*)
 *
 *  OoT sólo lo usa para detectar el Rumble Pak; no guarda partidas ahí.
 *  Devolvemos "no hay pak de memoria" de forma consistente, que es la
 *  respuesta correcta para un DualShock 2.
 * ───────────────────────────────────────────────────────────────── */
#define PFS_ERR_NOPACK  1

s32 osPfsInitPak(struct OSMesgQueue* queue, OSPfs* pfs, s32 channel) {
    (void)queue;
    if (pfs) {
        memset(pfs, 0, sizeof(OSPfs));
        pfs->channel = channel;
    }
    return PFS_ERR_NOPACK;
}

s32 osPfsFreeBlocks(OSPfs* pfs, s32* leftoverBytes) {
    (void)pfs;
    if (leftoverBytes) *leftoverBytes = 0;
    return PFS_ERR_NOPACK;
}

s32 osPfsAllocateFile(OSPfs* pfs, u16 company_code, u32 game_code,
                      u8* game_name, u8* ext_name, s32 file_size_in_bytes,
                      s32* file_no) {
    (void)pfs; (void)company_code; (void)game_code; (void)game_name;
    (void)ext_name; (void)file_size_in_bytes;
    if (file_no) *file_no = -1;
    return PFS_ERR_NOPACK;
}

s32 osPfsDeleteFile(OSPfs* pfs, u16 company_code, u32 game_code,
                    u8* game_name, u8* ext_name) {
    (void)pfs; (void)company_code; (void)game_code;
    (void)game_name; (void)ext_name;
    return PFS_ERR_NOPACK;
}

s32 osPfsFindFile(OSPfs* pfs, u16 company_code, u32 game_code,
                  u8* game_name, u8* ext_name, s32* file_no) {
    (void)pfs; (void)company_code; (void)game_code;
    (void)game_name; (void)ext_name;
    if (file_no) *file_no = -1;
    return PFS_ERR_NOPACK;
}

s32 osPfsFileState(OSPfs* pfs, s32 file_no, void* state) {
    (void)pfs; (void)file_no; (void)state;
    return PFS_ERR_NOPACK;
}

s32 osPfsReadWriteFile(OSPfs* pfs, s32 file_no, u8 flag, s32 offset,
                       s32 size_in_bytes, u8* data_buffer) {
    (void)pfs; (void)file_no; (void)flag; (void)offset;
    (void)size_in_bytes; (void)data_buffer;
    return PFS_ERR_NOPACK;
}
