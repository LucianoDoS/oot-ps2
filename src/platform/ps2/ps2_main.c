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
#include "ps2_runtime_trace.h"
#include "ps2/ps2_actor_overlays.h"
#include "ps2_types.h"
#include "rdp_to_gs.h"
#ifdef PS2_EXTERNAL_ASSETS
#include <debug.h>
#include "ps2_release_data.h"
#endif
#ifdef PS2_USB_BOOT
#include "ps2_usb_boot.h"
#include "ps2_boot_trace.h"
static const char* s_usbLaunchPath;
#define BOOT_STAGE(message) ps2_boot_trace_stage(message)
#define BOOT_RESULT(name, result) ps2_boot_trace_result(name, result)
#else
#define BOOT_STAGE(message) ((void)0)
#define BOOT_RESULT(name, result) ((void)0)
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
    BOOT_STAGE("IOP: iniciando RPC");
    SifInitRpc(0);

    /* Reset completo del IOP */
    BOOT_STAGE("IOP: solicitando reinicio");
    while (!SifIopReset("", 0)) {}
    BOOT_STAGE("IOP: esperando sincronizacion");
    while (!SifIopSync()) {}

    BOOT_STAGE("IOP: restaurando RPC y parches");
    SifInitRpc(0);
    SifLoadFileInit();
    SifInitIopHeap();

    /* Parches que permiten cargar IRX desde el EE (necesario para
     * audsrv, que no está en la ROM de la consola) */
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
    BOOT_STAGE("IOP: reinicio terminado");
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
    BOOT_STAGE("IOP: cargando SIO2MAN");
    ret = SifLoadModule("rom0:SIO2MAN", 0, NULL);
    BOOT_RESULT("SIO2MAN", ret);
    if (ret < 0) printf("[OoT-PS2] AVISO: SIO2MAN fallo (%d)\n", ret);

    BOOT_STAGE("IOP: cargando PADMAN");
    ret = SifLoadModule("rom0:PADMAN", 0, NULL);
    BOOT_RESULT("PADMAN", ret);
    if (ret < 0) printf("[OoT-PS2] AVISO: PADMAN fallo (%d)\n", ret);

    /* ── Memory card ────────────────────────────────────────────── */
    BOOT_STAGE("IOP: cargando MCMAN");
    ret = SifLoadModule("rom0:MCMAN", 0, NULL);
    BOOT_RESULT("MCMAN", ret);
    if (ret < 0) printf("[OoT-PS2] AVISO: MCMAN fallo (%d)\n", ret);

    BOOT_STAGE("IOP: cargando MCSERV");
    ret = SifLoadModule("rom0:MCSERV", 0, NULL);
    BOOT_RESULT("MCSERV", ret);
    if (ret < 0) printf("[OoT-PS2] AVISO: MCSERV fallo (%d)\n", ret);

    /* ── Audio ──────────────────────────────────────────────────── */
    /* Use the SDK's sound driver with audsrv instead of depending on the
     * BIOS version's block-DMA implementation. Keep ROM fallback available. */
    extern unsigned char freesd_irx[];
    extern unsigned int size_freesd_irx;
    int soundRv = 0;
    BOOT_STAGE("IOP: cargando FREESD");
    ret = SifExecModuleBuffer(freesd_irx, size_freesd_irx, 0, NULL, &soundRv);
    BOOT_RESULT("FREESD id", ret);
    BOOT_RESULT("FREESD rv", soundRv);
    if (ret < 0 || soundRv != 0) {
        printf("[PS2 SOUND MODULE] SDK failed ret=%d rv=%d; ROM fallback\n", ret, soundRv);
        BOOT_STAGE("IOP: cargando LIBSD de ROM");
        ret = SifLoadModule("rom0:LIBSD", 0, NULL);
        BOOT_RESULT("LIBSD", ret);
        if (ret < 0) printf("[OoT-PS2] AVISO: LIBSD fallo (%d)\n", ret);
    } else printf("[PS2 SOUND MODULE] SDK libsd loaded id=%d\n", ret);

    /* audsrv.irx NO está en la ROM de la consola: lo trae ps2sdk y se
     * enlaza dentro del ELF. Por eso hacen falta los parches sbv. */
    extern unsigned char audsrv_irx[];
    extern unsigned int  size_audsrv_irx;
    int rv = 0;
    BOOT_STAGE("IOP: cargando AUDSRV");
    ret = SifExecModuleBuffer(audsrv_irx, size_audsrv_irx, 0, NULL, &rv);
    BOOT_RESULT("AUDSRV id", ret);
    BOOT_RESULT("AUDSRV rv", rv);
    if (ret < 0 || rv != 0) {
        printf("[OoT-PS2] AVISO: audsrv.irx fallo (%d, rv=%d). Sin sonido.\n", ret, rv);
    }

    printf("[OoT-PS2] Modulos IOP cargados.\n");
}

/* ───────────────────────────────────────────────────────────────── *
 *  ps2_init_gs() — Graphics Synthesizer
 * ───────────────────────────────────────────────────────────────── */
void ps2_init_gs(void) {
    BOOT_STAGE("VIDEO: reservando contexto GS");
    g_gsGlobal = gsKit_init_global();
    if (!g_gsGlobal) {
        printf("[OoT-PS2] ERROR FATAL: gsKit_init_global fallo\n");
        BOOT_STAGE("ERROR: gsKit_init_global fallo");
        return;
    }

    g_gsGlobal->Mode      = PS2_VIDEO_MODE;
    g_gsGlobal->Interlace = PS2_INTERLACED;
    g_gsGlobal->Field     = PS2_FIELD;
    g_gsGlobal->Width     = PS2_SCREEN_W;
    g_gsGlobal->Height    = PS2_SCREEN_H;
#ifdef PS2_USB_VIDEO_224
    /* Keep NTSC interlaced output, but duplicate each rendered line.
     * This comparison mode removes single-line interlace twitter without
     * requiring a 240p/480p signal from the user's AV converter. */
    g_gsGlobal->Height = 224;
#endif
    /* OoT's native framebuffer is RGB5551. CT24 occupies 32-bit GS words;
     * CT16 frees over 1 MiB for textures while retaining 24-bit Z precision. */
    /* CT16S keeps RGB5551 storage without gsKit's CT16 -> Z16 override. */
    g_gsGlobal->PSM       = GS_PSM_CT16S;
    g_gsGlobal->PSMZ      = GS_PSMZ_24;
    g_gsGlobal->ZBuffering       = GS_SETTING_ON;
    g_gsGlobal->DoubleBuffering  = GS_SETTING_ON;
    g_gsGlobal->PrimAlphaEnable  = GS_SETTING_ON;
    g_gsGlobal->PrimAAEnable     = GS_SETTING_OFF;

    BOOT_STAGE("VIDEO: inicializando DMA");
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_FROMSPR);
    dmaKit_chan_init(DMA_CHANNEL_TOSPR);

#ifdef PS2_USB_BOOT
    ps2_boot_trace_screen_off();
#endif
#ifdef PS2_USB_VIDEO_224
    BOOT_STAGE("VIDEO: NTSC interlazado, render 640x224, lineas duplicadas");
#else
    BOOT_STAGE("VIDEO: gsKit_init_screen NTSC 640x448 interlazado");
#endif
    gsKit_init_screen(g_gsGlobal);
    printf("[PS2 VIDEO] render=%dx%d displayHeight=%d interlace=%d field=%d magV=%d\n",
        g_gsGlobal->Width, g_gsGlobal->Height, g_gsGlobal->DH, g_gsGlobal->Interlace,
        g_gsGlobal->Field, g_gsGlobal->MagV);
    BOOT_STAGE("VIDEO: pantalla inicializada");
    printf("[PS2 GS FORMAT] color=%02x depth=%02x textureStart=%08x\n",
           g_gsGlobal->PSM, g_gsGlobal->PSMZ, g_gsGlobal->CurrentPointer);
    gsKit_mode_switch(g_gsGlobal, GS_ONESHOT);

    gsKit_clear(g_gsGlobal, GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0));
    BOOT_STAGE("VIDEO: enviando primer borrado");
    gsKit_queue_exec(g_gsGlobal);
    BOOT_STAGE("VIDEO: esperando primer sync_flip");
    gsKit_sync_flip(g_gsGlobal);

    /* ESTO FALTABA: sin esta llamada g_rdpState.gs se quedaba a NULL y
     * el primer rdp_to_gs_flip() reventaba. */
    BOOT_STAGE("VIDEO: inicializando renderer");
    rdp_to_gs_init(g_gsGlobal);
    BOOT_STAGE("VIDEO: listo");

    printf("[OoT-PS2] GS listo: %dx%d NTSC\n", g_gsGlobal->Width, g_gsGlobal->Height);
}

/* ───────────────────────────────────────────────────────────────── *
 *  ps2_init_pad()
 *
 *  Antes abría los puertos por su cuenta y chocaba con osContInit().
 *  Ahora delega en osContInit(), que es el único dueño de los puertos.
 * ───────────────────────────────────────────────────────────────── */
void ps2_init_pad(void) {
    BOOT_STAGE("MANDO: iniciando puertos");
    u8 numControllers = 0;
    s32 result = osContInit(NULL, &numControllers, NULL);
    BOOT_RESULT("MANDO init", result);
    (void)result;
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
    BOOT_STAGE("TARJETA: iniciando RPC");
    int mcResult = mcInit(MC_TYPE_MC);
    BOOT_RESULT("TARJETA init", mcResult);
    (void)mcResult;

#ifdef PS2_USB_BOOT
    if (!ps2_usb_prepare(s_usbLaunchPath)) ps2_usb_fail_screen();
    ps2_boot_trace_file_ready(ps2_usb_data_path());
    ps2_runtime_trace_init(ps2_usb_data_path());
#endif

    /* 4. Gráficos */
    ps2_init_gs();

    /* 5. Mandos */
    ps2_init_pad();

    printf("[OoT-PS2] Sistema inicializado.\n");
    BOOT_STAGE("SISTEMA: inicializado");
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

    setvbuf(stdout, NULL, _IONBF, 0);
#ifdef PS2_USB_BOOT
    ps2_boot_trace_init();
    ps2_usb_set_progress(ps2_boot_trace_stage);
    BOOT_RESULT("RAM EE bytes", GetMemorySize());
#endif
    SifInitRpc(0);

    printf("\n=================================\n");
    printf("  OoT PS2 Port - Build " __DATE__ "\n");
    printf("=================================\n\n");

#ifdef PS2_EXTERNAL_ASSETS
    /* Restore native .data before actor overlays snapshot their initial state. */
    ps2_init_all();
    if (!ps2_release_data_load(ps2_usb_data_path())) {
        init_scr();
        scr_printf("OoT PS2: PS2DATA.DAT falta o no corresponde a este ELF.\n");
        scr_printf("Genera tus datos locales y copia PS2DATA.DAT junto a OOT.Z64.\n");
        for (;;) SleepThread();
    }
#endif
    BOOT_STAGE("ACTORES: conservando datos iniciales");
    if (!ps2_actor_overlays_init()) {
        printf("[OoT-PS2] Cannot preserve initial actor overlay data\n");
        BOOT_STAGE("ERROR: no hay memoria para los datos de actores");
#ifdef PS2_USB_BOOT
        for (;;) SleepThread();
#endif
        return 1;
    }
    BOOT_STAGE("ACTORES: datos iniciales conservados");
#ifndef PS2_EXTERNAL_ASSETS
    ps2_init_all();
#endif

    /* Cede el control a la cadena de arranque real del juego
     * (bootproc -> idle -> Main -> Graph). Ver ps2_boot.c. */
    ps2_boot_game();

    ps2_shutdown();
    return 0;
}
