/**
 * rdp_to_gs.c — Intérprete de Display Lists N64 → GS de PS2
 *
 * ══════════════════════════════════════════════════════════════════════
 *  QUÉ ESTABA MAL
 * ══════════════════════════════════════════════════════════════════════
 *
 * 1. TABLA DE OPCODES CON COLISIONES SILENCIOSAS
 *
 *    G_VTX valía 0x01 y G_SPNOOP también. El `case G_SPNOOP` aparecía
 *    ANTES en el switch, así que el compilador se quedaba con el primero
 *    y G_VTX (¡el comando que carga los vértices!) caía en la rama de
 *    "no hacer nada". Dicho de otro modo: aunque todo lo demás hubiera
 *    estado bien, no se habría dibujado un solo triángulo.
 *
 *    De hecho ese switch no compilaba: dos `case` con el mismo valor es
 *    un error, no un aviso. Aquí se separan los opcodes de RSP (bajos)
 *    de los de RDP (altos), que es como funciona F3DEX2 de verdad.
 *
 * 2. SALTOS A DISPLAY LISTS SIN TRADUCIR EL SEGMENTO
 *    `pc = (const Gfx*)addr` con addr = w1 crudo. Ver ps2_segment.h.
 *
 * 3. G_ENDDL NO RESTAURABA BIEN LA PILA en el caso de branch (NOPUSH).
 *
 * 4. rdp_to_gs_init() NO LO LLAMABA NADIE, así que g_rdpState.gs se
 *    quedaba a NULL y rdp_to_gs_flip() reventaba en gsKit_sync_flip(NULL).
 *    Ahora se llama desde ps2_init_gs().
 *
 * 5. Faltaban G_CULLDL, G_BRANCH_Z, G_RDPHALF_1/2 y G_SETOTHERMODE_H/L,
 *    que OoT usa constantemente.
 */

#include <gsKit.h>
#include <string.h>
#include <stdio.h>
#include "ps2_gfx_profile.h"
#include <kernel.h>
#include <unistd.h>

#include "ps2_types.h"
#include "ps2_segment.h"
#include "ps2_init.h"
#include "rdp_to_gs.h"
#include "f3dex2_ps2.h"
#include "ps2_texture.h"
#include "ps2_texture_content.h"
#include "ps2_background.h"
#include "ps2_offscreen.h"
extern void ps2_offscreen_init(GSGLOBAL*);
extern void ps2_offscreen_begin(RDPState*, const PS2RenderTarget*);
extern void ps2_offscreen_end(RDPState*);
extern const u64 gspS2DEX2d_fifoTextStart[] __attribute__((weak));
extern void ps2_background_draw(const PS2Background*, int, RDPState*);
static bool s_s2dex;
static bool s_presentWait;

uintptr_t gRspSegments[PS2_NUM_SEGMENTS];
volatile unsigned gPS2GfxTraceStage;
static const Gfx* s_tracePc;
static u32 s_traceW0, s_traceW1;
void PS2Gfx_DumpTrace(void) {
    printf("[PS2 GFX WAIT] pc=%p command=%08x,%08x stage=%u tri=%u draw=%u\n",
           s_tracePc, s_traceW0, s_traceW1, gPS2GfxTraceStage,
           g_rdpState.stats.triangles, g_rdpState.stats.submitted);
}

/* ═══════════════════════════════════════════════════════════════════ *
 *  OPCODES
 *
 *  Ojo: F3DEX2 tiene DOS espacios de opcodes que se solapan en número.
 *  Los de geometría (RSP) van de 0x00 a 0x0A; los de RDP van de 0xC8
 *  hacia arriba. Por eso NO pueden estar en el mismo switch plano.
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Comandos de RSP (bajos) ── */
#define G_NOOP          0x00
#define G_VTX           0x01
#define G_MODIFYVTX     0x02
#define G_CULLDL        0x03
#define G_BRANCH_Z      0x04
#define G_TRI1          0x05
#define G_TRI2          0x06
#define G_QUAD          0x07
#define G_LINE3D        0x08
#define G_SPECIAL_3     0xD3
#define G_SPECIAL_2     0xD4
#define G_SPECIAL_1     0xD5
#define G_DMA_IO        0xD6
#define G_TEXTURE       0xD7
#define G_POPMTX        0xD8
#define G_GEOMETRYMODE  0xD9
#define G_MTX           0xDA
#define G_MOVEWORD      0xDB
#define G_MOVEMEM       0xDC
#define G_LOAD_UCODE    0xDD
#define G_DL            0xDE
#define G_ENDDL         0xDF

/* ── Comandos de RDP (altos) ── */
#define G_SETCIMG         0xFF
#define G_SETZIMG         0xFE
#define G_SETTIMG         0xFD
#define G_SETCOMBINE      0xFC
#define G_SETENVCOLOR     0xFB
#define G_SETPRIMCOLOR    0xFA
#define G_SETBLENDCOLOR   0xF9
#define G_SETFOGCOLOR     0xF8
#define G_SETFILLCOLOR    0xF7
#define G_FILLRECT        0xF6
#define G_SETTILE         0xF5
#define G_LOADTILE        0xF4
#define G_LOADBLOCK       0xF3
#define G_SETTILESIZE     0xF2
#define G_LOADTLUT        0xF0
#define G_RDPSETOTHERMODE 0xEF
#define G_SETPRIMDEPTH    0xEE
#define G_SETSCISSOR      0xED
#define G_SETCONVERT      0xEC
#define G_SETKEYR         0xEB
#define G_SETKEYGB        0xEA
#define G_RDPFULLSYNC     0xE9
#define G_RDPTILESYNC     0xE8
#define G_RDPPIPESYNC     0xE7
#define G_RDPLOADSYNC     0xE6
#define G_TEXRECTFLIP     0xE5
#define G_TEXRECT         0xE4
#define G_RDPHALF_1       0xE1
#define G_RDPHALF_2       0xF1
#define G_SETOTHERMODE_H  0xE3
#define G_SETOTHERMODE_L  0xE2

/* ═══════════════════════════════════════════════════════════════════ *
 *  Estado global del renderer
 * ═══════════════════════════════════════════════════════════════════ */
RDPState g_rdpState;
#if !PS2_GFX_SCRATCHPAD
RSPState g_rspState;
#endif

/* Profundidad máxima de anidamiento de display lists.
 * OoT llega a anidar bastante (escena → habitación → actor → parte del
 * modelo → animación), así que 18 da margen de sobra sobre los 10 del
 * RSP real. */
#define DL_STACK_DEPTH  18

/* ═══════════════════════════════════════════════════════════════════ *
 *  rdp_to_gs_init
 * ═══════════════════════════════════════════════════════════════════ */
void rdp_to_gs_init(GSGLOBAL* gs) {
    ps2_gs_state_reset();
    s_s2dex = false;
    memset(gRspSegments, 0, sizeof(gRspSegments));
    memset(&g_rdpState, 0, sizeof(g_rdpState));
    memset(&g_rspState, 0, sizeof(g_rspState));
#if PS2_GFX_SCRATCHPAD
    for (unsigned i = 0; i < 16; i++) {
        PS2_GFX_WORKSPACE.guardBegin[i] = 0x53505230u ^ i;
        PS2_GFX_WORKSPACE.guardEnd[i] = 0x53505231u ^ i;
    }
    printf("[PS2 GFX STORAGE] scratchpad=%p bytes=%u capacity=16384 CPU-only\n",
        &PS2_GFX_WORKSPACE, (unsigned)sizeof(PS2GfxWorkspace));
#else
    printf("[PS2 GFX STORAGE] main RAM\n");
#endif

    g_rdpState.gs = gs;
    ps2_offscreen_init(gs);

    /* Pila de matrices a identidad */
    for (int i = 0; i < RSP_MTX_STACK_DEPTH; i++) {
        float* m = g_rspState.mtxStack[i];
        memset(m, 0, sizeof(float) * 16);
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }
    memset(g_rspState.modelview,  0, sizeof(g_rspState.modelview));
    memset(g_rspState.projection, 0, sizeof(g_rspState.projection));
    memset(g_rspState.mvp,        0, sizeof(g_rspState.mvp));
    g_rspState.modelview[0]  = g_rspState.modelview[5]  =
    g_rspState.modelview[10] = g_rspState.modelview[15] = 1.0f;
    g_rspState.projection[0]  = g_rspState.projection[5]  =
    g_rspState.projection[10] = g_rspState.projection[15] = 1.0f;
    g_rspState.mvp[0]  = g_rspState.mvp[5]  =
    g_rspState.mvp[10] = g_rspState.mvp[15] = 1.0f;

    g_rspState.mtxStackDepth = 0;
    g_rspState.lookAt[0][0] = 1.0f;
    g_rspState.lookAt[1][1] = 1.0f;

    /* Viewport por defecto: si una display list dibuja antes de mandar
     * su gSPViewport, al menos sale algo en pantalla en lugar de
     * colapsar todo al origen (que es lo que pasaba antes). */
    g_rspState.vpScaleX =  (float)OOT_RENDER_W / 2.0f;
    g_rspState.vpScaleY = -(float)OOT_RENDER_H / 2.0f;
    g_rspState.vpTransX =  (float)OOT_RENDER_W / 2.0f;
    g_rspState.vpTransY =  (float)OOT_RENDER_H / 2.0f;

    g_rspState.geometryMode = 0;
    g_rspState.texScaleS = 1.0f;
    g_rspState.texScaleT = 1.0f;

    g_rdpState.primColor = 0xFFFFFFFF;
    g_rdpState.envColor  = 0xFFFFFFFF;

    printf("[RDP->GS] Renderer inicializado\n");
}

/* ═══════════════════════════════════════════════════════════════════ *
 *  rdp_to_gs_exec — recorre y ejecuta una display list
 * ═══════════════════════════════════════════════════════════════════ */
void rdp_to_gs_exec(const Gfx* dl) {
    if (!dl) return;
#ifdef PS2_FLOOR_DIAGNOSTIC
    ps2_floor_begin_frame();
#endif
    ps2_gs_state_reset();
    ps2_gfx_profile_begin();
    s_s2dex = false;

    const Gfx* pc = dl;
    const Gfx* callStack[DL_STACK_DEPTH];
    int depth = 0;
    bool triangleRun = false;

    /* Guardia contra listas corruptas: si una DL se desboca, mejor
     * cortar que colgar la consola. */
    unsigned long guard = 0;
    const unsigned long GUARD_MAX = 2000000UL;

    for (;;) {
        if (++guard > GUARD_MAX) {
            if (triangleRun) f3dex2_batch_end(&g_rdpState);
            printf("[DL] ABORTADA: mas de %lu comandos, lista corrupta?\n",
                   GUARD_MAX);
            return;
        }
        /* The graphics worker is below audio, scheduler and controller.
         * Retrace wakes those threads directly; no per-command alarm needed. */

        u32 w0, w1;
        gfx_read_words(pc, &w0, &w1);
        u8  op = (u8)(w0 >> 24);
        bool triangle = op == G_TRI1 || op == G_TRI2 || op == G_QUAD;
        if (triangleRun && !triangle) {
            u32 start = ps2_gfx_profile_enter();
            f3dex2_batch_end(&g_rdpState);
            ps2_gfx_profile_leave(GFX_PROFILE_TRI, start);
            triangleRun = false;
        }
        if (!triangleRun && triangle)
            triangleRun = f3dex2_batch_begin(&g_rspState, &g_rdpState);
        if (s_s2dex && (op == 0x09 || op == 0x0a)) {
            ps2_background_draw(ps2_segmented_to_virtual(w1), op == 0x09, &g_rdpState);
            pc++;
            continue;
        }
        if (s_s2dex && op == 0x0b) { pc++; continue; } /* OBJ_RENDERMODE */
        s_tracePc = pc; s_traceW0 = w0; s_traceW1 = w1; gPS2GfxTraceStage = 0;
#ifdef RDP_GS_DEBUG
        if (guard % 10000 == 0)
            printf("[PS2 DL TRACE] commands=%lu pc=%p op=%02x depth=%d\n", guard, pc, op, depth);
#endif

#ifdef RDP_GS_DEBUG
        printf("[DL] @%p op=0x%02X w0=%08X w1=%08X\n",
               (const void*)pc, op, (unsigned)w0, (unsigned)w1);
#endif

        /* ── Comandos de RSP (opcodes bajos y 0xD3..0xDF) ─────────── */
        if (op <= G_LINE3D || (op >= G_SPECIAL_3 && op <= G_ENDDL)) {
            switch (op) {
                case G_NOOP:
                    if (w0 == PS2_GFX_TARGET_BEGIN)
                        ps2_offscreen_begin(&g_rdpState,(const PS2RenderTarget*)(uintptr_t)w1);
                    else if (w0 == PS2_GFX_TARGET_END)
                        ps2_offscreen_end(&g_rdpState);
                    else if (w0 == PS2_GFX_CAPTURE_FRAME)
                        ps2_capture_frame(&g_rdpState,(const void*)(uintptr_t)w1);
                    break;

                case G_VTX: {
                    u32 start = ps2_gfx_profile_enter();
                    f3dex2_vtx(w0, w1, &g_rspState);
                    ps2_gfx_profile_leave(GFX_PROFILE_VTX, start);
                    break;
                }

                case G_TRI1: {
                    u32 start = ps2_gfx_profile_enter();
                    f3dex2_tri1(w0, w1, &g_rspState, &g_rdpState);
                    ps2_gfx_profile_leave(GFX_PROFILE_TRI, start);
                    break;
                }

                case G_TRI2: {
                    u32 start = ps2_gfx_profile_enter();
                    f3dex2_tri2(w0, w1, &g_rspState, &g_rdpState);
                    ps2_gfx_profile_leave(GFX_PROFILE_TRI, start);
                    break;
                }

                case G_QUAD: {
                    u32 start = ps2_gfx_profile_enter();
                    /* Un quad son dos triángulos que comparten arista */
                    f3dex2_tri2(w0, w1, &g_rspState, &g_rdpState);
                    ps2_gfx_profile_leave(GFX_PROFILE_TRI, start);
                    break;
                }

                case G_CULLDL: {
                    /* Descarta la lista entera si el volumen indicado
                     * queda fuera de pantalla. Implementación
                     * conservadora: sólo se corta si TODOS los vértices
                     * del rango comparten un plano de recorte. */
                    int vFirst = (int)((w0 >> 1) & 0x7F);
                    int vLast  = (int)((w1 >> 1) & 0x7F);
                    if (vFirst <= vLast && vLast < RSP_VTX_CACHE_SIZE) {
                        u8 andFlags = 0xFF;
                        for (int i = vFirst; i <= vLast; i++) {
                            andFlags &= g_rspState.vtx[i].clipFlags;
                        }
                        if (andFlags != 0) {
                            /* Fuera de pantalla: terminar esta lista */
                            if (depth > 0) { pc = callStack[--depth]; continue; }
                            return;
                        }
                    }
                    break;
                }

                case G_BRANCH_Z:
                    /* Salto por profundidad (LOD). Sin soporte de LOD
                     * tomamos siempre la rama de más detalle, que es lo
                     * que interesa en PS2. La dirección destino viene en
                     * el G_RDPHALF_1 anterior, así que aquí no hay nada
                     * que hacer: seguimos de largo. */
                    break;

                case G_TEXTURE:
                    f3dex2_texture(w0, w1, &g_rspState, &g_rdpState);
                    break;

                case G_POPMTX:
                    f3dex2_popmtx(w0, w1, &g_rspState);
                    break;

                case G_GEOMETRYMODE:
                    f3dex2_geometrymode(w0, w1, &g_rspState);
                    break;

                case G_MTX: {
                    u32 start = ps2_gfx_profile_enter();
                    f3dex2_mtx(w0, w1, &g_rspState);
                    ps2_gfx_profile_leave(GFX_PROFILE_MATRIX, start);
                    break;
                }

                case G_MOVEWORD:
                    f3dex2_moveword(w0, w1, &g_rspState);
                    break;

                case G_MOVEMEM:
                    f3dex2_movemem(w0, w1, &g_rspState);
                    break;

                case G_DL: {
                    /* w0[16] = 0 → llamada (apilar retorno)
                     *          1 → salto   (no apilar)   */
                    u8 noPush = (u8)((w0 >> 16) & 0xFF);
                    const Gfx* target = (const Gfx*)ps2_segmented_to_virtual(w1);
                    if (!target) break;    /* segmento sin asignar */

                    if (noPush == 0) {
                        if (depth < DL_STACK_DEPTH) {
                            callStack[depth++] = pc + 1;
                        } else {
                            printf("[DL] AVISO: pila de listas llena\n");
                            break;
                        }
                    }
                    pc = target;
                    continue;
                }

                case G_ENDDL:
                    if (depth > 0) {
                        pc = callStack[--depth];
                        continue;
                    }
                    return;

                case G_LOAD_UCODE:
                    s_s2dex = gspS2DEX2d_fifoTextStart &&
                        (w1 & 0x1fffffffu) == ((u32)gspS2DEX2d_fifoTextStart & 0x1fffffffu);
                    { static unsigned reports;
                      if (reports++ < 6) printf("[PS2 UCODE] address=%08x s2dex=%p active=%d\n", w1, gspS2DEX2d_fifoTextStart, s_s2dex); }
                    break;

                default:
                    break;
            }
        }
        /* ── Comandos de RDP (opcodes altos) ──────────────────────── */
        else {
            switch (op) {
                case G_RDPFULLSYNC:
                case G_RDPTILESYNC:
                case G_RDPPIPESYNC:
                case G_RDPLOADSYNC:
                    /* Sincronización interna del RDP: en PS2 todo es
                     * secuencial, no hay nada que sincronizar. */
                    break;

                case G_SETTIMG:      rdp_settimg(w0, w1, &g_rdpState);      break;
                case G_SETTILE:      rdp_settile(w0, w1, &g_rdpState);      break;
                case G_SETTILESIZE:  rdp_settilesize(w0, w1, &g_rdpState);  break;
                case G_LOADBLOCK:
                case G_LOADTILE:     rdp_loadtexture(op, w0, w1, &g_rdpState); break;
                case G_LOADTLUT:     rdp_loadtlut(w0, w1, &g_rdpState);     break;

                case G_SETCOMBINE:   rdp_setcombine(w0, w1, &g_rdpState);   break;
                case G_SETPRIMCOLOR: rdp_setprimcolor(w0, w1, &g_rdpState); break;
                case G_SETENVCOLOR:  rdp_setenvcolor(w0, w1, &g_rdpState);  break;
                case G_SETFOGCOLOR:  rdp_setfogcolor(w0, w1, &g_rdpState);  break;
                case G_SETBLENDCOLOR:rdp_setblendcolor(w0, w1, &g_rdpState);break;
                case G_SETFILLCOLOR: rdp_setfillcolor(w0, w1, &g_rdpState); break;

                case G_RDPSETOTHERMODE:
                    rdp_setothermode(w0, w1, &g_rdpState);
                    break;

                /* FALTABAN: OoT usa muchísimo las versiones parciales */
                case G_SETOTHERMODE_H:
                    rdp_setothermode_h(w0, w1, &g_rdpState);
                    break;

                case G_SETOTHERMODE_L:
                    rdp_setothermode_l(w0, w1, &g_rdpState);
                    break;

                case G_FILLRECT:     rdp_fillrect(w0, w1, &g_rdpState);     break;
                case G_SETSCISSOR:   rdp_setscissor(w0, w1, &g_rdpState);   break;
                case G_SETCIMG:      rdp_setcimg(w0, w1, &g_rdpState);      break;
                case G_SETZIMG:      rdp_setzimg(w0, w1, &g_rdpState);      break;

                case G_TEXRECT:
                case G_TEXRECTFLIP: {
                    u32 start = ps2_gfx_profile_enter();
                    rdp_texrect(op, w0, w1, pc, &g_rdpState);
                    ps2_gfx_profile_leave(GFX_PROFILE_RECT, start);
                    pc += 2;   /* TEXRECT ocupa 3 comandos en total */
                    break;
                }

                case G_RDPHALF_1:
                case G_RDPHALF_2:
                    /* Mitades de comandos largos; las consume quien las
                     * necesita (TEXRECT, BRANCH_Z). */
                    break;

                case G_SETPRIMDEPTH:
                case G_SETCONVERT:
                case G_SETKEYR:
                case G_SETKEYGB:
                    /* Sin efecto visible en la mayoría de escenas de OoT */
                    break;

                default:
#ifdef RDP_GS_DEBUG
                    printf("[DL] opcode sin implementar: 0x%02X\n", op);
#endif
                    break;
            }
        }

        pc++;
    }
}

/* ═══════════════════════════════════════════════════════════════════ *
 *  rdp_to_gs_flip — vuelca la cola y cambia de buffer
 * ═══════════════════════════════════════════════════════════════════ */
void rdp_to_gs_submit(void) {
    GSGLOBAL* gs = g_rdpState.gs;
    if (!gs) return;          /* antes esto reventaba con NULL */
#if PS2_GFX_SCRATCHPAD
    static bool guardReported;
    for (unsigned i = 0; i < 16; i++) {
        if ((PS2_GFX_WORKSPACE.guardBegin[i] != (0x53505230u ^ i) ||
             PS2_GFX_WORKSPACE.guardEnd[i] != (0x53505231u ^ i)) && !guardReported) {
            printf("[PS2 GFX STORAGE] ERROR scratchpad guard changed index=%u\n", i);
            guardReported = true;
        }
    }
#endif
    ps2_gfx_profile_end();
    PS2TextureContentStats content = ps2_texture_content_stats(true);
    static u32 frames;
    if (++frames <= 3 || frames % 300 == 0) {
#if PS2_GFX_SCRATCHPAD
        printf("[PS2 GFX STORAGE] frame=%u scratchpadBytes=%u capacity=16384\n",
            frames, (unsigned)sizeof(PS2GfxWorkspace));
#endif
        printf("[PS2 GEOMETRY] frame=%u tri=%u clipped=%u culled=%u invalid=%u textureFail=%u draw=%u bg=%u mode=%08x,%08x\n",
            frames, g_rdpState.stats.triangles, g_rdpState.stats.clipped,
            g_rdpState.stats.culled, g_rdpState.stats.invalid,
            g_rdpState.stats.textureFailed, g_rdpState.stats.submitted, g_rdpState.stats.backgrounds,
            g_rdpState.otherMode.hi, g_rdpState.otherMode.lo);
        printf("[PS2 TEXTURE PERF] hits=%u uploads=%u waits=%u packets=%u pointBuilds=%u pointHits=%u\n",
            g_rdpState.stats.textureHits, g_rdpState.stats.textureUploads,
            g_rdpState.stats.textureWaits, g_rdpState.stats.texturePackets,
            g_rdpState.stats.textureVertexBuilds, g_rdpState.stats.textureVertexHits);
        printf("[PS2 STATE PERF] writes=%u reused=%u\n", g_rdpState.stats.stateWrites, g_rdpState.stats.stateReuse);
        printf("[PS2 TEXTURE CONTENT] exactHits=%u hashes=%u comparedBytes=%u hashedBytes=%u\n",
            content.hits, content.hashes, content.comparedBytes, content.hashedBytes);
        printf("[PS2 COLOR PERF] packets=%u vertexBuilds=%u vertexHits=%u\n",
            g_rdpState.stats.colorPackets,g_rdpState.stats.colorVertexBuilds,g_rdpState.stats.colorVertexHits);
    }
    memset(&g_rdpState.stats, 0, sizeof(g_rdpState.stats));

    gPS2GfxTraceStage = 7;
    gsKit_set_finish(gs);
    gsKit_queue_exec(gs);
    gPS2GfxTraceStage = 8;
    gsKit_finish();
    ps2_texture_end_frame(&g_rdpState);
    /* Arm vblank before DP_DONE wakes the CPU. A retrace occurring during
     * its next update must remain visible, rather than being cleared later
     * by gsKit_sync_flip when the lower-priority worker resumes. */
    s_presentWait = !gs->FirstFrame;
    if (s_presentWait) gsKit_vsync_nowait();
}

void rdp_to_gs_present(void) {
    GSGLOBAL* gs = g_rdpState.gs;
    if (!gs) return;
    if (s_presentWait) {
        while (!(*GS_CSR & 8)) {}
        if (gs->DoubleBuffering == GS_SETTING_ON) {
            gsKit_display_buffer(gs);
            gs->ActiveBuffer ^= 1;
        }
    }
    /* Same buffer/context setup as gsKit_sync_flip, with the vblank clear
     * moved to submit. switch_context also toggles PrimContext, so avoid it. */
    gsKit_setactive(gs);
    gPS2GfxTraceStage = 9;
}

void rdp_to_gs_flip(void) {
    rdp_to_gs_submit();
    rdp_to_gs_present();
}
