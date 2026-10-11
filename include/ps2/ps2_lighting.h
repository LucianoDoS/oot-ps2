#ifndef PS2_LIGHTING_H
#define PS2_LIGHTING_H
#include "rdp_to_gs.h"

/* Independent normal caching had no hits in the fixed-view game profiles. */
#ifndef PS2_NORMAL_VECTOR_REUSE
#define PS2_NORMAL_VECTOR_REUSE 0
#endif

#ifndef PS2_LIGHT_RGB_CACHE_BITS
/* Fixed-view game profiles retain identical pixels with fewer rebuilds.
 * 1024 entries use 12 KiB, 9 KiB more than the previous direct map. */
#define PS2_LIGHT_RGB_CACHE_BITS 10
#endif
#if PS2_LIGHT_RGB_CACHE_BITS < 8 || PS2_LIGHT_RGB_CACHE_BITS > 10
#error "RGB lighting cache supports 256, 512 or 1024 entries"
#endif

typedef struct {
    u32 tableChanges, rgbHits, rgbBuilds, normalHits, normalBuilds, storageBytes;
    u32 rgbEntries, rgbStorageBytes;
} PS2LightingCacheStats;
PS2LightingCacheStats ps2_light_cache_stats(bool reset);

#ifdef PS2_RENDERER_SMOKE_CAPTURE
/* Clears both caches, so the oracle can replay identical light/normal order. */
void ps2_light_test_normal_cache(bool enabled);
#endif
#endif
