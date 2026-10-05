/* Test-only USB fixture. Uses a fresh debug save and no memory card/input.
 * Exercise normal fade-out, destruction, scene loading and fade-in, rather
 * than skipping directly to a new Play_Init as a scene sweep does. */
#include "console_logo_state.h"
#include "map_select_state.h"
#include "play_state.h"
#include "save.h"
#include "transition.h"
#include "libc/string.h"
#include "ultra64.h"

extern void MapSelect_LoadGame(MapSelectState*, s32);
static unsigned sPhase, sFrames;
static const s16 sScenes[] = { SCENE_LINKS_HOUSE, SCENE_KOKIRI_FOREST, SCENE_LINKS_HOUSE };
static const s16 sEntrances[] = { ENTR_KOKIRI_FOREST_3, ENTR_LINKS_HOUSE_0 };
static void empty_state(GameState* state) { (void)state; }
static void delayed_start(GameState* state) {
    /* Let the startup batch drain before the deliberately early scene load. */
    if (state->frames != 120) return;
    MapSelect_LoadGame((MapSelectState*)state, ENTR_LINKS_HOUSE_0);
    osSyncPrintf("[PS2 USB TRANSITION] BEGIN debug save; player cards/input disabled\n");
}

void __wrap_ConsoleLogo_Init(GameState* state) {
    state->main = delayed_start; state->destroy = empty_state;
    SaveContext_Init();
    gSaveContext.fileNum = 0xff;
    gSaveContext.save.linkAge = LINK_AGE_CHILD;
    gSaveContext.save.dayTime = 0x8000;
}

void __wrap_GameState_ReqPadData(GameState* state) {
    memset(state->input, 0, sizeof(state->input));
    if (state->main != Play_Main || sPhase >= 3) return;
    PlayState* play = (PlayState*)state;
    if (play->sceneId != sScenes[sPhase]) return;
    if (++sFrames != 120) return;
    osSyncPrintf("[PS2 USB TRANSITION] PASS phase=%u scene=%d frames=%u\n", sPhase, play->sceneId, sFrames);
    if (sPhase == 2) {
        sPhase++;
        osSyncPrintf("[PS2 USB TRANSITION RESULT] PASS house-forest-house\n");
        return;
    }
    play->nextEntranceIndex = sEntrances[sPhase];
    play->transitionType = TRANS_TYPE_FADE_BLACK_FAST;
    play->transitionTrigger = TRANS_TRIGGER_START;
    osSyncPrintf("[PS2 USB TRANSITION] EXIT scene=%d entrance=%04x\n", play->sceneId, play->nextEntranceIndex);
    sPhase++; sFrames = 0;
}
