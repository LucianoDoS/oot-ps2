#include <kernel.h>
#include <string.h>
#include "ps2_types.h"

extern s32 osEPiReadIo(void*, u32, u32*);
extern void* _gp;
static u8 stacks[2][16384] __attribute__((aligned(16)));
static volatile int failures[2];
static int done;

static void reader(void* argument) {
    int index = (int)(uintptr_t)argument;
    for (u32 i = 0; i < 100; i++) {
        u32 offset = ((i * 7 + index * 31) % 63) * 4;
        u32 expected = (offset << 24) | ((offset + 1) << 16) | ((offset + 2) << 8) | (offset + 3);
        u32 actual;
        if (osEPiReadIo(NULL, offset, &actual) != 0 || actual != expected) {
            failures[index] = 1;
            break;
        }
    }
    SignalSema(done);
    ExitThread();
}

int ps2_rom_threads_smoke(void) {
    ee_sema_t semaphore;
    memset(&semaphore, 0, sizeof(semaphore));
    semaphore.max_count = 2;
    done = CreateSema(&semaphore);
    if (done < 0) return 1;
    int ids[2] = {-1, -1};
    int result = 0;
    for (int i = 0; i < 2; i++) {
        ee_thread_t thread;
        memset(&thread, 0, sizeof(thread));
        thread.func = reader;
        thread.stack = stacks[i];
        thread.stack_size = sizeof(stacks[i]);
        thread.gp_reg = &_gp;
        thread.initial_priority = 80;
        failures[i] = 0;
        ids[i] = CreateThread(&thread);
        if (ids[i] < 0 || StartThread(ids[i], (void*)(uintptr_t)i) < 0) {
            result = 2;
            goto cleanup;
        }
    }
    WaitSema(done);
    WaitSema(done);
    result = failures[0] || failures[1] ? 3 : 0;
cleanup:
    for (int i = 0; i < 2; i++) {
        if (ids[i] >= 0) {
            TerminateThread(ids[i]);
            DeleteThread(ids[i]);
        }
    }
    DeleteSema(done);
    return result;
}
