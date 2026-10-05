/* Link wrappers for the optional logger: no synthetic save or input. */
#include "play_state.h"
#include "room.h"
#include "ps2_runtime_trace.h"
extern void __real_Play_Init(GameState*);
extern s32 __real_Room_RequestNewRoom(PlayState*, RoomContext*, s32);
extern s32 __real_Room_ProcessRoomRequest(PlayState*, RoomContext*);
extern u32 __real_Room_SetupFirstRoom(PlayState*, RoomContext*);
static OSTime sRoomStart;

void __wrap_Play_Init(GameState* state) {
    OSTime begin = osGetTime();
    ps2_runtime_event("PLAY_INIT_BEGIN");
    __real_Play_Init(state);
    PlayState* play = (PlayState*)state;
    ps2_runtime_event("PLAY_INIT_READY scene=%d room=%d durationMs=%u", play->sceneId,
        play->roomCtx.curRoom.num, (unsigned)((osGetTime() - begin) * 1000 / 46875000ull));
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
