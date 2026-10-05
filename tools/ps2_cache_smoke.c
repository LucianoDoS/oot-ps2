/* On real EE hardware, check dirty neighbours and cached/uncached aliases.
 * PCSX2 can check flow but does not certify real data-cache behaviour. */
#include <kernel.h>
#include <stdio.h>

extern void osInvalDCache(void*, s32);
extern void osWritebackDCache(void*, s32);

static volatile unsigned char sBuffer[192] __attribute__((aligned(64)));
static volatile unsigned char sOther[128] __attribute__((aligned(64)));

int ps2_cache_smoke(void) {
    volatile unsigned int stack_guard[16];
    volatile unsigned char* physical = UNCACHED_SEG(sBuffer);
    for (int i = 0; i < 192; i++) sBuffer[i] = 0;
    for (int i = 0; i < 128; i++) sOther[i] = 0;
    FlushCache(WRITEBACK_DCACHE);

    /* Leave all these stores dirty. Invalidating a subrange must preserve
     * the rest of its cache lines, an unrelated object and this thread's stack. */
    for (int i = 0; i < 192; i++) sBuffer[i] = (unsigned char)(i ^ 0x5a);
    for (int i = 0; i < 128; i++) sOther[i] = (unsigned char)(i ^ 0xa5);
    for (int i = 0; i < 16; i++) stack_guard[i] = 0x12340000u + i;
    osInvalDCache((void*)(sBuffer + 3), 70);
    for (int i = 0; i < 192; i++) if (sBuffer[i] != (unsigned char)(i ^ 0x5a)) return 1;
    for (int i = 0; i < 128; i++) if (sOther[i] != (unsigned char)(i ^ 0xa5)) return 2;
    for (int i = 0; i < 16; i++) if (stack_guard[i] != 0x12340000u + i) return 3;
    for (int i = 0; i < 128; i++) if (physical[i] != (unsigned char)(i ^ 0x5a)) return 4;

    /* Simulate a DMA write through the uncached alias after preparing its
     * destination. Cached reads must then see the new bytes. */
    osInvalDCache((void*)(sBuffer + 64), 64);
    for (int i = 64; i < 128; i++) physical[i] = (unsigned char)(i + 7);
    __asm__ volatile("sync" : : : "memory");
    for (int i = 64; i < 128; i++) if (sBuffer[i] != (unsigned char)(i + 7)) return 5;
    /* CPU-produced JPEG/audio data also uses the invalidation API. */
    sBuffer[71] = 0xee;
    osInvalDCache((void*)(sBuffer + 71), 1);
    if (physical[71] != 0xee || physical[70] != 77 || physical[72] != 79) return 6;
    sBuffer[1] = 0xbc;
    osWritebackDCache((void*)(sBuffer + 1), 1);
    if (physical[1] != 0xbc) return 7;

    int enabled = DIntr();
    osInvalDCache((void*)sBuffer, 1);
    int unexpectedly_enabled = DIntr();
    if (enabled) EIntr();
    if (unexpectedly_enabled) return 8;
    osInvalDCache(NULL, 1);
    osInvalDCache((void*)sBuffer, 0);
    osWritebackDCache((void*)sBuffer, -1);
    if (sBuffer[1] != 0xbc || sOther[9] != (unsigned char)(9 ^ 0xa5)) return 9;
    printf("[PS2 CACHE CHECK] PASS neighbours=1 cpuPayload=1 dmaAlias=1 interruptState=1\n");
    return 0;
}
