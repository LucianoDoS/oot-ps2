/* Optional diagnostic ELF only. No movement/collision behavior is replaced. */
#include "play_state.h"
#include "player.h"
#include "overlays/actors/ovl_En_St/z_en_st.h"

extern void __real_Actor_UpdateAll(PlayState*, ActorContext*);
extern void EnSt_WaitOnCeiling(EnSt*, PlayState*);
extern void EnSt_MoveToGround(EnSt*, PlayState*);
extern void EnSt_LandOnGround(EnSt*, PlayState*);
extern void EnSt_WaitOnGround(EnSt*, PlayState*);
extern void EnSt_ReturnToCeiling(EnSt*, PlayState*);

static const char* state_name(EnSt* st) {
    if (st->actionFunc == EnSt_WaitOnCeiling) return "ceiling";
    if (st->actionFunc == EnSt_MoveToGround) return "drop";
    if (st->actionFunc == EnSt_LandOnGround) return "land";
    if (st->actionFunc == EnSt_WaitOnGround) return "ground";
    if (st->actionFunc == EnSt_ReturnToCeiling) return "return";
    return "other";
}

void __wrap_Actor_UpdateAll(PlayState* play, ActorContext* ctx) {
    __real_Actor_UpdateAll(play, ctx);
    if (play->sceneId != SCENE_DEKU_TREE || (play->gameplayFrames & 15) != 0) return;
    Player* player = GET_PLAYER(play);
    for (Actor* actor = ctx->actorLists[ACTORCAT_ENEMY].head; actor; actor = actor->next) {
        if (actor->id != ACTOR_EN_ST || actor->init || !actor->update || actor->xzDistToPlayer > 500) continue;
        EnSt* st = (EnSt*)actor;
        osSyncPrintf("[PS2 SKULLTULA LIVE] room=%d frame=%u action=%s home=(%.1f,%.1f,%.1f) pos=(%.1f,%.1f,%.1f) floor=%.3f bg=%d vy=%.3f spin=%d sway=%d stun=%d hp=%u player=(%.1f,%.1f,%.1f) pFloor=%.3f mass=%u flags=%08x pcyl=(%d,%d,%d) stcyl=(%d,%d,%d) oc=%02x,%02x,%02x\n",
            play->roomCtx.curRoom.num, play->gameplayFrames, state_name(st),
            actor->home.pos.x, actor->home.pos.y, actor->home.pos.z,
            actor->world.pos.x, actor->world.pos.y, actor->world.pos.z,
            actor->floorHeight, actor->floorBgId, actor->velocity.y,
            st->gaveDamageSpinTimer, st->swayTimer, st->stunTimer, actor->colChkInfo.health,
            player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z,
            player->actor.floorHeight, player->actor.colChkInfo.mass, player->stateFlags1,
            player->cylinder.dim.pos.y, player->cylinder.dim.height, player->cylinder.dim.yShift,
            st->colliderCylinders[4].dim.pos.y, st->colliderCylinders[4].dim.height,
            st->colliderCylinders[4].dim.yShift,
            st->colliderCylinders[3].base.ocFlags2,
            st->colliderCylinders[4].base.ocFlags2,
            st->colliderCylinders[5].base.ocFlags2);
    }
}
