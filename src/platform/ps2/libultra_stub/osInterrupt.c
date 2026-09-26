/* libultra's saved interrupt token is opaque to callers. */
#include <kernel.h>
#include "ps2_types.h"

s32 __osDisableInt(void) {
    return (s32)DIntr();
}

void __osRestoreInt(s32 previous) {
    if (previous) {
        EIntr();
    }
}

u32 __osGetFpcCsr(void) {
    u32 value;
    __asm__ volatile("cfc1 %0, $31" : "=r"(value));
    return value;
}

u32 __osSetFpcCsr(u32 value) {
    u32 previous = __osGetFpcCsr();
    __asm__ volatile("ctc1 %0, $31\n\tnop\n\tnop" : : "r"(value));
    return previous;
}
