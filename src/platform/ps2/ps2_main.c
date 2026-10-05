/**
 * ps2_main.c — Punto de entrada del ELF de PS2
 *
 * ══════════════════════════════════════════════════════════════════════
 *  QUÉ ESTABA MAL EN LA VERSIÓN ANTERIOR
 * ══════════════════════════════════════════════════════════════════════
 *
 * 1. Llamaba a Game_Main(), una función que no existe (ver ps2_boot.c).
 *
 * 2. Llamaba a Sched_Init() sin argumentos cuando la real lleva seis.
 *
 * 3. ORDEN DE MÓDULOS IOP MAL:
 *      · Cargaba rom0:PADMAN sin cargar antes SIO2MAN, del que depende.
 *      · Llamaba a audsrv_init() sin haber cargado los IRX de audio
 *        (libsd + audsrv). audsrv_init() no carga nada por sí sola:
 *        sólo abre el canal RPC con un módulo que tiene que estar YA
 *        en el IOP.
 *      · mcInit() (en io/ps2_controller.c) necesitaba MCMAN/MCSERV, que
 *        no se cargaban en ningún sitio.
 *    Los tres subsistemas fallaban en silencio.
 *
 * 4. RESET DEL IOP EN EL MOMENTO EQUIVOCADO:
 *    hacía SifInitRpc(0) y dmaKit_init() y DESPUÉS reseteaba el IOP
 *    dentro de ps2_init_iop(). El reset invalida el estado del RPC, así
 *    que todo lo anterior quedaba en nada. El reset va PRIMERO.
 *
 * 5. Inicializaba los mandos por duplicado (aquí y en osContPad.c) con
 *    buffers DMA distintos sobre los mismos puertos.
 *
 * 6. audsrv_init() se llamaba dos veces (aquí y en el driver de audio).
 *
 * 7. No llamaba a rdp_to_gs_init(), así que el renderer se quedaba con
 *    el puntero del GS a NULL.
 */

#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <sbv_patches.h>
#include <libpad.h>
#include <libmc.h>
#include <audsrv.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <string.h>
#include <stdio.h>

#include "ps2_init.h"
#include "ps2/ps2_actor_overlays.h"
#include "ps2_types.h"
#include "rdp_to_gs.h"
#ifdef PS2_USB_BOOT
#include "ps2_usb_boot.h"
static const char* s_usbLaunchPath;
#endif

/* ───────────────────────────────────────────────────────────────── *
 *  Contexto global del GS
 * ───────────────────────────────────────────────────────────────── */
GSGLOBAL* g_gsGlobal = NULL;
GSTEXTURE g_framebuffer[2];

extern void ps2_boot_game(void);
extern void ps2_boot_shutdown(void);
extern void ps2_pad_shutdown(void);
extern void ps2_rom_shutdown(void);
extern s32  osContInit(void* siHandle, u8* numControllers, void* status);

/* ───────────────────────────────────────────────────────────────── *
 *  reset_iop() — reinicia el IOP y deja el RPC listo
 *
 *  Tiene que ser LO PRIMERO de todo. Después de un reset del IOP hay
 *  que rehacer SifInitRpc.
 * ───────────────────────────────────────────────────────────────── */
static void reset_iop(void) {
    SifInitRpc(0);

    /* Reset completo del IOP */
    while (!SifIopReset("", 0)) {}
    while (!SifIopSync()) {}

    SifInitRpc(0);
    SifLoadFileInit();
    SifInitIopHeap();

    /* Parches que permiten cargar IRX desde el EE (necesario para
     * audsrv, que no está en la ROM de la consola) */
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
}

/* ───────────────────────────────────────────────────────────────── *
 *  ps2_init_iop() — carga los módulos del IOP EN ORDEN
 *
 *  Las dependencias importan y antes no se respetaban:
 *      SIO2MAN  ← base de mandos y memory card
 *      PADMAN   ← necesita SIO2MAN
 *      MCMAN    ← necesita SIO2MAN
 *      MCSERV   ← necesita MCMAN
 *      LIBSD    ← base de audio
 *      AUDSRV   ← necesita LIBSD
 * ───────────────────────────────────────────────────────────────── */
void ps2_init_iop(void) {
    int ret;

    /* ── Mandos ─────────────────────────────────────────────────── */
    ret = SifLoadModule("rom0:SIO2MAN", 0, NULL);
    if (ret < 0) printf("[OoT-PS2] AVISO: SIO2MAN fallo (%d)\n", ret);

    ret = SifLoadModule("rom0:PADMAN", 0, NULL);
    if (ret < 0) printf("[OoT-PS2] AVISO: PADMAN fallo (%d)\n", ret);

    /* ── Memory card ────────────────────────────────────────────── */
    ret = SifLoadModule("rom0:MCMAN", 0, NULL);
    if (ret < 0) printf("[OoT-PS2] AVISO: MCMAN fallo (%d)\n", ret);

    ret = SifLoadModule("rom0:MCSERV", 0, NULL);
    if (ret < 0) printf("[OoT-PS2] AVISO: MCSERV fallo (%d)\n", ret);

    /* ── Audio ──────────────────────────────────────────────────── */
    /* Use the SDK's sound driver with audsrv instead of depending on the
     * BIOS version's block-DMA implementation. Keep ROM fallback available. */
    extern unsigned char freesd_irx[];
    extern unsigned int size_freesd_irx;
    int soundRv = 0;
    ret = SifExecModuleBuffer(freesd_irx, size_freesd_irx, 0, NULL, &soundRv);
    if (ret < 0 || soundRv != 0) {
        printf("[PS2 SOUND MODULE] SDK failed ret=%d rv=%d; ROM fallback\n", ret, soundRv);
        ret = SifLoadModule("rom0:LIBSD", 0, NULL);
        if (ret < 0) printf("[OoT-PS2] AVISO: LIBSD fallo (%d)\n", ret);
    } else printf("[PS2 SOUND MODULE] SDK libsd loaded id=%d\n", ret);

    /* audsrv.irx NO está en la ROM de la consola: lo trae ps2sdk y se
     * enlaza dentro del ELF. Por eso hacen falta los parches sbv. */
    extern unsigned char audsrv_irx[];
    extern unsigned int  size_audsrv_irx;
    int rv = 0;
    ret = SifExecModuleBuffer(audsrv_irx, size_audsrv_irx, 0, NULL, &rv);
    if (ret < 0 || rv != 0) {
        printf("[OoT-PS2] AVISO: audsrv.irx fallo (%d, rv=%d). Sin sonido.\n", ret, rv);
    }

    printf("[OoT-PS2] Modulos IOP cargados.\n");
}

/* ───────────────────────────────────────────────────────────────── *
 *  ps2_init_gs() — Graphics Synthesizer
 * ───────────────────────────────────────────────────────────────── */
void ps2_init_gs(void) {
    g_gsGlobal = gsKit_init_global();
    if (!g_gsGlobal) {
        printf("[OoT-PS2] ERROR FATAL: gsKit_init_global fallo\n");
        return;
    }

    g_gsGlobal->Mode      = PS2_VIDEO_MODE;
    g_gsGlobal->Interlace = PS2_INTERLACED;
    g_gsGlobal->Field     = PS2_FIELD;
    g_gsGlobal->Width     = PS2_SCREEN_W;
    g_gsGlobal->Height    = PS2_SCREEN_H;
    /* OoT's native framebuffer is RGB5551. CT24 occupies 32-bit GS words;
     * CT16 frees over 1 MiB for textures while retaining 24-bit Z precision. */
    /* CT16S keeps RGB5551 storage without gsKit's CT16 -> Z16 override. */
    g_gsGlobal->PSM       = GS_PSM_CT16S;
    g_gsGlobal->PSMZ      = GS_PSMZ_24;
    g_gsGlobal->ZBuffering       = GS_SETTING_ON;
    g_gsGlobal->DoubleBuffering  = GS_SETTING_ON;
    g_gsGlobal->PrimAlphaEnable  = GS_SETTING_ON;
    g_gsGlobal->PrimAAEnable     = GS_SETTING_OFF;

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_FROMSPR);
    dmaKit_chan_init(DMA_CHANNEL_TOSPR);

    gsKit_init_screen(g_gsGlobal);
    printf("[PS2 GS FORMAT] color=%02x depth=%02x textureStart=%08x\n",
           g_gsGlobal->PSM, g_gsGlobal->PSMZ, g_gsGlobal->CurrentPointer);
    gsKit_mode_switch(g_gsGlobal, GS_ONESHOT);

    gsKit_clear(g_gsGlobal, GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0));
    gsKit_queue_exec(g_gsGlobal);
    gsKit_sync_flip(g_gsGlobal);

    /* ESTO FALTABA: sin esta llamada g_rdpState.gs se quedaba a NULL y
     * el primer rdp_to_gs_flip() reventaba. */
    rdp_to_gs_init(g_gsGlobal);

    printf("[OoT-PS2] GS listo: %dx%d NTSC\n", PS2_SCREEN_W, PS2_SCREEN_H);
}

/* ───────────────────────────────────────────────────────────────── *
 *  ps2_init_pad()
 *
 *  Antes abría los puertos por su cuenta y chocaba con osContInit().
 *  Ahora delega en osContInit(), que es el único dueño de los puertos.
 * ───────────────────────────────────────────────────────────────── */
void ps2_init_pad(void) {
    u8 numControllers = 0;
    osContInit(NULL, &numControllers, NULL);
}

/* ───────────────────────────────────────────────────────────────── *
 *  ps2_init_all()
 * ───────────────────────────────────────────────────────────────── */
void ps2_init_all(void) {
    /* 1. IOP primero: el reset invalida todo lo anterior */
    reset_iop();

    /* 2. Módulos del IOP, en orden de dependencias */
    ps2_init_iop();

    /* 3. Memory card (necesita MCMAN/MCSERV ya cargados) */
    mcInit(MC_TYPE_MC);

#ifdef PS2_USB_BOOT
    if (!ps2_usb_prepare(s_usbLaunchPath)) ps2_usb_fail_screen();
#endif

    /* 4. Gráficos */
    ps2_init_gs();

    /* 5. Mandos */
    ps2_init_pad();

    printf("[OoT-PS2] Sistema inicializado.\n");
}

/* ───────────────────────────────────────────────────────────────── *
 *  ps2_shutdown()
 * ───────────────────────────────────────────────────────────────── */
void ps2_shutdown(void) {
    ps2_boot_shutdown();
    ps2_pad_shutdown();
    ps2_rom_shutdown();
    audsrv_quit();
    SifExitRpc();
    printf("[OoT-PS2] Apagado completo.\n");
}

/* ───────────────────────────────────────────────────────────────── *
 *  main()
 * ───────────────────────────────────────────────────────────────── */
int main(int argc, char* argv[]) {
#ifdef PS2_USB_BOOT
    s_usbLaunchPath = argc > 0 ? argv[0] : NULL;
#else
    (void)argc;
    (void)argv;
#endif

    SifInitRpc(0);
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("\n=================================\n");
    printf("  OoT PS2 Port - Build " __DATE__ "\n");
    printf("=================================\n\n");

    if (!ps2_actor_overlays_init()) {
        printf("[OoT-PS2] Cannot preserve initial actor overlay data\n");
        return 1;
    }
    ps2_init_all();

    /* Cede el control a la cadena de arranque real del juego
     * (bootproc -> idle -> Main -> Graph). Ver ps2_boot.c. */
    ps2_boot_game();

    ps2_shutdown();
    return 0;
}
