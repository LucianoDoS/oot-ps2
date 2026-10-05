/* Link wrappers for the optional logger: no synthetic save or input. */
#include "play_state.h"
#include "gfx.h"
#include "room.h"
#include "ps2_runtime_trace.h"
extern void __real_GameState_Init(GameState*, GameStateFunc, GraphicsContext*);
extern void __real_GameState_Update(GameState*);
extern void __real_GameState_Destroy(GameState*);
extern void PS2Gfx_DumpTrace(void);
extern void ps2_runtime_thread_snapshot(void);
extern void ps2_runtime_gfx_snapshot(void);
extern s32 __real_Room_RequestNewRoom(PlayState*, RoomContext*, s32);
extern s32 __real_Room_ProcessRoomRequest(PlayState*, RoomContext*);
extern u32 __real_Room_SetupFirstRoom(PlayState*, RoomContext*);
static OSTime sRoomStart;
/* The logger has higher priority than Graph. Clear the pointer before Graph
 * can free the state; never dereference a completed/destroyed state. */
static GameState* sState;
static unsigned sPhase, sPlayReady;
static int sLastTrigger, sLastMode;

void ps2_runtime_stall_snapshot(void) {
    GameState* state = sState;
    ps2_runtime_event("GAME_PROGRESS phase=%u state=%p main=%p frames=%u running=%u gfxQueue=%d",
        sPhase, state, state ? state->main : NULL, state ? state->frames : 0,
        state ? state->running : 0, state && state->gfxCtx ? state->gfxCtx->queue.validCount : -1);
    if (state && sPlayReady) {
        PlayState* play = (PlayState*)state;
        ps2_runtime_event("PLAY_PROGRESS scene=%d room=%d roomStatus=%u transitionMode=%d trigger=%d type=%u nextEntrance=%04x pause=%u",
            play->sceneId, play->roomCtx.curRoom.num, play->roomCtx.status,
            play->transitionMode, play->transitionTrigger, play->transitionType,
            play->nextEntranceIndex, play->pauseCtx.state);
    }
    PS2Gfx_DumpTrace();
    ps2_runtime_gfx_snapshot();
    ps2_runtime_thread_snapshot();
}

void __wrap_GameState_Update(GameState* state) {
    sState = state; sPhase = 2;
    if (state->main != Play_Main) sPlayReady = 0;
    if (sPlayReady) {
        PlayState* play = (PlayState*)state;
        if (play->transitionTrigger != sLastTrigger || play->transitionMode != sLastMode) {
            ps2_runtime_event("TRANSITION scene=%d mode=%d trigger=%d type=%u nextEntrance=%04x frame=%u",
                play->sceneId, play->transitionMode, play->transitionTrigger, play->transitionType,
                play->nextEntranceIndex, state->frames);
            sLastTrigger = play->transitionTrigger; sLastMode = play->transitionMode;
        }
    }
    __real_GameState_Update(state);
    sPhase = 3;
}

void __wrap_GameState_Destroy(GameState* state) {
    sState = state; sPhase = 4;
    ps2_runtime_event("STATE_DESTROY_BEGIN main=%p frames=%u", state->main, state->frames);
    __real_GameState_Destroy(state);
    ps2_runtime_event("STATE_DESTROY_READY");
    sState = NULL; sPlayReady = 0; sPhase = 5;
}

/* Wrap the dispatcher, not Play_Init: Play_Update refers to its own
 * Play_Init directly, while Graph's external comparison would be rewritten
 * to __wrap_Play_Init. That mismatch makes Graph end instead of reloading
 * Play after any scene exit. Preserve the original callback addresses. */
void __wrap_GameState_Init(GameState* state, GameStateFunc init, GraphicsContext* gfx) {
    sState = NULL; sPhase = 1; sPlayReady = 0;
    OSTime begin = osGetTime();
    int isPlay = init == Play_Init;
    ps2_runtime_event("STATE_INIT_BEGIN init=%p", init);
    if (isPlay) ps2_runtime_event("PLAY_INIT_BEGIN");
    __real_GameState_Init(state, init, gfx);
    PlayState* play = (PlayState*)state;
    sState = state; sPlayReady = isPlay && state->main == Play_Main; sPhase = 3;
    sLastTrigger = -999; sLastMode = -999;
    if (sPlayReady)
        ps2_runtime_event("PLAY_INIT_READY scene=%d room=%d durationMs=%u", play->sceneId,
            play->roomCtx.curRoom.num, (unsigned)((osGetTime() - begin) * 1000 / 46875000ull));
    ps2_runtime_event("STATE_INIT_READY init=%p", init);
}
u32 __wrap_Room_SetupFirstRoom(PlayState* play, RoomContext* room) {
    sRoomStart = osGetTime();
    ps2_runtime_event("ROOM_SETUP_BEGIN scene=%d", play->sceneId);
    u32 bytes = __real_Room_SetupFirstRoom(play, room);
    ps2_runtime_event("ROOM_SETUP_REQUESTED scene=%d room=%d heapBytes=%u", play->sceneId, room->curRoom.num, bytes);
    return bytes;
}
s32 __wrap_Room_RequestNewRoom(PlayState* play, RoomContext* room, s32 number) {
    OSTime begin = osGetTime();
    s32 accepted = __real_Room_RequestNewRoom(play, room, number);
    if (accepted) {
        sRoomStart = begin;
        ps2_runtime_event("ROOM_REQUEST scene=%d room=%d", play->sceneId, number);
    }
    return accepted;
}
s32 __wrap_Room_ProcessRoomRequest(PlayState* play, RoomContext* room) {
    s32 loading = room->status == 1;
    s32 result = __real_Room_ProcessRoomRequest(play, room);
    if (loading && room->status == 0)
        ps2_runtime_event("ROOM_READY scene=%d room=%d durationMs=%u", play->sceneId,
            room->curRoom.num, (unsigned)((osGetTime() - sRoomStart) * 1000 / 46875000ull));
    return result;
}
