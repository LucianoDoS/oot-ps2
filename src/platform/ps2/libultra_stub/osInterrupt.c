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
