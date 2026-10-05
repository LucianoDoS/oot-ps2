/**
 * ps2_boot.c — Cadena de arranque del juego (NUEVO)
 *
 * ══════════════════════════════════════════════════════════════════════
 *  EL PROBLEMA QUE RESUELVE ESTE ARCHIVO
 * ══════════════════════════════════════════════════════════════════════
 *
 * El ps2_main.c anterior hacía esto:
 *
 *     extern void Game_Main(void);
 *     ...
 *     Sched_Init();     // con 0 argumentos
 *     Game_Main();
 *
 * Dos problemas graves:
 *
 *   · Game_Main() NO EXISTE. No está en el decomp, ni en src/code, ni en
 *     ningún sitio. Se lo inventó. El enlace habría fallado.
 *
 *   · Sched_Init() sí existe, pero su firma real es:
 *         void Sched_Init(Scheduler*, void* stack, OSPri, u8 viModeType,
 *                         UNK_TYPE, IrqMgr*);
 *     Llamarla sin argumentos hace que lea basura de los registros y
 *     escriba a través de un puntero inválido: crash inmediato.
 *
 * ══════════════════════════════════════════════════════════════════════
 *  LA CADENA DE ARRANQUE REAL DE OoT
 * ══════════════════════════════════════════════════════════════════════
 *
 *   boot.s  →  bootproc()          [src/boot/boot_main.c]
 *                 · osInitialize()
 *                 · gCartHandle = osCartRomInit()
 *                 · crea y arranca el hilo IDLE
 *
 *              Idle_ThreadEntry()  [src/boot/idle.c]
 *                 · configura el vídeo
 *                 · crea el PI manager y el DMA manager
 *                 · crea y arranca el hilo MAIN
 *                 · se queda en bucle a la prioridad más baja
 *
 *              Main()              [src/code/main.c]
 *                 · inicializa el heap del sistema
 *                 · IrqMgr_Init, Sched_Init, AudioMgr_Init, PadMgr_Init
 *                 · lanza Graph_ThreadEntry ← el bucle del juego
 *
 * Nadie llama a "Game_Main". El bucle del juego vive dentro de
 * Graph_ThreadEntry, y para llegar ahí hay que pasar por toda la
 * cadena, porque cada paso inicializa cosas que el siguiente necesita.
 *
 * Lo que hace este archivo es arrancar esa cadena tal cual, dejando que
 * el propio código del juego haga su trabajo en vez de saltárselo.
 */

#include <kernel.h>
#include <string.h>
#include <stdio.h>

#include "ps2_types.h"
#include "ps2_init.h"
#include "ps2_runtime_trace.h"
#ifdef PS2_USB_BOOT
#include "ps2_boot_trace.h"
#define BOOT_STAGE(message) ps2_boot_trace_stage(message)
#define BOOT_RESULT(name, result) ps2_boot_trace_result(name, result)
#else
#define BOOT_STAGE(message) ((void)0)
#define BOOT_RESULT(name, result) ((void)0)
#endif

/* ───────────────────────────────────────────────────────────────── *
 *  Funciones del propio juego
 *
 *  Se declaran aquí con `extern` en vez de incluir sus cabeceras para
 *  no arrastrar todo el árbol de headers de N64 a la capa de PS2.
 * ───────────────────────────────────────────────────────────────── */
extern void bootproc(void);            /* src/boot/boot_main.c */

/* ───────────────────────────────────────────────────────────────── *
 *  Funciones de nuestra capa
 * ───────────────────────────────────────────────────────────────── */
extern void ps2_thread_init(void);
extern void ps2_mesgqueue_init(void);
extern void ps2_event_init(void);
extern void ps2_timer_init(void);
extern void ps2_timer_tick(void);
extern void ps2_vi_init(void);
extern void ps2_vi_retrace(void);
extern void ps2_heap_init(void);
extern void ps2_save_init(void);
extern void PS2Audio_Init(void);
extern void PS2Audio_Update(void);
extern void rdp_to_gs_flip(void);

/* ───────────────────────────────────────────────────────────────── *
 *  Latido del sistema
 *
 *  En N64 el VI genera una interrupción por frame y de ahí cuelga todo:
 *  el retrace del scheduler, los temporizadores, el audio...
 *
 *  En PS2 enganchamos el mismo latido al VSync del GS. Esta función la
 *  llama el hilo de vblank creado más abajo.
 * ───────────────────────────────────────────────────────────────── */
static int  s_vblankSema  = -1;
static int  s_vblankThread = -1;
static u8   s_vblankStack[8192] __attribute__((aligned(64)));
static volatile bool s_running = true;

static u64 s_frameCount = 0;

/**
 * ps2_vblank_tick() — todo lo que debe ocurrir una vez por frame
 */
void ps2_vblank_tick(void) {
    s_frameCount++;

    /* 1. El VI "consume" el swap pendiente y avisa del retrace.
     *    Esto es el latido del scheduler de OoT. */
    ps2_vi_retrace();

    /* 2. Temporizadores software.
     *    ANTES: existía osTimer_tick() pero no la llamaba nadie, así que
     *    ningún temporizador del juego llegó a dispararse jamás. */
    ps2_timer_tick();

    /* 3. Audio: rellenar el buffer de reproducción. */
    PS2Audio_Update();
}

/* Manejador de VSync del GS */
static int vblank_handler(int cause) {
    (void)cause;
    if (s_vblankSema >= 0) {
        iSignalSema(s_vblankSema);
    }
    ExitHandler();
    return 0;
}

/* Hilo que atiende el vblank fuera del contexto de interrupción.
 * Importante: dentro de una interrupción no se puede llamar a casi
 * nada (ni printf, ni semáforos bloqueantes, ni audsrv), así que la
 * interrupción sólo señaliza y el trabajo real se hace aquí. */
static void vblank_thread(void* arg) {
    (void)arg;
    while (s_running) {
        WaitSema(s_vblankSema);
        ps2_vblank_tick();
    }
    ExitDeleteThread();
}

static void start_vblank_thread(void) {
    BOOT_STAGE("VBLANK: creando semaforo y hilo");
    ee_sema_t sema;
    memset(&sema, 0, sizeof(sema));
    sema.init_count = 0;
    sema.max_count  = 1;      /* si nos retrasamos, se pierden frames,
                                 que es mejor que acumular retraso */
    s_vblankSema = CreateSema(&sema);
    BOOT_RESULT("VBLANK semaforo", s_vblankSema);

    ee_thread_t th;
    memset(&th, 0, sizeof(th));
    th.func             = (void*)vblank_thread;
    th.stack            = s_vblankStack;
    th.stack_size       = sizeof(s_vblankStack);
    th.gp_reg           = &_gp;
    th.initial_priority = 8;   /* alta: es el reloj del sistema */

    s_vblankThread = CreateThread(&th);
    BOOT_RESULT("VBLANK hilo", s_vblankThread);
    if (s_vblankThread >= 0) {
        StartThread(s_vblankThread, NULL);
    }

    /* Enganchar al VSync del GS (INTC_VBLANK_START = 2) */
    int handler = AddIntcHandler(2, vblank_handler, 0);
    BOOT_RESULT("VBLANK handler", handler);
    (void)handler;
    EnableIntc(2);
    BOOT_STAGE("VBLANK: activo");

    printf("[boot] Latido de vblank en marcha\n");
}

/* ───────────────────────────────────────────────────────────────── *
 *  ps2_boot_game() — punto de entrada al juego
 * ───────────────────────────────────────────────────────────────── */
void ps2_boot_game(void) {
    printf("\n[boot] ─── Inicializando capa de compatibilidad ───\n");

    /* El orden importa: las tablas laterales deben existir antes de que
     * nadie cree un hilo o una cola. */
    BOOT_STAGE("JUEGO: tablas de hilos y mensajes");
    ps2_thread_init();
    ps2_mesgqueue_init();
    ps2_event_init();
    ps2_timer_init();
    ps2_runtime_trace_start();
    ps2_vi_init();

    /* Heap del juego.
     * ANTES: ps2_heap_init() existía pero no la llamaba nadie. */
    BOOT_STAGE("JUEGO: inicializando heap");
    ps2_heap_init();

    /* Guardado en memory card.
     * ANTES: ps2_save_init() tampoco la llamaba nadie. */
    BOOT_STAGE("GUARDADO: cargando memory card");
    ps2_save_init();
    BOOT_STAGE("GUARDADO: inicializacion terminada");

    /* Audio */
    BOOT_STAGE("AUDIO: inicializando controlador");
    PS2Audio_Init();
    BOOT_STAGE("AUDIO: inicializacion terminada");

    /* Latido por frame */
    start_vblank_thread();

    printf("[boot] ─── Arrancando el juego ───\n");

    /* Y aquí se le cede el control al código original de OoT.
     * bootproc() crea el hilo idle, que crea el hilo main, que crea el
     * hilo graph. A partir de este punto manda el juego. */
    BOOT_STAGE("JUEGO: llamando bootproc");
    bootproc();
    BOOT_STAGE("JUEGO: bootproc ha retornado");

    /* bootproc() vuelve enseguida (sólo arranca hilos). El hilo actual
     * se queda vivo a prioridad mínima para no morir y llevarse por
     * delante el proceso. */
    ChangeThreadPriority(GetThreadId(), 126);
    while (s_running) {
        SleepThread();
    }
}

void ps2_boot_shutdown(void) {
    s_running = false;
    DisableIntc(2);
}

u64 ps2_frame_count(void) {
    return s_frameCount;
}
