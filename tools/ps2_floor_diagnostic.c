/* Optional hardware comparison. Preserve real pad data and state callbacks. */
#include "play_state.h"
#include "controller.h"
#include "ps2_runtime_trace.h"
/* The game uses ultra64 types; avoid importing the renderer's SDK types here. */
extern void ps2_floor_request_mode(unsigned mode);
extern void __real_GameState_ReqPadData(GameState* state);

void __wrap_GameState_ReqPadData(GameState* state) {
    static unsigned announced, mode;
    __real_GameState_ReqPadData(state);
    if (!announced) {
        ps2_runtime_event("FLOOR_LAB v2 up=read-only-overlay left=base-only right=legacy");
        announced = 1;
    }
    if (state->main != Play_Main) return;
    PlayState* play = (PlayState*)state;
    if (play->pauseCtx.state || play->msgCtx.msgMode || play->transitionTrigger || play->transitionMode) return;
    unsigned pressed = state->input[0].press.button;
    unsigned next = mode;
    if (pressed & BTN_DUP) next = 0;
    else if (pressed & BTN_DLEFT) next = 1;
    else if (pressed & BTN_DRIGHT) next = 2;
    else return;
    mode = next;
    ps2_floor_request_mode(mode);
    ps2_runtime_event("FLOOR_MODE mode=%u scene=%d frame=%u", mode, play->sceneId, state->frames);
}
