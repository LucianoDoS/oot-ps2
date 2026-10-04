/* Exercise real resident sections before creating any actors. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "actor.h"
#include "ps2/ps2_actor_overlays.h"

typedef struct { u8* dataStart; u8* dataEnd; u8* bssStart; u8* bssEnd; } ActorBank;
#include "actor_overlays.inc"
extern u8 ps2_actor_data_start[], ps2_actor_data_end[];

static int bytes_equal_value(const u8* begin, const u8* end, u8 value) {
    while (begin < end) if (*begin++ != value) return 0;
    return 1;
}

int ps2_actor_overlay_smoke(void) {
    size_t size = ps2_actor_data_end - ps2_actor_data_start;
    u8* original = malloc(size ? size : 1);
    unsigned failures = 0, banks = 0, dataBytes = 0, bssBytes = 0;
    if (!original) return 1;
    memcpy(original, ps2_actor_data_start, size);
    for (int id = 0; id < ACTOR_ID_MAX; id++) {
        const ActorBank* bank = &sActorBanks[id];
        if (!bank->dataStart) continue;
        size_t ds = bank->dataEnd - bank->dataStart, bs = bank->bssEnd - bank->bssStart;
        const u8* expected = original + (bank->dataStart - ps2_actor_data_start);
        banks++; dataBytes += ds; bssBytes += bs;
        memset(bank->dataStart, 0xa5, ds);
        memset(bank->bssStart, 0x5a, bs);
        ps2_actor_overlay_load(id, 1, 0);
        failures += !bytes_equal_value(bank->dataStart, bank->dataEnd, 0xa5);
        failures += !bytes_equal_value(bank->bssStart, bank->bssEnd, 0x5a);
        ps2_actor_overlay_load(id, 0, 0);
        failures += memcmp(bank->dataStart, expected, ds) != 0;
        failures += !bytes_equal_value(bank->bssStart, bank->bssEnd, 0);
        memset(bank->dataStart, 0xa5, ds);
        memset(bank->bssStart, 0x5a, bs);
        ps2_actor_overlay_load(id, 0, 1);
        failures += !bytes_equal_value(bank->dataStart, bank->dataEnd, 0xa5);
        failures += !bytes_equal_value(bank->bssStart, bank->bssEnd, 0x5a);
        ps2_actor_overlays_new_scene();
        ps2_actor_overlay_load(id, 0, 1);
        failures += memcmp(bank->dataStart, expected, ds) != 0;
        failures += !bytes_equal_value(bank->bssStart, bank->bssEnd, 0);
    }
    // Internal actors, including Player, have no overlay sections.
    ps2_actor_overlay_load(ACTOR_PLAYER, 0, 0);
    ps2_actor_overlays_new_scene();
    free(original);
    printf("[PS2 ACTOR OVERLAY TEST] banks=%u dataBytes=%u bssBytes=%u failures=%u\n",
                 banks, dataBytes, bssBytes, failures);
    return failures != 0;
}
