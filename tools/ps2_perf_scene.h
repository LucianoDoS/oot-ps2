/* Shared test-only task metadata. No production ELF includes this header. */
#ifndef PS2_PERF_SCENE_H
#define PS2_PERF_SCENE_H
#ifndef PS2_PERF_SCENE_FRAMES
#define PS2_PERF_SCENE_FRAMES 120
#endif
#ifndef PS2_PERF_SCENE_WARMUP
#define PS2_PERF_SCENE_WARMUP 30
#endif
#ifndef PS2_PERF_SCENE_TITLE_CINEMATIC
#define PS2_PERF_SCENE_TITLE_CINEMATIC 0
#endif
#ifndef PS2_PERF_SCENE_CASTLE_GATE
#define PS2_PERF_SCENE_CASTLE_GATE 0
#endif
#ifndef PS2_PERF_SCENE_DAY_TIME
#define PS2_PERF_SCENE_DAY_TIME 43690
#endif
#if PS2_PERF_SCENE_TITLE_CINEMATIC && PS2_PERF_SCENE_CASTLE_GATE
#error "Select only one benchmark mode"
#endif
#if PS2_PERF_SCENE_TITLE_CINEMATIC
#define PS2_PERF_SCENE_VIEWS 1
#else
#define PS2_PERF_SCENE_VIEWS 3
#endif
#define PS2_PERF_SCENE_SEED 0x12345678u
#if PS2_PERF_SCENE_TITLE_CINEMATIC
typedef struct {
    unsigned scene, room, gameMode, linkAge, cutsceneState, cutsceneFrame, riding, horseDrawable;
    int eyeX, eyeY, eyeZ, atX, atY, atZ, fovMilli, playerX, playerY, playerZ;
} PS2PerfMotionTag;
#endif
#if PS2_PERF_SCENE_CASTLE_GATE
typedef struct {
    unsigned scene, room, dayTime, sunDisabled, skyboxDisabled, skyboxConfig, cutsceneState;
    int sunX, sunY, sunZ, screenX, screenY, glareMilli, flareMilli;
} PS2PerfEnvironmentTag;
#endif
typedef struct {
    const void* root;
    unsigned view, frame, capture;
#if PS2_PERF_SCENE_TITLE_CINEMATIC
    PS2PerfMotionTag motion;
#endif
#if PS2_PERF_SCENE_CASTLE_GATE
    PS2PerfEnvironmentTag environment;
#endif
} PS2PerfTaskTag;
extern PS2PerfTaskTag gPerfTaskTags[2];
extern volatile unsigned gPerfViewComplete;
extern volatile unsigned gPerfCaptureDone;
#endif
