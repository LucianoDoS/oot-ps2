/* libultra cache calls on EE. ROM, JPEG and audio stubs also write through
 * the CPU, so preserve dirty bytes before invalidating a destination.
 * SyncDCache handles complete 64-byte EE lines and saves interrupt state. */
#include <kernel.h>
#include <stdint.h>

static void sync_range(void* address, s32 bytes) {
    if (!address || bytes <= 0) return;
    uintptr_t start = (uintptr_t)address;
    uintptr_t last = start + (uintptr_t)bytes - 1;
    if (last < start) return;
    /* SDK end is inclusive after rounding down. Using the final byte avoids
     * touching an unrelated line when the requested end is exactly aligned. */
    SyncDCache((void*)start, (void*)last);
}

void osWritebackDCache(void* address, s32 bytes) { sync_range(address, bytes); }
void osInvalDCache(void* address, s32 bytes) { sync_range(address, bytes); }
void osWritebackDCacheAll(void) { FlushCache(WRITEBACK_DCACHE); }

void osInvalICache(void* address, s32 bytes) {
    if (!address || bytes <= 0) return;
    FlushCache(INVALIDATE_ICACHE);
}
