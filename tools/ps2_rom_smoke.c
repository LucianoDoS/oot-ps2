/* Generated sectors, never a game ROM. Test the real SDK file driver/cache. */
#include "ultra64.h"
#include "string.h"
extern void ps2_rom_cache_stats(u32*, u32*);
extern void ps2_rom_shutdown(void);

int ps2_rom_smoke(void) {
    OSPiHandle* handle = osCartRomInit();
    OSIoMesg message;
    u8 data[20];
    u32 value;
    if (handle == NULL) return 1;
    if (osEPiReadIo(handle, 0, &value) != 0 || value != 0x00010203) return 2;
    memset(&message, 0, sizeof(message));
    memset(data, 0xAA, sizeof(data));
    message.dramAddr = data + 2;
    message.devAddr = 0x100000FC;
    message.size = 16;
    if (osEPiStartDma(handle, &message, OS_READ) != 0) return 3;
    for (int i = 0; i < 16; i++) if (data[i + 2] != (u8)(252 + i)) return 4;
    if (data[0] != 0xAA || data[1] != 0xAA || data[18] != 0xAA || data[19] != 0xAA) return 5;
    // An out-of-range request must fail without silently filling in zeroes.
    memset(data, 0xAA, sizeof(data));
    message.devAddr = 256 * 513 - 6;
    if (osEPiStartDma(handle, &message, OS_READ) != -1) return 6;
    for (int i = 0; i < 20; i++) if (data[i] != 0xAA) return 7;
    if (osEPiReadIo(handle, 0xB0000004, &value) != 0 || value != 0x04050607) return 8;
    /* Cross a cache-sector boundary without changing caller guard bytes. */
    message.devAddr = 4090;
    if (osEPiStartDma(handle, &message, OS_READ) != 0) return 9;
    for (int i = 0; i < 16; i++) if (data[i + 2] != (u8)(4090 + i)) return 10;
    u32 hits, reads, afterHits, afterReads;
    ps2_rom_cache_stats(&hits, &reads);
    if (osEPiStartDma(handle, &message, OS_READ) != 0) return 11;
    ps2_rom_cache_stats(&afterHits, &afterReads);
    if (afterReads != reads || afterHits != hits + 2) return 12;
    /* Evict the first sector, then recover its original bytes. */
    for (int i = 1; i <= 17; i++) {
        if (osEPiReadIo(handle, i * 4096, &value) != 0 || value != 0x00010203) return 13;
    }
    ps2_rom_cache_stats(NULL, &reads);
    if (osEPiReadIo(handle, 0, &value) != 0 || value != 0x00010203) return 14;
    ps2_rom_cache_stats(NULL, &afterReads);
    if (afterReads != reads + 1) return 15;
    /* Partial final sector and shutdown/reopen cannot retain stale entries. */
    if (osEPiReadIo(handle, 256 * 513 - 4, &value) != 0 || value != 0xfcfdfeff) return 16;
    ps2_rom_shutdown();
    if (osEPiReadIo(handle, 0, &value) != 0 || value != 0x00010203) return 17;
    ps2_rom_cache_stats(&hits, &reads);
    if (hits || reads != 1) return 18;
    return 0;
}
