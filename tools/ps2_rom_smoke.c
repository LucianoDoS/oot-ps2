/* A generated 4 KiB fixture, never a game ROM. Tests the real SDK file driver. */
#include "ultra64.h"
#include "string.h"

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
    message.devAddr = 4090;
    if (osEPiStartDma(handle, &message, OS_READ) != -1) return 6;
    for (int i = 0; i < 20; i++) if (data[i] != 0xAA) return 7;
    if (osEPiReadIo(handle, 0xB0000004, &value) != 0 || value != 0x04050607) return 8;
    return 0;
}
