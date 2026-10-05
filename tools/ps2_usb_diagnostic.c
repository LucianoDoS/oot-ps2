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
#include "ps2_usb_boot.h"

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
    scr_printf("\nEste diagnostico NO prueba mando, sonido ni guardado.\n");
    scr_printf("Reinicia y ejecuta OOTPS2.ELF para probar el juego.\n");
    printf("[PS2 USB DIAGNOSTIC] PASS data=1 cardWrites=0 hardwareValidated=0\n");
    for (;;) SleepThread();
}
