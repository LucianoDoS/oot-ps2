/* Shared test-only task metadata. No production ELF includes this header. */
#ifndef PS2_PERF_SCENE_H
#define PS2_PERF_SCENE_H
#ifndef PS2_PERF_SCENE_FRAMES
#define PS2_PERF_SCENE_FRAMES 120
#endif
#ifndef PS2_PERF_SCENE_WARMUP
#define PS2_PERF_SCENE_WARMUP 30
#endif
#define PS2_PERF_SCENE_VIEWS 3
#define PS2_PERF_SCENE_SEED 0x12345678u
typedef struct {
    const void* root;
    unsigned view, frame, capture;
} PS2PerfTaskTag;
extern PS2PerfTaskTag gPerfTaskTags[2];
extern volatile unsigned gPerfViewComplete;
extern volatile unsigned gPerfCaptureDone;
#endif
