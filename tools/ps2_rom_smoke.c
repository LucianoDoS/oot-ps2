/* Generated sectors, never a game ROM. Test the real SDK file driver/cache. */
#include "ultra64.h"
#include "string.h"
#include "ps2_rom_cache.h"
extern void ps2_rom_cache_stats(u32*, u32*);
extern void ps2_rom_shutdown(void);
#define FIXTURE_SIZE (256u * 2049u)
static u8 fixture_byte(u32 offset) { return (u8)(offset ^ (offset >> 12) ^ (offset >> 17)); }
static u32 fixture_word(u32 offset) {
    return (fixture_byte(offset) << 24) | (fixture_byte(offset + 1) << 16) |
        (fixture_byte(offset + 2) << 8) | fixture_byte(offset + 3);
}

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
    message.devAddr = FIXTURE_SIZE - 6;
    if (osEPiStartDma(handle, &message, OS_READ) != -1) return 6;
    for (int i = 0; i < 20; i++) if (data[i] != 0xAA) return 7;
    if (osEPiReadIo(handle, 0xB0000004, &value) != 0 || value != 0x04050607) return 8;
    /* Cross a cache-sector boundary without changing caller guard bytes. */
    message.devAddr = 4090;
    if (osEPiStartDma(handle, &message, OS_READ) != 0) return 9;
    for (int i = 0; i < 16; i++) if (data[i + 2] != fixture_byte(4090 + i)) return 10;
    u32 hits, reads, afterHits, afterReads;
    ps2_rom_cache_stats(&hits, &reads);
    if (osEPiStartDma(handle, &message, OS_READ) != 0) return 11;
    ps2_rom_cache_stats(&afterHits, &afterReads);
    if (afterReads != reads || afterHits != hits + 2) return 12;
    /* Evict the first sector, then recover its original bytes. */
    for (int i = 1; i <= PS2_ROM_CACHE_SLOTS + 1; i++) {
        if (osEPiReadIo(handle, i * 4096, &value) != 0 || value != fixture_word(i * 4096)) return 13;
    }
    ps2_rom_cache_stats(NULL, &reads);
    if (osEPiReadIo(handle, 0, &value) != 0 || value != 0x00010203) return 14;
    ps2_rom_cache_stats(NULL, &afterReads);
    if (afterReads != reads + 1) return 15;
    /* Partial final sector and shutdown/reopen cannot retain stale entries. */
    if (osEPiReadIo(handle, FIXTURE_SIZE - 4, &value) != 0 || value != fixture_word(FIXTURE_SIZE - 4)) return 16;
    ps2_rom_shutdown();
    if (osEPiReadIo(handle, 0, &value) != 0 || value != 0x00010203) return 17;
    ps2_rom_cache_stats(&hits, &reads);
    if (hits || reads != 1) return 18;
#ifdef PS2_ROM_BULK_BLOCK
    /* Twenty hot sample sectors survive bulk room loads. The previous
     * sixteen-sector cache cannot hold this working set. */
    ps2_rom_shutdown();
    for (int i = 0; i < 20; i++)
        if (osEPiReadIo(handle, 0x40000 + i * 4096, &value) || value != fixture_word(0x40000 + i * 4096)) return 19;
    ps2_rom_cache_stats(NULL, &reads);
    static u8 bulk[8192 + 2];
    message.dramAddr = bulk + 1; message.size = 8192;
    for (int pass = 0; pass < 2; pass++) {
        for (int chunk = 0; chunk < 8; chunk++) {
            memset(bulk, 0xAA, sizeof(bulk));
            message.devAddr = chunk * 8192;
            if (osEPiStartDma(handle, &message, OS_READ)) return 20;
            for (int i = 0; i < 8192; i++) if (bulk[i + 1] != fixture_byte(chunk * 8192 + i)) return 21;
            if (bulk[0] != 0xAA || bulk[8193] != 0xAA) return 22;
            for (int i = 0; i < 20; i++)
                if (osEPiReadIo(handle, 0x40000 + i * 4096, &value) || value != fixture_word(0x40000 + i * 4096)) return 23;
        }
        ps2_rom_cache_stats(NULL, &afterReads);
        if (afterReads != reads + 4) return 24; /* Four 16 KiB reads, then no I/O. */
    }
    /* Cross a bulk boundary, evict it, recover it, and read partial EOF. */
    message.devAddr = 65530; message.size = 8192;
    if (osEPiStartDma(handle, &message, OS_READ)) return 25;
    for (int i = 0; i < 8192; i++) if (bulk[i + 1] != fixture_byte(65530 + i)) return 26;
    message.devAddr = FIXTURE_SIZE - 8192;
    if (osEPiStartDma(handle, &message, OS_READ)) return 27;
    for (int i = 0; i < 8192; i++) if (bulk[i + 1] != fixture_byte(FIXTURE_SIZE - 8192 + i)) return 28;
    ps2_rom_shutdown();
#endif
    return 0;
}
