/* Exercise the actual SRAM adapter with an isolated in-memory card backend. */
#include <string.h>
#include <stdio.h>
#include "ps2_types.h"
#include "ps2_sram.h"

static u8 card[PS2_SRAM_SIZE];
static int exists, writes, failWrite;

int ps2_sram_load_from_card(void* data, u32 size) {
    if (!exists || size != sizeof(card)) return -1;
    memcpy(data, card, size);
    return 0;
}

int ps2_sram_flush_to_card(const void* data, u32 size) {
    if (failWrite || size != sizeof(card)) return -1;
    memcpy(card, data, size);
    exists = 1;
    writes++;
    return 0;
}

int ps2_sram_smoke(void) {
    int failures = 0;
    u8 options[3] = {2,1,7}, slot[4] = {0xde,0xad,0xbe,0xef}, readback[4];
    ps2_sram_init();
    memset(readback, 0xff, sizeof(readback));
    if (ps2_sram_transfer(PS2_SRAM_ADDRESS + 20, readback, 4, 0) ||
        memcmp(readback, "\0\0\0\0", 4)) failures++;
    if (ps2_sram_transfer(PS2_SRAM_ADDRESS + 0x100, slot, 4, 1) ||
        ps2_sram_transfer(0xa8000000u, options, 3, 1)) failures++;
    if (writes != 2 || memcmp(card, options, 3) || memcmp(card + 0x100, slot, 4)) failures++;
    ps2_sram_init(); /* Simulate a cold cache and recover the persisted image. */
    if (ps2_sram_transfer(PS2_SRAM_ADDRESS + 0x100, readback, 4, 0) ||
        memcmp(readback, slot, 4)) failures++;
    failWrite = 1;
    slot[0] = 0x42;
    if (ps2_sram_transfer(PS2_SRAM_ADDRESS + 0x100, slot, 4, 1) != -1 ||
        ps2_sram_transfer(PS2_SRAM_ADDRESS + 0x100, readback, 4, 0) ||
        memcmp(readback, slot, 4) || card[0x100] == 0x42) failures++;
    if (ps2_sram_transfer(PS2_SRAM_ADDRESS + PS2_SRAM_SIZE - 1, slot, 4, 1) != -1 ||
        ps2_sram_transfer(0, readback, 4, 0) != -1 ||
        ps2_sram_transfer(PS2_SRAM_ADDRESS, NULL, 4, 0) != -1 ||
        ps2_sram_transfer(PS2_SRAM_ADDRESS + PS2_SRAM_SIZE, NULL, 0, 0)) failures++;
    printf("[PS2 SRAM TEST] %s: blank, partial writes, reload, failure and bounds (%d)\n",
        failures ? "FAIL" : "PASS", failures);
    return failures;
}
