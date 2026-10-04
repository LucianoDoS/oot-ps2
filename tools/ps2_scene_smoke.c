/* Test-only linker wrappers. The normal game never links this file. */
#include "array_count.h"
#include "console_logo_state.h"
#include "game.h"
#include "map_select_state.h"
#include "play_state.h"
#include "save.h"
#include "sram.h"
#include "libc/string.h"
#include "ultra64.h"
#include "scene_smoke_cases.h"

extern void MapSelect_LoadGame(MapSelectState*, s32);
extern void __real_GameState_Update(GameState*);
extern void __real_GameState_Destroy(GameState*);
extern int ps2_actor_overlay_smoke(void);
static unsigned sCase, sFrames, sFailures;
static int sAdvance;

static void prepare_case(GameState* state) {
    SaveContext_Init();
    gSaveContext.fileNum = 0xff;
    gSaveContext.save.linkAge = sSceneCases[sCase].age;
    gSaveContext.save.dayTime = sSceneCases[sCase].time;
    MapSelect_LoadGame((MapSelectState*)state, sSceneCases[sCase].entrance);
    sFrames = 0;
    osSyncPrintf("[PS2 SCENE SWEEP] BEGIN case=%u scene=%u age=%u entrance=%04x name=%s\n",
        sCase, sSceneCases[sCase].scene, sSceneCases[sCase].age,
        sSceneCases[sCase].entrance, sSceneCases[sCase].name);
}

static void empty_state(GameState* state) { (void)state; }

void __wrap_ConsoleLogo_Init(GameState* state) {
    state->main = empty_state;
    state->destroy = empty_state;
    sFailures += ps2_actor_overlay_smoke();
    osSyncPrintf("[PS2 SCENE SWEEP] cases=%u frames=%u; debug saves, input disabled\n",
        (unsigned)ARRAY_COUNT(sSceneCases), PS2_SCENE_SMOKE_FRAMES);
    prepare_case(state);
}

void __wrap_GameState_ReqPadData(GameState* state) {
    memset(state->input, 0, sizeof(state->input));
}

void __wrap_GameState_Update(GameState* state) {
    int isPlay = state->main == Play_Main;
    __real_GameState_Update(state);
    if (!isPlay) return;
    PlayState* play = (PlayState*)state;
    sFrames++;
    if (sFrames < PS2_SCENE_SMOKE_FRAMES && state->running) return;
    int correct = play->sceneId == sSceneCases[sCase].scene && sFrames >= PS2_SCENE_SMOKE_FRAMES;
    sFailures += !correct;
    osSyncPrintf("[PS2 SCENE SWEEP] %s case=%u expected=%u actual=%d age=%u frames=%u room=%d\n",
        correct ? "PASS" : "FAIL", sCase, sSceneCases[sCase].scene,
        play->sceneId, sSceneCases[sCase].age, sFrames, play->roomCtx.curRoom.num);
    state->running = false;
    if (sCase + 1 == ARRAY_COUNT(sSceneCases)) {
        state->init = NULL;
        state->size = 0;
        osSyncPrintf("[PS2 SCENE SWEEP RESULT] %s cases=%u failures=%u\n",
            sFailures ? "FAIL" : "PASS", (unsigned)ARRAY_COUNT(sSceneCases), sFailures);
    } else {
        SET_NEXT_GAMESTATE(&play->state, Play_Init, PlayState);
        sAdvance = 1;
    }
}

void __wrap_GameState_Destroy(GameState* state) {
    __real_GameState_Destroy(state);
    if (sAdvance) {
        sAdvance = 0;
        sCase++;
        // The old actors are destroyed before the next debug save is prepared.
        prepare_case(state);
    }
}
