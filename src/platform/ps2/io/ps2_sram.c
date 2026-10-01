/* Cartridge SRAM shadow, backed by the port's memory-card file. Partial
 * writes must preserve the other slots and the three option bytes. */
#include <kernel.h>
#include <string.h>
#include <stdio.h>
#include "ps2_types.h"
#include "ps2_sram.h"

extern int ps2_sram_load_from_card(void* data, u32 size);
extern int ps2_sram_flush_to_card(const void* data, u32 size);

static u8 s_sram[PS2_SRAM_SIZE] __attribute__((aligned(64)));
static int s_loaded;
static int s_mutex = -1;

void ps2_sram_init(void) {
    if (s_mutex < 0) {
        ee_sema_t sema = {0};
        sema.init_count = sema.max_count = 1;
        s_mutex = CreateSema(&sema);
    }
    if (s_mutex < 0) return;
    WaitSema(s_mutex);
    memset(s_sram, 0, sizeof(s_sram));
    s_loaded = 0;
    SignalSema(s_mutex);
}

int ps2_sram_transfer(unsigned int address, void* data, unsigned int size, int write) {
    u32 offset;
    /* Accept physical SRAM addresses and their N64 KSEG aliases. */
    address &= 0x1fffffffu;
    if (address < PS2_SRAM_ADDRESS) return -1;
    offset = address - PS2_SRAM_ADDRESS;
    if ((!data && size) || offset > PS2_SRAM_SIZE || size > PS2_SRAM_SIZE - offset) return -1;
    if (!size) return 0;
    if (s_mutex < 0) ps2_sram_init();
    if (s_mutex < 0) return -1;
    WaitSema(s_mutex);
    if (!s_loaded) {
        /* A missing card/file starts with blank SRAM. Reads still complete;
         * the game initializes its own header and slot checksums. */
        memset(s_sram, 0, sizeof(s_sram));
        if (ps2_sram_load_from_card(s_sram, sizeof(s_sram)) != 0)
            memset(s_sram, 0, sizeof(s_sram));
        s_loaded = 1;
    }
    int result = 0;
    if (write) {
        memcpy(s_sram + offset, data, size);
        result = ps2_sram_flush_to_card(s_sram, sizeof(s_sram));
        if (result) printf("[PS2 SRAM] Memory-card write failed; SRAM retained in RAM\n");
    } else {
        memcpy(data, s_sram + offset, size);
    }
    SignalSema(s_mutex);
    return result;
}
