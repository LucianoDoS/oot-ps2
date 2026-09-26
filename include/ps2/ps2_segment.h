/**
 * ps2_segment.h — Direcciones segmentadas y lectura de display lists
 *
 * ══════════════════════════════════════════════════════════════════════
 *  1. DIRECCIONES SEGMENTADAS
 * ══════════════════════════════════════════════════════════════════════
 *
 * Este era uno de los fallos más graves del renderer. El código anterior
 * hacía, en f3dex2_vtx():
 *
 *     void* addr = (void*)w1;   // "segment address (already resolved)"
 *
 * El comentario era falso: nadie las resolvía nunca.
 *
 * En N64 los punteros que viajan dentro de una display list NO son
 * direcciones de memoria. Son pares (segmento, offset) empaquetados:
 *
 *     bits 31..24 = número de segmento (0-15)
 *     bits 23..0  = offset dentro de ese segmento
 *
 * El RSP los traduce consultando su tabla de segmentos, que el juego va
 * rellenando con gSPSegment(). Sin esa traducción, un puntero a vértices
 * del segmento 6 (objetos) se interpretaba como la dirección literal
 * 0x06xxxxxx, que en PS2 no es memoria válida.
 *
 * Por eso NINGUNA display list real del juego podía funcionar, por muy
 * bien que estuviera el resto del intérprete.
 *
 * ══════════════════════════════════════════════════════════════════════
 *  2. ENDIANNESS: EL PUNTO MÁS IMPORTANTE DE TODO EL PORT
 * ══════════════════════════════════════════════════════════════════════
 *
 * El intérprete anterior leía SIEMPRE en big-endian (read_be32). Eso es
 * incorrecto en la mitad de los casos, y conviene entender por qué.
 *
 * Hay dos clases de display lists en OoT:
 *
 *   a) Las que vienen de los ASSETS (escenas, objetos, mapas). Son datos
 *      binarios extraídos de la ROM de N64 → BIG-ENDIAN.
 *
 *   b) Las que el juego CONSTRUYE en tiempo de ejecución con las macros
 *      gSPxxx (Gfx_SetupDL, gfxalloc, el HUD, el menú de pausa, casi
 *      todo z_lib...). Estas las escribe el código C compilado para PS2
 *      → LITTLE-ENDIAN.
 *
 * No existe ningún modo de lectura que acierte con las dos a la vez.
 * La solución correcta, y la que usan todos los ports serios, es
 * CONVERTIR LOS ASSETS A LITTLE-ENDIAN AL EXTRAERLOS, y luego leerlo
 * todo en el endianness nativo.
 *
 * Por eso el modo por defecto aquí es NATIVO. Si en algún momento
 * quieres probar con assets sin convertir, compila con
 * -DOOT_ASSETS_BIG_ENDIAN=1, pero ten claro que entonces el HUD y los
 * menús se romperán, porque esos sí son nativos.
 */

#ifndef PS2_SEGMENT_H
#define PS2_SEGMENT_H

#include "ps2_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ───────────────────────────────────────────────────────────────── *
 *  Tabla de segmentos
 *
 *  El renderer conserva su propia tabla, equivalente a la memoria DMEM del
 *  RSP. gSPSegment() la modifica sin alterar gSegments del código del juego.
 * ───────────────────────────────────────────────────────────────── */
#define PS2_NUM_SEGMENTS  16

/* RSP DMEM state is independent of the CPU's gSegments array. */
extern uintptr_t gRspSegments[PS2_NUM_SEGMENTS];

#define SEGMENT_NUMBER_OF(a)  (((u32)(a) >> 24) & 0x0F)
#define SEGMENT_OFFSET_OF(a)  ( (u32)(a) & 0x00FFFFFF)

/**
 * ps2_segmented_to_virtual() — traduce (segmento:offset) a puntero real
 *
 * El segmento 0 es especial: significa "dirección absoluta", así que se
 * devuelve tal cual. Esto importa porque las display lists construidas
 * en tiempo de ejecución usan punteros reales, no segmentados.
 */
static inline void* ps2_segmented_to_virtual(u32 addr) {
    if (addr == 0) return NULL;

    u32 seg = SEGMENT_NUMBER_OF(addr);

    /* Segmento 0 = puntero absoluto (DLs generadas en runtime).
     * Además, cualquier dirección que ya apunte a RAM del EE
     * (>= 0x00100000 con el byte alto a 0) se deja intacta. */
    if (seg == 0) {
        return (void*)(uintptr_t)addr;
    }

    uintptr_t base = gRspSegments[seg];
    if (base == 0) {
        /* Segmento sin asignar: devolvemos NULL en vez de un puntero
         * basura. Quien lo use debe comprobarlo (todos lo hacen). */
        return NULL;
    }

    return (void*)(base + SEGMENT_OFFSET_OF(addr));
}

/* ───────────────────────────────────────────────────────────────── *
 *  Lectura de palabras de display list
 * ───────────────────────────────────────────────────────────────── */

#ifndef OOT_ASSETS_BIG_ENDIAN
#define OOT_ASSETS_BIG_ENDIAN 0
#endif

#if OOT_ASSETS_BIG_ENDIAN
#  define GFX_READ_W0(p)  read_be32((const u8*)(p))
#  define GFX_READ_W1(p)  read_be32((const u8*)(p) + 4)
#  define ASSET_S16(p)    ((s16)read_be16((const u8*)(p)))
#  define ASSET_U16(p)    ((u16)read_be16((const u8*)(p)))
#  define ASSET_U32(p)    ((u32)read_be32((const u8*)(p)))
#else
   /* Nativo (little-endian en PS2): los assets se convierten al
    * extraerlos, así que todo se lee directamente. */
   static inline u32 gfx_read_w0(const void* p) {
       u32 v; memcpy(&v, (const u8*)p, 4); return v;
   }
   static inline u32 gfx_read_w1(const void* p) {
       u32 v; memcpy(&v, (const u8*)p + 4, 4); return v;
   }
   static inline s16 asset_s16(const void* p) {
       s16 v; memcpy(&v, p, 2); return v;
   }
   static inline u16 asset_u16(const void* p) {
       u16 v; memcpy(&v, p, 2); return v;
   }
   static inline u32 asset_u32(const void* p) {
       u32 v; memcpy(&v, p, 4); return v;
   }
#  define GFX_READ_W0(p)  gfx_read_w0(p)
#  define GFX_READ_W1(p)  gfx_read_w1(p)
#  define ASSET_S16(p)    asset_s16(p)
#  define ASSET_U16(p)    asset_u16(p)
#  define ASSET_U32(p)    asset_u32(p)
#endif

#ifdef __cplusplus
}
#endif

#endif /* PS2_SEGMENT_H */
