#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "actor.h"
#include "ps2/ps2_actor_overlays.h"

typedef struct {
    unsigned char* dataStart;
    unsigned char* dataEnd;
    unsigned char* bssStart;
    unsigned char* bssEnd;
} ActorBank;

#include "actor_overlays.inc"

extern unsigned char ps2_actor_data_start[], ps2_actor_data_end[];
static unsigned char* sInitialData;
static unsigned char sLoaded[ACTOR_ID_MAX];

int ps2_actor_overlays_init(void) {
    size_t size = ps2_actor_data_end - ps2_actor_data_start;
    sInitialData = malloc(size ? size : 1);
    if (!sInitialData) return 0;
    memcpy(sInitialData, ps2_actor_data_start, size);
    printf("[PS2 ACTOR OVERLAYS] initial writable data=%u bytes\n", (unsigned)size);
    return 1;
}

void ps2_actor_overlays_new_scene(void) {
    memset(sLoaded, 0, sizeof(sLoaded));
}

void ps2_actor_overlay_load(int actorId, int clients, int persistent) {
    const ActorBank* bank = &sActorBanks[actorId];
    if (!bank->dataStart || clients || (persistent && sLoaded[actorId])) return;
    // N64 reloads .data and clears .bss on a fresh overlay load. Code and
    // read-only assets stay resident; live actors keep their shared state.
    memcpy(bank->dataStart, sInitialData + (bank->dataStart - ps2_actor_data_start),
           bank->dataEnd - bank->dataStart);
    memset(bank->bssStart, 0, bank->bssEnd - bank->bssStart);
    sLoaded[actorId] = 1;
}
