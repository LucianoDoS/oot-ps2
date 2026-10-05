/* Read-only data/card check. It does not format or write a memory card. */
#include <kernel.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <libmc.h>
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "ps2_usb_boot.h"
extern int ps2_cache_smoke(void);

/* Read the tail only; do not truncate the game's evidence on the next boot. */
static void show_previous_boot(void) {
    char path[256], tail[3072];
    const char* data = ps2_usb_data_path();
    const char* separator = strrchr(data, '/');
    if (!separator) separator = strchr(data, ':');
    unsigned int prefix = separator ? (unsigned int)(separator - data + 1) : 0;
    if (prefix + sizeof("BOOT.LOG") > sizeof(path)) return;
    memcpy(path, data, prefix);
    memcpy(path + prefix, "BOOT.LOG", sizeof("BOOT.LOG"));
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        scr_printf("\nSin BOOT.LOG previo. Ejecuta el juego primero.\n");
        return;
    }
    int size = lseek(fd, 0, SEEK_END);
    int offset = size > (int)sizeof(tail) - 1 ? size - ((int)sizeof(tail) - 1) : 0;
    int used = 0;
    if (size > 0 && lseek(fd, offset, SEEK_SET) >= 0) {
        while (used < (int)sizeof(tail) - 1) {
            int got = read(fd, tail + used, sizeof(tail) - 1 - used);
            if (got <= 0) break;
            used += got;
        }
    }
    close(fd);
    tail[used] = '\0';
    char* start = tail;
    if (offset) {
        char* newline = strchr(start, '\n');
        start = newline ? newline + 1 : start + used;
    }
    int lines = 0;
    for (const char* p = start; *p; p++) if (*p == '\n') lines++;
    while (lines > 10) {
        char* newline = strchr(start, '\n');
        if (!newline) break;
        start = newline + 1;
        lines--;
    }
    /* The file is text supplied by the USB, never a format string. */
    for (char* p = start; *p; p++)
        if ((*p < 32 || *p > 126) && *p != '\n') *p = '?';
    scr_printf("\nUltimas etapas de OOTPS2.ELF:\n%s", start);
    printf("[PS2 USB PREVIOUS LOG] path=%s bytes=%d\n%s", path, used, start);
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    SifInitRpc(0);
    while (!SifIopReset("", 0)) {}
    while (!SifIopSync()) {}
    SifInitRpc(0);
    SifLoadFileInit();
    SifInitIopHeap();
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
    init_scr();
    scr_printf("OoT PS2 - comprobacion del arranque\n\n");
    scr_printf("Comprobando datos. Espera hasta 10 segundos...\n");
    if (!ps2_usb_prepare(argc > 0 ? argv[0] : NULL)) ps2_usb_fail_screen();
    scr_printf("DATOS OK: %s\n", ps2_usb_data_path());
    int memory_result = ps2_cache_smoke();
    if (memory_result != 0) {
        scr_printf("ERROR MEMORIA: prueba %d\n", memory_result);
        printf("[PS2 CACHE CHECK] FAIL case=%d\n", memory_result);
        for (;;) SleepThread();
    }
    scr_printf("MEMORIA OK: buffers y cache conservados\n");
    int sio = SifLoadModule("rom0:SIO2MAN", 0, NULL);
    int mcm = SifLoadModule("rom0:MCMAN", 0, NULL);
    int mcs = SifLoadModule("rom0:MCSERV", 0, NULL);
    if (sio >= 0 && mcm >= 0 && mcs >= 0 && mcInit(MC_TYPE_MC) >= 0) {
        int type = 0, free_clusters = 0, formatted = 0, result = -1;
        int request = mcGetInfo(0, 0, &type, &free_clusters, &formatted);
        if (request >= 0) mcSync(MC_WAIT, NULL, &result);
        scr_printf("Memory card 1: tipo=%d formato=%d libre=%d KB\n", type, formatted, free_clusters);
        printf("[PS2 USB CARD] request=%d result=%d type=%d formatted=%d free=%d\n",
               request, result, type, formatted, free_clusters);
    } else scr_printf("No pude consultar la memory card.\n");
    show_previous_boot();
    scr_printf("\nEste diagnostico NO prueba mando, sonido ni guardado.\n");
    scr_printf("Reinicia y ejecuta OOTPS2.ELF para probar el juego.\n");
    printf("[PS2 USB DIAGNOSTIC] PASS data=1 cardWrites=0 hardwareValidated=0\n");
    for (;;) SleepThread();
}
