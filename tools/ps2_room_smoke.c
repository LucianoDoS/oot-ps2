/* Test-only room loading and real event APIs. No controller or player card. */
#include "array_count.h"
#include "console_logo_state.h"
#include "game.h"
#include "gfx.h"
#include "map_select_state.h"
#include "play_state.h"
#include "player.h"
#include "interface.h"
#include "save.h"
#include "sram.h"
#include "libc/string.h"
#include "ultra64.h"
#include "ps2/ps2_offscreen.h"
#include "room_smoke_cases.h"

extern void MapSelect_LoadGame(MapSelectState*, s32);
extern void __real_GameState_Update(GameState*);
extern void __real_GameState_Destroy(GameState*);
extern u32 __real_Room_SetupFirstRoom(PlayState*, RoomContext*);
extern s32 __real_Room_RequestNewRoom(PlayState*, RoomContext*, s32);
extern PS2RenderTarget gRoomCaptureTarget;
extern volatile unsigned gRoomCaptureDone;
extern unsigned gRoomCaptureCase;
static unsigned sCase, sFrames, sFailures;
static int sAdvance, sCaptureRequested;
static ActorEntry sPlayerEntry;
static SaveContext sSaveSnapshot;
static unsigned sBlockedTransitions;

static void prepare_case(GameState* state) {
    SaveContext_Init();
    gSaveContext.fileNum = 0xff;
    gSaveContext.save.linkAge = sRoomCases[sCase].age;
    gSaveContext.save.dayTime = sRoomCases[sCase].time;
    MapSelect_LoadGame((MapSelectState*)state, sRoomCases[sCase].entrance);
    sFrames = sCaptureRequested = gRoomCaptureDone = 0;
    sBlockedTransitions = 0;
    osSyncPrintf("[PS2 ROOM SWEEP] BEGIN case=%u scene=%u age=%u room=%u name=%s\n",
        sCase, sRoomCases[sCase].scene, sRoomCases[sCase].age, sRoomCases[sCase].room, sRoomCases[sCase].name);
}

s32 __wrap_Room_RequestNewRoom(PlayState* play, RoomContext* roomCtx, s32 room) {
    /* This sweep tests resident rooms independently, not natural door crossing.
     * Synthetic camera/player positions must not select a different fixture. */
    if (room != sRoomCases[sCase].room) { sBlockedTransitions++; return false; }
    return __real_Room_RequestNewRoom(play, roomCtx, room);
}

u32 __wrap_Room_SetupFirstRoom(PlayState* play, RoomContext* roomCtx) {
    if (sRoomCases[sCase].room >= play->roomList.count || !play->playerEntry || !play->spawnList) {
        sFailures++;
        osSyncPrintf("[PS2 ROOM SWEEP] FAIL invalid fixture case=%u\n", sCase);
        return __real_Room_SetupFirstRoom(play, roomCtx);
    }
    play->spawnList[play->spawn].room = sRoomCases[sCase].room;
    sPlayerEntry = *play->playerEntry;
    if (sRoomCases[sCase].anchored) {
        sPlayerEntry.pos.x = sRoomCases[sCase].x;
        sPlayerEntry.pos.y = sRoomCases[sCase].y;
        sPlayerEntry.pos.z = sRoomCases[sCase].z;
        sPlayerEntry.rot.y = sRoomCases[sCase].yaw;
    }
    play->playerEntry = &sPlayerEntry;
    return __real_Room_SetupFirstRoom(play, roomCtx);
}

static void empty_state(GameState* state) { (void)state; }
void __wrap_ConsoleLogo_Init(GameState* state) {
    state->main = empty_state;
    state->destroy = empty_state;
    osSyncPrintf("[PS2 ROOM SWEEP] cases=%u frames=%u; synthetic saves/anchors, cards and input disabled\n",
                 (unsigned)ARRAY_COUNT(sRoomCases), PS2_ROOM_SMOKE_FRAMES);
    prepare_case(state);
}
void __wrap_GameState_ReqPadData(GameState* state) { memset(state->input, 0, sizeof(state->input)); }

static int check_events(PlayState* play) {
    unsigned failures = 0;
    ActorContextSceneFlags flags = play->actorCtx.flags;
    sSaveSnapshot = gSaveContext;
    gSaveContext.save.info.inventory.equipment = 0;
    Item_Give(play, ITEM_SWORD_KOKIRI);
    failures += gSaveContext.save.info.inventory.equipment != 0x0001;
    Item_Give(play, ITEM_SHIELD_DEKU);
    failures += gSaveContext.save.info.inventory.equipment != 0x0011;
    gSaveContext.rupeeAccumulator = 0;
    Item_Give(play, ITEM_RUPEE_GREEN);
    Item_Give(play, ITEM_RUPEE_BLUE);
    failures += gSaveContext.rupeeAccumulator != 6;
    for (int i = 0; i < 64; i++) {
        Flags_UnsetSwitch(play, i);
        failures += Flags_GetSwitch(play, i) != 0;
        Flags_SetSwitch(play, i);
        failures += !Flags_GetSwitch(play, i);
        Flags_UnsetSwitch(play, i);
        failures += Flags_GetSwitch(play, i) != 0;
    }
    play->actorCtx.flags.chest = 0;
    for (int i = 0; i < 32; i++) {
        failures += Flags_GetTreasure(play, i) != 0;
        Flags_SetTreasure(play, i);
        failures += !Flags_GetTreasure(play, i);
    }
    gSaveContext = sSaveSnapshot;
    play->actorCtx.flags = flags;
    osSyncPrintf("[PS2 ROOM EVENTS] %s case=%u equipment=2 rupees=2 switches=64 chests=32 restored=1 failures=%u\n",
                 failures ? "FAIL" : "PASS", sCase, failures);
    return failures != 0;
}

static int check_residents(PlayState* play) {
    unsigned count = 0, failures = 0;
    for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
        unsigned categoryCount = 0;
        for (Actor* actor = play->actorCtx.actorLists[cat].head; actor && categoryCount < 256; actor = actor->next) {
            failures += actor->id < 0 || actor->id >= ACTOR_ID_MAX || actor->category != cat;
            categoryCount++;
        }
        failures += categoryCount != (unsigned)play->actorCtx.actorLists[cat].length;
        count += categoryCount;
    }
    failures += count != play->actorCtx.total || !GET_PLAYER(play);
    for (int i = 0; i < play->objectCtx.numEntries; i++) {
        ObjectEntry* object = &play->objectCtx.slots[i];
        failures += object->id < 0 || (unsigned)object->id >= OBJECT_ID_MAX || !object->segment;
    }
    osSyncPrintf("[PS2 ROOM RESIDENTS] %s case=%u actors=%u objects=%u failures=%u\n",
                 failures ? "FAIL" : "PASS", sCase, count, play->objectCtx.numEntries, failures);
    osSyncPrintf("[PS2 ROOM FIXTURE] case=%u blockedTransitions=%u\n", sCase, sBlockedTransitions);
    return failures != 0;
}

void __wrap_GameState_Update(GameState* state) {
    int isPlay = state->main == Play_Main;
    if (isPlay && GET_PLAYER((PlayState*)state)) GET_PLAYER((PlayState*)state)->actor.freezeTimer = 3;
    __real_GameState_Update(state);
    if (!isPlay) return;
    PlayState* play = (PlayState*)state;
    sFrames++;
    if (sFrames < PS2_ROOM_SMOKE_FRAMES && state->running) return;
    if (!sCaptureRequested && state->running) {
        sFailures += check_residents(play) + check_events(play);
        gRoomCaptureCase = sCase;
        Gfx* command = state->gfxCtx->overlay.p++;
        command->words.w0 = PS2_GFX_CAPTURE_FRAME;
        command->words.w1 = (u32)&gRoomCaptureTarget;
        sCaptureRequested = 1;
        return;
    }
    if (state->running && !gRoomCaptureDone) return;
    /* DemoKankyo_Init deliberately clears curRoom.segment in CUTSCENE_MAP.
     * The completed DMA buffer and initialized actors still belong to the room. */
    int hiddenGeometry = !play->roomCtx.curRoom.segment && play->sceneId == SCENE_CUTSCENE_MAP &&
                         play->roomCtx.roomRequestAddr;
    int correct = state->running && sFrames >= PS2_ROOM_SMOKE_FRAMES && play->sceneId == sRoomCases[sCase].scene &&
                  play->roomCtx.curRoom.num == sRoomCases[sCase].room && (play->roomCtx.curRoom.segment || hiddenGeometry) &&
                  !play->roomCtx.status && gRoomCaptureDone == 1;
    if (hiddenGeometry) osSyncPrintf("[PS2 ROOM GEOMETRY] case=%u hidden=1 intentional=1\n", sCase);
    sFailures += !correct;
    if (!correct) osSyncPrintf("[PS2 ROOM CONDITIONS] case=%u running=%u segment=%p status=%u\n",
        sCase, state->running, play->roomCtx.curRoom.segment, play->roomCtx.status);
    osSyncPrintf("[PS2 ROOM SWEEP] %s case=%u scene=%d room=%d frames=%u capture=%u\n",
        correct ? "PASS" : "FAIL", sCase, play->sceneId, play->roomCtx.curRoom.num, sFrames, gRoomCaptureDone);
    state->running = false;
    if (sCase + 1 == ARRAY_COUNT(sRoomCases)) {
        state->init = NULL; state->size = 0;
        osSyncPrintf("[PS2 ROOM SWEEP RESULT] %s cases=%u failures=%u\n",
            sFailures ? "FAIL" : "PASS", (unsigned)ARRAY_COUNT(sRoomCases), sFailures);
    } else { SET_NEXT_GAMESTATE(&play->state, Play_Init, PlayState); sAdvance = 1; }
}
void __wrap_GameState_Destroy(GameState* state) {
    __real_GameState_Destroy(state);
    if (sAdvance) { sAdvance = 0; sCase++; prepare_case(state); }
}
