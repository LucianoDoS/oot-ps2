#include "ultra64.h"
#if PLATFORM_PS2
#include "ps2/ps2_queue.h"
#endif

void Sleep_Cycles(OSTime cycles) {
    OSMesgQueue mq;
    OSMesg msg;
    OSTimer timer;

    osCreateMesgQueue(&mq, &msg, 1);
    osSetTimer(&timer, cycles, 0, &mq, NULL);
    osRecvMesg(&mq, NULL, OS_MESG_BLOCK);
#if PLATFORM_PS2
    ps2_os_release_queues(&mq, sizeof(mq));
#endif
}

void Sleep_Nsec(u32 nsec) {
    Sleep_Cycles(OS_NSEC_TO_CYCLES(nsec));
}

void Sleep_Usec(u32 usec) {
    Sleep_Cycles(OS_USEC_TO_CYCLES(usec));
}

// originally "msleep"
void Sleep_Msec(u32 ms) {
    Sleep_Cycles((ms * OS_CPU_COUNTER) / 1000ull);
}

void Sleep_Sec(u32 sec) {
    Sleep_Cycles(sec * OS_CPU_COUNTER);
}
