/* Test-only ELF: real EnSt, original Deku Tree collision, poses and player OC. */
#include "console_logo_state.h"
#include "game.h"
#include "map_select_state.h"
#include "play_state.h"
#include "player.h"
#include "save.h"
#include "sram.h"
#include "regs.h"
#include "segmented_address.h"
#include "libc/string.h"
#include "overlays/actors/ovl_En_St/z_en_st.h"
#include "skulltula_pose_cases.h"

extern void MapSelect_LoadGame(MapSelectState*, s32);
extern void __real_GameState_Update(GameState*);
extern void EnSt_WaitOnCeiling(EnSt*, PlayState*);
extern void EnSt_MoveToGround(EnSt*, PlayState*);
extern void EnSt_LandOnGround(EnSt*, PlayState*);
extern void EnSt_WaitOnGround(EnSt*, PlayState*);
extern void EnSt_ReturnToCeiling(EnSt*, PlayState*);
extern void SkelAnime_GetFrameData(AnimationHeader*, s32, s32, Vec3s*);

static unsigned sBootFrames, sFailures;
static const struct {
    Vec3f pos;
    s16 yaw, params;
} sCases[] = {
    {{-1082, 993, 268}, -0x8000, 0}, /* Room 10: chest beside platforms. */
    {{-67, 1069, 255}, -0x0aab, 1},
    {{256, 1071, -62}, 0x4aab, 1},
    {{-179, 1072, -190}, -0x6000, 1},
    {{-1360, -806, 1079}, 0x4000, 0},
    {{-1854, -642, 488}, -0x8000, 0},
    {{-2684, -735, -712}, 0x2000, 0},
};

static const char* action_name(EnSt* st) {
    if (st->actionFunc == EnSt_WaitOnCeiling) return "ceiling";
    if (st->actionFunc == EnSt_MoveToGround) return "drop";
    if (st->actionFunc == EnSt_LandOnGround) return "land";
    if (st->actionFunc == EnSt_WaitOnGround) return "ground";
    if (st->actionFunc == EnSt_ReturnToCeiling) return "return";
    return "initial";
}

static void step_actor(PlayState* play, EnSt* st, Vec3f playerPos) {
    Player* player = GET_PLAYER(play);
    CollisionCheck_ClearContext(play, &play->colChkCtx);
    player->actor.world.pos = playerPos;
    /* These controlled cases place Player standing on the specified support. */
    player->actor.floorHeight = playerPos.y;
    player->actor.bgCheckFlags |= BGCHECKFLAG_GROUND;
    st->actor.prevPos = st->actor.world.pos;
    st->actor.xzDistToPlayer = Actor_WorldDistXZToActor(&st->actor, &player->actor);
    st->actor.yDistToPlayer = Actor_HeightDiff(&st->actor, &player->actor);
    st->actor.yawTowardsPlayer = Actor_WorldYawTowardActor(&st->actor, &player->actor);
    Actor_SetObjectDependency(play, &st->actor);
    st->actor.update(&st->actor, play);
    CollisionCheck_ResetDamage(&st->actor.colChkInfo);
    play->state.frames++;
}

static void check_case(PlayState* play, unsigned index) {
    EnSt* st = (EnSt*)Actor_Spawn(&play->actorCtx, play, ACTOR_EN_ST,
        sCases[index].pos.x, sCases[index].pos.y, sCases[index].pos.z,
        0, sCases[index].yaw, 0, sCases[index].params);
    Player* player = GET_PLAYER(play);
    if (st == NULL || st->actor.init != NULL) {
        osSyncPrintf("[PS2 SKULLTULA] FAIL case=%u spawn/object\n", index);
        sFailures++;
        return;
    }
    Vec3f far = sCases[index].pos;
    far.x += 300;
    /* Settle the actual floor query before positioning the test player. */
    for (unsigned i = 0; i < 8; i++) step_actor(play, st, far);
    Vec3f near = sCases[index].pos;
    near.z += 75;
    near.y = st->actor.floorHeight;
    unsigned drops = 0, lands = 0, grounds = 0;
    EnStActionFunc previous = NULL;
    for (unsigned i = 0; i < 160; i++) {
        step_actor(play, st, near);
        drops += st->actionFunc == EnSt_MoveToGround;
        lands += st->actionFunc == EnSt_LandOnGround;
        grounds += st->actionFunc == EnSt_WaitOnGround;
        if (previous != st->actionFunc || i == 159) {
            osSyncPrintf("[PS2 SKULLTULA] case=%u frame=%u action=%s y=%.3f floor=%.3f vy=%.3f anim=%.3f/%.3f oc=%d\n",
                index, i, action_name(st), st->actor.world.pos.y, st->actor.floorHeight,
                st->actor.velocity.y, st->skelAnime.curFrame, st->skelAnime.animLength,
                play->colChkCtx.colOCCount);
            previous = st->actionFunc;
        }
    }
    int grounded = grounds != 0 && lands != 0 && drops != 0;
    sFailures += !grounded;

    /* Use Player's real collider, initialized by Play, against EnSt's three
     * OC cylinders. This also tests registration, mass and collision flags. */
    near = st->actor.world.pos;
    near.y = st->actor.floorHeight;
    near.z += 10;
    step_actor(play, st, near);
    CollisionCheck_ResetDamage(&player->actor.colChkInfo);
    /* The entrance cutscene makes Player immovable; normal gameplay uses 50. */
    player->actor.colChkInfo.mass = 50;
    Collider_UpdateCylinder(&player->actor, &player->cylinder);
    CollisionCheck_SetOC(play, &play->colChkCtx, &player->cylinder.base);
    CollisionCheck_OC(play, &play->colChkCtx);
    unsigned hit = 0;
    for (unsigned c = 3; c < 6; c++) hit |= st->colliderCylinders[c].base.ocFlags2 & OC2_HIT_PLAYER;
    int blocks = hit && (player->actor.colChkInfo.displacement.x != 0 ||
                        player->actor.colChkInfo.displacement.z != 0);
    sFailures += !blocks;
    osSyncPrintf("[PS2 SKULLTULA] case=%u grounded=%d blocks=%d hit=%u push=(%.2f,%.2f) playerCyl=(r=%d h=%d shift=%d) stCyl=(y=%d r=%d h=%d shift=%d) mass=%u\n",
        index, grounded, blocks, hit, player->actor.colChkInfo.displacement.x,
        player->actor.colChkInfo.displacement.z, player->cylinder.dim.radius,
        player->cylinder.dim.height, player->cylinder.dim.yShift,
        st->colliderCylinders[4].dim.pos.y, st->colliderCylinders[4].dim.radius,
        st->colliderCylinders[4].dim.height, st->colliderCylinders[4].dim.yShift,
        st->actor.colChkInfo.mass);
    /* Consume the real contact response, then allow the retreat to finish. */
    far.y = near.y;
    for (unsigned i = 0; i < 200; i++) step_actor(play, st, far);
    int returned = st->actionFunc == EnSt_WaitOnCeiling &&
                   st->actor.world.pos.y >= st->actor.home.pos.y - 10;
    sFailures += !returned;
    osSyncPrintf("[PS2 SKULLTULA] case=%u returned=%d action=%s y=%.3f home=%.3f vy=%.3f spin=%d sway=%d stun=%d\n",
        index, returned, action_name(st), st->actor.world.pos.y, st->actor.home.pos.y,
        st->actor.velocity.y, st->gaveDamageSpinTimer, st->swayTimer, st->stunTimer);
    Actor_Delete(&play->actorCtx, &st->actor, play);
}

static void check_room10_ledge(PlayState* play, f32 playerFloor) {
    EnSt* st = (EnSt*)Actor_Spawn(&play->actorCtx, play, ACTOR_EN_ST,
        -1082, 993, 268, 0, -0x8000, 0, 0);
    if (st == NULL || st->actor.init != NULL) { sFailures++; return; }
    Vec3f near = {-1082, 820, 345};
    near.y = playerFloor;
    Player* player = GET_PLAYER(play);
    player->actor.floorHeight = playerFloor;
    player->actor.bgCheckFlags |= BGCHECKFLAG_GROUND;
    Actor_SetObjectDependency(play, &st->actor);
    unsigned poseFailures = 0, poseChannels = 0;
    Vec3s pose[30];
    for (unsigned c = 0; c < sizeof(sPoseCases) / sizeof(sPoseCases[0]); c++) {
        AnimationHeader* header = SEGMENTED_TO_VIRTUAL((void*)sPoseCases[c].address);
        poseFailures += header->common.frameCount != sPoseCases[c].frames;
        for (s32 f = 0; f < sPoseCases[c].frames; f++) {
            SkelAnime_GetFrameData((AnimationHeader*)sPoseCases[c].address, f, 30, pose);
            for (unsigned j = 0; j < 30; j++) {
                poseFailures += pose[j].x != sPoseCases[c].pose[f][j].x;
                poseFailures += pose[j].y != sPoseCases[c].pose[f][j].y;
                poseFailures += pose[j].z != sPoseCases[c].pose[f][j].z;
                poseChannels += 3;
            }
        }
    }
    sFailures += poseFailures;
    osSyncPrintf("[PS2 SKULLTULA POSE] originalReferenceChannels=%u failures=%u\n", poseChannels, poseFailures);
    EnStActionFunc previous = NULL;
    unsigned grounds = 0, turns = 0, ceilings = 0;
    f32 lowest = 993;
    for (unsigned i = 0; i < 240; i++) {
        step_actor(play, st, near);
        grounds += st->actionFunc == EnSt_WaitOnGround;
        ceilings += st->actionFunc == EnSt_WaitOnCeiling;
        if (st->actor.world.pos.y < lowest) lowest = st->actor.world.pos.y;
        if (previous != st->actionFunc) {
            osSyncPrintf("[PS2 SKULLTULA LEDGE] frame=%u action=%s y=%.3f floor=%.3f vy=%.3f playerY=%.3f\n",
                i, action_name(st), st->actor.world.pos.y, st->actor.floorHeight,
                st->actor.velocity.y, near.y);
            turns++;
            previous = st->actionFunc;
        }
    }
    sFailures += lowest < playerFloor + st->floorHeightOffset || ceilings == 0;
    osSyncPrintf("[PS2 SKULLTULA LEDGE] grounds=%u changes=%u ceilings=%u lowest=%.3f min=%.3f\n",
        grounds, turns, ceilings, lowest, playerFloor + st->floorHeightOffset);
    /* Test frontal OC contact at the upper ledge's height separately from the
     * observation above. Walking past its side is allowed by original EnSt. */
    near.z = 278;
    player->actor.colChkInfo.mass = 50;
    unsigned hits = 0, response = 0;
    for (unsigned i = 0; i < 160 && !hits; i++) {
        step_actor(play, st, near);
        CollisionCheck_ResetDamage(&player->actor.colChkInfo);
        Collider_UpdateCylinder(&player->actor, &player->cylinder);
        CollisionCheck_SetOC(play, &play->colChkCtx, &player->cylinder.base);
        CollisionCheck_OC(play, &play->colChkCtx);
        for (unsigned c = 3; c < 6; c++) hits |= st->colliderCylinders[c].base.ocFlags2 & OC2_HIT_PLAYER;
        if (hits) {
            int pushed = player->actor.colChkInfo.displacement.x != 0 || player->actor.colChkInfo.displacement.z != 0;
            step_actor(play, st, near);
            response = pushed && st->gaveDamageSpinTimer != 0;
        }
    }
    sFailures += !hits || !response;
    osSyncPrintf("[PS2 SKULLTULA LEDGE] frontalHit=%u pushAndResponse=%u spin=%d\n",
        hits, response, st->gaveDamageSpinTimer);
    Actor_Delete(&play->actorCtx, &st->actor, play);
}

static void empty_state(GameState* state) { (void)state; }

void __wrap_ConsoleLogo_Init(GameState* state) {
    state->main = empty_state;
    state->destroy = empty_state;
    SaveContext_Init();
    gSaveContext.fileNum = 0xff;
    gSaveContext.save.linkAge = LINK_AGE_CHILD;
    gSaveContext.save.dayTime = 0x8000;
    MapSelect_LoadGame((MapSelectState*)state, 0);
}

void __wrap_GameState_ReqPadData(GameState* state) {
    memset(state->input, 0, sizeof(state->input));
}

void __wrap_GameState_Update(GameState* state) {
    int isPlay = state->main == Play_Main;
    __real_GameState_Update(state);
    if (!isPlay || ++sBootFrames < 60) return;
    PlayState* play = (PlayState*)state;
    osSyncPrintf("[PS2 SKULLTULA] BEGIN scene=%d rate=%d original floor/OC, real AI; private debug save\n",
        play->sceneId, R_UPDATE_RATE);
    for (unsigned i = 0; i < sizeof(sCases) / sizeof(sCases[0]); i++) check_case(play, i);
    check_room10_ledge(play, 799);
    check_room10_ledge(play, 820);
    osSyncPrintf("[PS2 SKULLTULA RESULT] %s cases=7 failures=%u\n", sFailures ? "FAIL" : "PASS", sFailures);
    state->running = false;
    state->init = NULL;
    state->size = 0;
}
