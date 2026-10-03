/* Exercise the real task bridge/event/queue code with a deliberately slow
 * renderer and delayed presentation. Audio must precede DP; a pending task
 * must be accepted during presentation without overwriting the active one. */
#include <kernel.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "ps2_types.h"
#include "rdp_to_gs.h"
typedef struct { void* a; void* b; s32 count, first, size; void** messages; } Queue;
typedef union { u32 words[16]; u64 alignment; } Task;
extern void osCreateMesgQueue(Queue*, void**, int);
extern int osRecvMesg(Queue*, void**, int);
extern void osSetEventMesg(int, Queue*, void*);
extern void osSpTaskStartGo(Task*);
extern u32 osDpGetStatus(void);
extern void ps2_timer_init(void);
static volatile int drawing, audio, rendered, submitted, started, presenting, failures;
extern u64 osGetTime(void);
void rdp_to_gs_exec(const Gfx* data) {
    if (!data || *(const u32*)data != 0x12345678u+(unsigned)started ||
        presenting || rendered != started) failures++;
    started++;
    drawing = 1;
    usleep(20000);
}
void rdp_to_gs_submit(void) { submitted++; drawing = 0; }
void rdp_to_gs_present(void) {
    presenting = 1;
    usleep(20000);
    rendered++;
    presenting = 0;
}
void rdp_to_gs_flip(void) { rdp_to_gs_submit(); rdp_to_gs_present(); }
void PS2Audio_ProcessTask(void* data, u32 size) {
    if (!drawing || !(osDpGetStatus() & (1u << 5)) || size != 8 || *(u32*)data != 0x87654321) failures++;
    audio++;
}
int main(void) {
    ps2_timer_init();
    Queue queue;
    void* messages[8], *message;
    osCreateMesgQueue(&queue, messages, 8);
    osSetEventMesg(4, &queue, (void*)1); /* SP */
    osSetEventMesg(9, &queue, (void*)2); /* DP */
    Gfx commands[64];
    for (int i=0;i<64;i++) commands[i]=0x12345678u+i;
    u32 samples[2] = {0x87654321,0};
    Task graphics = {0}, sound = {0};
    graphics.words[0] = 1;
    sound.words[0] = 2; sound.words[12] = (u32)samples; sound.words[13] = sizeof(samples);
    for (int i = 0; i < 64; i++) {
        graphics.words[12] = (u32)&commands[i];
        if (i && !presenting) failures++;
        u64 begin = osGetTime();
        osSpTaskStartGo(&graphics);
        /* A 20 ms presentation wait must not block the scheduler mailbox. */
        if (osGetTime()-begin > 46875000ull/100) failures++;
        if (osRecvMesg(&queue, &message, 1) || message != (void*)1) failures++;
        /* Queued work starts only after the preceding presentation. */
        for (unsigned wait=0; !drawing && wait<120; wait++) usleep(500);
        if (!drawing) failures++;
        osSpTaskStartGo(&sound);
        if (osRecvMesg(&queue, &message, 1) || message != (void*)1) failures++;
        if (osRecvMesg(&queue, &message, 1) || message != (void*)2) failures++;
        if ((osDpGetStatus() & (1u << 5)) || drawing || submitted != i+1) failures++;
        usleep(1000);
        if (!presenting || rendered != i) failures++;
    }
    for (unsigned wait=0; rendered!=64 && wait<120; wait++) usleep(500);
    if (audio != 64 || rendered != 64 || submitted != 64 || started != 64) failures++;
    printf("[PS2 TASK TEST] %s: 64 queued frames, audio before DP, FINISH before release, nonblocking mailbox and serialized presentation (%d)\n",
           failures ? "FAIL" : "PASS", failures);
    fflush(stdout);
    SleepThread();
    return failures != 0;
}
