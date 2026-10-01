/* Native EE adapter for the licensed OoT NEAD audio synthesis interpreter. */
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "rsp_hle/hle_internal.h"
#include "rsp_hle/hle_external.h"
#include "rsp_hle/memory.h"

static struct hle_t s_audio;
static uint32_t s_dmem[1024] __attribute__((aligned(16)));
static unsigned s_errors;

void HleWarnMessage(void* ignored, const char* format, ...) {
    (void)ignored;
    if (++s_errors > 8) return;
    va_list args;
    va_start(args, format);
    printf("[PS2 DSP] ");
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

/* Completion belongs to the native scheduler; there are no emulated registers. */
void rsp_break(struct hle_t* hle, unsigned setbits) { (void)hle; (void)setbits; }
int HleForwardTask(void* ignored) { (void)ignored; return -1; }

int PS2RspAudio_Process(void* commands, unsigned size) {
    uintptr_t address = (uintptr_t)commands;
    if (!commands || (address & 7) || (size & 7) || size > 0x10000 ||
        address < 0x100000 || address + size > 0x1000000) return -1;
    s_audio.dmem = (unsigned char*)s_dmem;
    /* RDRAM helpers receive real EE pointers encoded in the game command list. */
    s_audio.dram = NULL;
    s_dmem[TASK_DATA_PTR / 4] = address;
    s_dmem[TASK_DATA_SIZE / 4] = size;
    unsigned before = s_errors;
    alist_process_nead_oot(&s_audio);
    return s_errors == before ? 0 : -1;
}
