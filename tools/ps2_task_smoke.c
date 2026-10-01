/* Exercise the real task bridge/event/queue code with a deliberately slow
 * renderer. Audio completion must precede DP completion, on repeated tasks. */
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
static volatile int drawing, audio, rendered, failures;
void rdp_to_gs_exec(const Gfx* data) {
    if (!data || *(const u32*)data != 0x12345678) failures++;
    drawing = 1;
    usleep(20000);
}
void rdp_to_gs_flip(void) { rendered++; drawing = 0; }
void PS2Audio_ProcessTask(void* data, u32 size) {
    if (!drawing || !(osDpGetStatus() & (1u << 5)) || size != 8 || *(u32*)data != 0x87654321) failures++;
    audio++;
}
int main(void) {
    Queue queue;
    void* messages[8], *message;
    osCreateMesgQueue(&queue, messages, 8);
    osSetEventMesg(4, &queue, (void*)1); /* SP */
    osSetEventMesg(9, &queue, (void*)2); /* DP */
    Gfx commands = 0x12345678;
    u32 samples[2] = {0x87654321,0};
    Task graphics = {0}, sound = {0};
    graphics.words[0] = 1; graphics.words[12] = (u32)&commands;
    sound.words[0] = 2; sound.words[12] = (u32)samples; sound.words[13] = sizeof(samples);
    for (int i = 0; i < 64; i++) {
        osSpTaskStartGo(&graphics);
        if (osRecvMesg(&queue, &message, 1) || message != (void*)1) failures++;
        /* Allow the worker to begin its slow render, then dispatch audio. */
        usleep(1000);
        if (!drawing) failures++;
        osSpTaskStartGo(&sound);
        if (osRecvMesg(&queue, &message, 1) || message != (void*)1) failures++;
        if (osRecvMesg(&queue, &message, 1) || message != (void*)2) failures++;
        if (osDpGetStatus() & (1u << 5)) failures++;
    }
    if (audio != 64 || rendered != 64) failures++;
    printf("[PS2 TASK TEST] %s: 64 slow graphics tasks, audio before DP, ordered completion (%d)\n",
           failures ? "FAIL" : "PASS", failures);
    fflush(stdout);
    SleepThread();
    return failures != 0;
}
