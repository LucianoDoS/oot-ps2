/* Visible startup milestones for a console without an EE debug connection. */
#include <kernel.h>
#include <debug.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "ps2_boot_trace.h"

#define TRACE_LINES 96
#define TRACE_BYTES 160
static char sLines[TRACE_LINES][TRACE_BYTES];
static char sPath[256];
static unsigned int sCount;
static int sScreen, sFile, sMutex = -1;
static int sFirstFrame;

static int write_all(int fd, const char* text) {
    unsigned int size = strlen(text);
    while (size) {
        int wrote = write(fd, text, size);
        if (wrote <= 0) return 0;
        text += wrote;
        size -= wrote;
    }
    return 1;
}

/* Close every record: a reset after a blocked stage must retain that stage.
 * Call only in normal thread context, never from an interrupt handler. */
static void append_file(const char* line) {
    int fd = open(sPath, O_WRONLY | O_APPEND);
    if (fd < 0) {
        sFile = 0;
        printf("[PS2 BOOT TRACE] Cannot append %s\n", sPath);
        return;
    }
    int okay = write_all(fd, line) && write_all(fd, "\n");
    int closed = close(fd);
    if (!okay || closed < 0) {
        sFile = 0;
        printf("[PS2 BOOT TRACE] Cannot complete record in %s\n", sPath);
    }
}

void ps2_boot_trace_init(void) {
    ee_sema_t sema;
    memset(&sema, 0, sizeof(sema));
    sema.init_count = sema.max_count = 1;
    sMutex = CreateSema(&sema);
    init_scr();
    sScreen = 1;
    ps2_boot_trace_stage("MAIN: ejecutable iniciado");
}

void ps2_boot_trace_stage(const char* message) {
    if (sMutex >= 0) WaitSema(sMutex);
    if (sCount < TRACE_LINES) {
        char* line = sLines[sCount];
        snprintf(line, TRACE_BYTES, "%02u %s", sCount + 1, message);
        /* Each stored record is one ASCII line, even for osSyncPrintf. */
        line[strcspn(line, "\r\n")] = '\0';
        sCount++;
        printf("[PS2 BOOT TRACE] %s\n", line);
        if (sFile) append_file(line);
        if (sScreen) {
            scr_clear();
            scr_setXY(0, 0);
            scr_printf("OoT PS2 - diagnostico de arranque\n\n");
            scr_printf("Ultima etapa:\n%s\n\n", line);
            if (sCount > 1) scr_printf("Anterior:\n%s\n\n", sLines[sCount - 2]);
            scr_printf("Si se bloquea, anota o fotografia esta etapa.\n");
            scr_printf("Tras activar el video: reinicia y abre BOOTTEST.ELF.\n");
        }
    }
    if (sMutex >= 0) SignalSema(sMutex);
}

void ps2_boot_trace_result(const char* name, int result) {
    char message[TRACE_BYTES];
    snprintf(message, sizeof(message), "%s resultado=%d", name, result);
    ps2_boot_trace_stage(message);
}

void ps2_boot_trace_file_ready(const char* data_path) {
    const char* separator = strrchr(data_path, '/');
    if (!separator) separator = strchr(data_path, ':');
    unsigned int prefix = separator ? (unsigned int)(separator - data_path + 1) : 0;
    if (prefix + sizeof("BOOT.LOG") > sizeof(sPath)) return;
    memcpy(sPath, data_path, prefix);
    memcpy(sPath + prefix, "BOOT.LOG", sizeof("BOOT.LOG"));
    int fd = open(sPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        ps2_boot_trace_stage("BOOT.LOG: no pude crear el registro en USB");
        return;
    }
    int okay = write_all(fd, "OoT PS2 boot trace v1 " __DATE__ " " __TIME__ "\n");
    for (unsigned int i = 0; okay && i < sCount; i++)
        okay = write_all(fd, sLines[i]) && write_all(fd, "\n");
    int closed = close(fd);
    sFile = okay && closed >= 0;
    ps2_boot_trace_stage(sFile ? "BOOT.LOG: registro activo" : "BOOT.LOG: fallo al escribir el registro");
}

void ps2_boot_trace_screen_off(void) {
    ps2_boot_trace_stage("VIDEO: cediendo la pantalla al juego");
    sScreen = 0;
}

void ps2_boot_trace_game_message(const char* message) {
    if (strncmp(message, "[PS2 STATE]", 11) == 0) {
        ps2_boot_trace_stage(message);
    } else if (!sFirstFrame && strncmp(message, "[PS2 FRAME]", 11) == 0 && strstr(message, "frame 1\n")) {
        sFirstFrame = 1;
        ps2_boot_trace_stage(message);
    } else if (strncmp(message, "[PS2 FATAL]", 11) == 0) {
        ps2_boot_trace_stage(message);
    }
}
