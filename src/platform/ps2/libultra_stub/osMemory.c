/**
 * osMemory.c — Memoria, caché y buffer NMI
 *
 * ── QUÉ ESTABA MAL ────────────────────────────────────────────────────
 *
 * 1. osAppNMIBuffer = NULL. El juego hace, literalmente:
 *        gAppNmiBufferPtr = (PreNmiBuff*)osAppNMIBuffer;
 *        PreNmiBuff_Init(gAppNmiBufferPtr);
 *    y PreNmiBuff_Init escribe a través de ese puntero. Con NULL, es un
 *    crash garantizado en cuanto arranca Main().
 *
 * 2. ps2_heap_init() reservaba el heap en un `static` DENTRO de la
 *    función y no lo llamaba nadie. El heap del juego no se inicializaba
 *    nunca.
 *
 * 3. Las operaciones de caché eran no-ops con un comentario de
 *    "descomentar si hay problemas". En PS2 SÍ hacen falta: el EE tiene
 *    caché de escritura diferida y la GIF/DMA lee de RAM física. Si no
 *    se vuelca la caché, la GPU dibuja datos viejos. Las operaciones
 *    por rango están ahora en ps2_cache.c; descartar toda la caché de
 *    datos también descartaba pilas y mensajes pendientes en hardware.
 *
 * 4. bzero/bcopy se definían aquí, pero ps2sdk ya las trae en su libc.
 *    Duplicarlas da "multiple definition" al enlazar. Se eliminan.
 */

#include <kernel.h>
#include <string.h>
#include <stdio.h>

#include "ps2_types.h"
#include "ps2_init.h"

/* ───────────────────────────────────────────────────────────────── *
 *  osMemSize — RAM total que le decimos al juego que hay
 *
 *  Reportamos 8 MB (N64 con Expansion Pak). No conviene mentir con más:
 *  OoT usa osMemSize para calcular el tamaño de arenas y buffers, y
 *  algunos cálculos asumen el mapa de memoria de N64.
 * ───────────────────────────────────────────────────────────────── */
u32 osMemSize = OOT_HEAP_SIZE;

u32 osGetMemSize(void) {
    return osMemSize;
}

/* ───────────────────────────────────────────────────────────────── *
 *  osAppNMIBuffer — buffer de 64 bytes que sobrevive a un reset
 *
 *  En N64 vive en una zona fija de RDRAM. Aquí basta con un buffer
 *  estático alineado: lo importante es que NO sea NULL.
 * ───────────────────────────────────────────────────────────────── */
/* CORREGIDO — y este era un error serio por mi parte:
 *
 * osAppNMIBuffer NO es un puntero. En ultra64/os_system.h es:
 *     extern s32 osAppNMIBuffer[0x10];
 * o sea un ARRAY de 16 enteros (64 bytes).
 *
 * Declararlo como `void* osAppNMIBuffer` habría hecho que el juego
 * leyese los primeros 4 bytes del array como si fueran la dirección
 * del buffer, y escribiese PreNmiBuff a través de esa basura. Es
 * exactamente el tipo de corrupción que este archivo venía a arreglar.
 *
 * Lo detecté compilando contra los headers reales del juego, no
 * leyendo el código. */
s32 osAppNMIBuffer[0x10];

/* ───────────────────────────────────────────────────────────────── *
 *  Heap del juego
 *
 *  El arena allocator de OoT recibe (inicio, tamaño) desde Main(). Este
 *  bloque es lo que le entregamos. Se declara a nivel de archivo (no
 *  dentro de la función) para que exista durante toda la ejecución y
 *  para poder consultarlo desde fuera.
 * ───────────────────────────────────────────────────────────────── */
static u8 s_gameHeap[OOT_HEAP_SIZE] __attribute__((aligned(64)));
static bool s_heapReady = false;

void ps2_heap_init(void) {
    if (s_heapReady) return;
    memset(s_gameHeap, 0, sizeof(s_gameHeap));
    s_heapReady = true;
    printf("[osMemory] Heap del juego: %p - %p (%u MB)\n",
           (void*)s_gameHeap,
           (void*)(s_gameHeap + sizeof(s_gameHeap)),
           (unsigned)(sizeof(s_gameHeap) / (1024 * 1024)));
}

void* ps2_heap_base(void) {
    return s_gameHeap;
}

u32 ps2_heap_size(void) {
    return (u32)sizeof(s_gameHeap);
}

/* Cache calls live in ps2_cache.c. Invalidation must never discard dirty
 * stacks, queue fields or CPU-produced payloads outside the requested range. */

/* ───────────────────────────────────────────────────────────────── *
 *  TLB — el EE no necesita el mapeo de overlays de N64
 * ───────────────────────────────────────────────────────────────── */
void osMapTLBRdb(void)    {}
void osUnmapTLBAll(void)  {}

/* NOTA IMPORTANTE sobre bzero/bcopy:
 * Ya las proporciona la libc de ps2sdk. Definirlas aquí provocaba
 * "multiple definition" al enlazar. Si tu ps2sdk no las trae, activa
 * este bloque. */
#ifdef PS2_NEED_BSD_MEM
void bzero(void* dst, int n)                  { memset(dst, 0, (size_t)n); }
void bcopy(const void* src, void* dst, int n) { memmove(dst, src, (size_t)n); }
#endif
