#include "libc64/os_malloc.h"
#include "ps2/ps2_queue.h"
extern int printf(const char*, ...);

#define CHECK(expr) do { if (!(expr)) { \
    printf("[PS2 QUEUE HEAP TEST] FAIL line %d: %s\n", __LINE__, #expr); return -1; \
} } while (0)

int ps2_queue_heap_smoke(void) {
    static unsigned char memory[32768] __attribute__((aligned(16)));
    Arena arena;
    OSMesgQueue survivor;
    OSMesg survivorMessage;
    osCreateMesgQueue(&survivor, &survivorMessage, 1);
    CHECK(osSendMesg(&survivor, (OSMesg)42, OS_MESG_NOBLOCK) == 0);
    /* Completed one-shot queues are not necessarily heap allocations. Use
     * more distinct addresses than the side table can retain, just like
     * temporary DMA queues and reused animation-task union slots. */
    static struct {
        OSMesgQueue queue;
        OSMesg message;
    } completed[96];
    for (int pass = 0; pass < 4; pass++) {
        for (int i = 0; i < 96; i++) {
            OSMesgQueue* queue = &completed[i].queue;
            osCreateMesgQueue(queue, &completed[i].message, 1);
            CHECK(osSendMesg(queue, (OSMesg)(i + 1), OS_MESG_NOBLOCK) == 0);
            OSMesg result;
            CHECK(osRecvMesg(queue, &result, OS_MESG_BLOCK) == 0 && result == (OSMesg)(i + 1));
            ps2_os_release_queues(queue, sizeof(*queue));
            CHECK(osSendMesg(queue, (OSMesg)7, OS_MESG_NOBLOCK) == -1);
            CHECK(osSendMesg(&survivor, (OSMesg)43, OS_MESG_NOBLOCK) == -1);
        }
    }
    /* Distinct embedded queue addresses expose the original 64-entry leak.
     * Exercise both freeing an allocation and discarding a whole scene heap. */
    for (int scene = 0; scene < 8; scene++) {
        __osMallocInit(&arena, memory, sizeof(memory));
        for (int i = 0; i < 96; i++) {
            void* allocation = __osMalloc(&arena, 4096);
            CHECK(allocation != NULL);
            OSMesgQueue* queue = (OSMesgQueue*)((u8*)allocation + i * 32);
            OSMesg* message = (OSMesg*)((u8*)queue + sizeof(*queue));
            osCreateMesgQueue(queue, message, 1);
            CHECK(osSendMesg(queue, (OSMesg)7, OS_MESG_NOBLOCK) == 0);
            __osFree(&arena, allocation);
            CHECK(osSendMesg(queue, (OSMesg)8, OS_MESG_NOBLOCK) == -1);
        }
        OSMesgQueue* queue = __osMalloc(&arena, 64);
        CHECK(queue != NULL);
        osCreateMesgQueue(queue, (OSMesg*)(queue + 1), 1);
        __osMallocCleanup(&arena);
        CHECK(osSendMesg(queue, (OSMesg)9, OS_MESG_NOBLOCK) == -1);
    }
    OSMesg result;
    CHECK(osRecvMesg(&survivor, &result, OS_MESG_NOBLOCK) == 0 && result == (OSMesg)42);
    ps2_os_release_queues(&survivor, sizeof(survivor));
    printf("[PS2 QUEUE HEAP TEST] PASS: 384 completed + 768 heap queue lifetimes, scene cleanup, surviving queue\n");
    return 0;
}
