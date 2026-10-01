/* Run against the actual EE kernel, not host-side fake SDK headers. */
#include <stdio.h>
#include <string.h>
#include "ps2_types.h"

typedef struct {
    void* mtqueue;
    void* fullqueue;
    s32 validCount, first, msgCount;
    void** msg;
} SmokeQueue;
typedef struct SmokeTimer {
    struct SmokeTimer *next, *prev;
    u64 interval, value;
    SmokeQueue* mq;
    void* msg;
} SmokeTimer;
_Static_assert(sizeof(SmokeQueue) == 0x18, "libultra queue ABI");
_Static_assert(sizeof(SmokeTimer) == 0x20, "libultra timer ABI");

extern void ps2_mesgqueue_init(void);
extern void ps2_timer_init(void);
extern void ps2_timer_tick(void);
extern void osCreateMesgQueue(SmokeQueue*, void**, s32);
extern s32 osSendMesg(SmokeQueue*, void*, s32);
extern s32 ps2_os_send_coalesced(SmokeQueue*, void*);
extern s32 osRecvMesg(SmokeQueue*, void**, s32);
extern s32 osSetTimer(SmokeTimer*, u64, u64, SmokeQueue*, void*);
extern s32 osStopTimer(SmokeTimer*);
extern u64 osGetTime(void);
extern s32 __osDisableInt(void);
extern void __osRestoreInt(s32 previous);

#define CHECK(expr) do { if (!(expr)) { \
    printf("[PS2 OS TEST] FAIL line %d: %s\n", __LINE__, #expr); \
    fflush(stdout); return -1; } } while (0)

int ps2_os_smoke(void) {
    struct { u32 before; SmokeQueue queue; u32 after; } guarded;
    void* slots[2];
    void* message = NULL;
    SmokeTimer timer;
    ps2_mesgqueue_init();
    ps2_timer_init();
    s32 outer = __osDisableInt();
    s32 inner = __osDisableInt();
    __osRestoreInt(inner);
    s32 stillDisabled = __osDisableInt();
    __osRestoreInt(outer);
    s32 restored = __osDisableInt();
    __osRestoreInt(restored);
    CHECK(outer != 0 && inner == 0 && stillDisabled == 0 && restored != 0);
    memset(&guarded, 0xA5, sizeof(guarded));
    osCreateMesgQueue(&guarded.queue, slots, 2);
    CHECK(guarded.before == 0xA5A5A5A5 && guarded.after == 0xA5A5A5A5);
    CHECK(osRecvMesg(&guarded.queue, &message, 0) == -1);
    CHECK(osSendMesg(&guarded.queue, (void*)1, 0) == 0);
    CHECK(osSendMesg(&guarded.queue, (void*)2, 0) == 0);
    CHECK(osSendMesg(&guarded.queue, (void*)3, 0) == -1);
    CHECK(osRecvMesg(&guarded.queue, &message, 0) == 0 && message == (void*)1);
    CHECK(osSendMesg(&guarded.queue, (void*)3, 0) == 0);
    CHECK(osRecvMesg(&guarded.queue, &message, 0) == 0 && message == (void*)2);
    CHECK(osRecvMesg(&guarded.queue, &message, 0) == 0 && message == (void*)3);
    CHECK(guarded.queue.validCount == 0);
    /* A slow native frame must retain space for completion notifications. */
    for (int i = 0; i < 1000; i++)
        CHECK(ps2_os_send_coalesced(&guarded.queue, (void*)1) == 0);
    CHECK(guarded.queue.validCount == 1);
    CHECK(osSendMesg(&guarded.queue, (void*)2, 0) == 0);
    CHECK(ps2_os_send_coalesced(&guarded.queue, (void*)1) == 0);
    CHECK(ps2_os_send_coalesced(&guarded.queue, (void*)3) == -1);
    CHECK(osRecvMesg(&guarded.queue, &message, 0) == 0 && message == (void*)1);
    CHECK(osRecvMesg(&guarded.queue, &message, 0) == 0 && message == (void*)2);

    /* A new timer is not required to have been zero-initialized by the game. */
    memset(&timer, 0xA5, sizeof(timer));
    CHECK(osSetTimer(&timer, 46875, 0, &guarded.queue, (void*)4) == 0);
    u64 begin = osGetTime(), previous = begin;
    while (osGetTime() - begin < 4687500) {
        u64 now = osGetTime();
        CHECK(now >= previous);
        previous = now;
        ps2_timer_tick();
    }
    CHECK(osRecvMesg(&guarded.queue, &message, 0) == 0 && message == (void*)4);
    CHECK(osRecvMesg(&guarded.queue, &message, 0) == -1);
    CHECK(timer.next == NULL && timer.prev == NULL);
    CHECK(osStopTimer(&timer) == 0);
    CHECK(guarded.before == 0xA5A5A5A5 && guarded.after == 0xA5A5A5A5);
    printf("[PS2 OS TEST] PASS: queue ABI, FIFO/full/empty, timer, monotonic clock\n");
    fflush(stdout);
    return 0;
}
