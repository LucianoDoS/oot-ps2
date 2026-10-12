/* Real-game benchmarks. Synthetic save; never loads a player save. */
#include "console_logo_state.h"
#include "title_setup_state.h"
#include "game.h"
#include "gfx.h"
#include "map_select_state.h"
#include "play_state.h"
#include "player.h"
#include "save.h"
#include "sram.h"
#include "camera.h"
#include "bgcheck.h"
#include "rand.h"
#include "z_lib.h"
#include "libc/string.h"
#include "ps2/ps2_offscreen.h"
#include "ps2_perf_scene.h"

extern void MapSelect_LoadGame(MapSelectState*, s32);
extern void __real_GameState_Update(GameState*);
extern void __real_GameState_Destroy(GameState*);
extern u32 __real_Room_SetupFirstRoom(PlayState*, RoomContext*);
extern Vec3s __real_Camera_Update(Camera*);
extern void __real_Rand_Seed(u32);
extern void ps2_perf_scene_profile_init(void);
extern PS2RenderTarget gPerfCaptureTarget;
PS2PerfTaskTag gPerfTaskTags[2];
static unsigned sView, sFrames, sFailures;
static int sCaptureRequested;
#if !PS2_PERF_SCENE_TITLE_CINEMATIC
static int sAdvance, sAnchorSettled;
static ActorEntry sPlayerEntry;
static Vec3f sAnchor, sEye, sAt;
static Vec3f sUp = { 0.0f, 1.0f, 0.0f };
static s16 sBaseYaw;
#if PS2_PERF_SCENE_CASTLE_GATE
#define FIXED_SCENE SCENE_HYRULE_CASTLE
#define FIXED_ENTRANCE ENTR_HYRULE_CASTLE_4
#define FIXED_TIME PS2_PERF_SCENE_DAY_TIME
#else
#define FIXED_SCENE SCENE_KOKIRI_FOREST
#define FIXED_ENTRANCE ENTR_KOKIRI_FOREST_3
#define FIXED_TIME CLOCK_TIME(12, 0)
#endif

static void prepare_view(GameState* state) {
    SaveContext_Init();
    gSaveContext.fileNum = 0xff;
    gSaveContext.save.linkAge = LINK_AGE_CHILD;
    gSaveContext.save.dayTime = FIXED_TIME;
    Rand_Seed(PS2_PERF_SCENE_SEED);
    MapSelect_LoadGame((MapSelectState*)state, FIXED_ENTRANCE);
#if PS2_PERF_SCENE_CASTLE_GATE
    /* Debug inventory remains synthetic, but retain the child's pre-flight
     * castle population instead of the debug-save Zelda escape event. */
    CLEAR_EVENTCHKINF(EVENTCHKINF_ZELDA_FLED_CASTLE);
#endif
    sFrames = sCaptureRequested = sAnchorSettled = gPerfCaptureDone = gPerfViewComplete = 0;
    osSyncPrintf("[PS2 PERF SCENE BEGIN] view=%u scene=%u entrance=%04x warmup=%u frames=%u seed=12345678\n",
        sView, FIXED_SCENE, FIXED_ENTRANCE, PS2_PERF_SCENE_WARMUP, PS2_PERF_SCENE_FRAMES);
}
#endif

void __wrap_Rand_Seed(u32 requested) {
    /* Play_Init seeds again from osGetTime before actor initialization.
     * Intercept that external call too, so init poses and effects are fixed. */
    __real_Rand_Seed(PS2_PERF_SCENE_SEED);
    osSyncPrintf("[PS2 PERF SCENE RNG] view=%u requestedSeed=%u effectiveSeed=%u\n",
        sView, requested, PS2_PERF_SCENE_SEED);
}

u32 __wrap_Room_SetupFirstRoom(PlayState* play, RoomContext* roomCtx) {
#if !PS2_PERF_SCENE_TITLE_CINEMATIC
    if (play->sceneId != FIXED_SCENE || !play->playerEntry) {
        sFailures++;
        osSyncPrintf("[PS2 PERF SCENE] FAIL wrong scene or missing spawn\n");
        return __real_Room_SetupFirstRoom(play, roomCtx);
    }
    sPlayerEntry = *play->playerEntry;
    sBaseYaw = sPlayerEntry.rot.y;
#if PS2_PERF_SCENE_CASTLE_GATE
    /* Entrance 4 is the original spawn just outside the toll gate. Its source
     * coordinates are (913,1109,2937), yaw 0x4000, room 0. Do not relocate it
     * across a guard or an exit; raycast below will settle the original floor. */
    sAnchor = (Vec3f){ sPlayerEntry.pos.x, sPlayerEntry.pos.y, sPlayerEntry.pos.z };
    if (sView == 0) {
        sEye = (Vec3f){ sAnchor.x - 220.0f, sAnchor.y + 100.0f, sAnchor.z };
        sAt = (Vec3f){ sAnchor.x + 160.0f, sAnchor.y + 60.0f, sAnchor.z };
    } else if (sView == 1) {
        sEye = (Vec3f){ sAnchor.x, sAnchor.y + 100.0f, sAnchor.z + 220.0f };
        sAt = (Vec3f){ sAnchor.x, sAnchor.y + 175.0f, sAnchor.z - 900.0f };
    } else {
        /* The retail sun trajectory; aim below it so its disc and foreground
         * can share the frame. Captures must prove visibility/occlusion. */
        f32 sx = -Math_SinS(FIXED_TIME - CLOCK_TIME(12, 0)) * 3000.0f;
        f32 sy = Math_CosS(FIXED_TIME - CLOCK_TIME(12, 0)) * 3000.0f;
        f32 sz = Math_CosS(FIXED_TIME - CLOCK_TIME(12, 0)) * 500.0f;
        sEye = (Vec3f){ sAnchor.x - sx * (220.0f / 3000.0f), sAnchor.y + 100.0f,
                       sAnchor.z - sz * (220.0f / 3000.0f) };
        sAt = (Vec3f){ sEye.x + sx * 0.1f, sEye.y + sy * 0.1f - 100.0f,
                      sEye.z + sz * 0.1f };
    }
#else
    /* Move forward from the house entrance onto the village path. The anchor
     * and all camera coordinates are recorded for visual fixture review. */
    sPlayerEntry.pos.x += (s16)(Math_SinS(sBaseYaw) * 350.0f);
    sPlayerEntry.pos.z += (s16)(Math_CosS(sBaseYaw) * 350.0f);
    sAnchor = (Vec3f){ sPlayerEntry.pos.x, sPlayerEntry.pos.y, sPlayerEntry.pos.z };
    s16 yaw = sBaseYaw + (sView == 1 ? 0x4000 : sView == 2 ? -0x4000 : 0);
    sEye = (Vec3f){ sAnchor.x - Math_SinS(yaw) * 220.0f, sAnchor.y + 100.0f,
                   sAnchor.z - Math_CosS(yaw) * 220.0f };
    sAt = (Vec3f){ sAnchor.x, sAnchor.y + 45.0f, sAnchor.z };
#endif
    play->playerEntry = &sPlayerEntry;
#endif
    return __real_Room_SetupFirstRoom(play, roomCtx);
}

Vec3s __wrap_Camera_Update(Camera* camera) {
    Vec3s result = __real_Camera_Update(camera);
#if !PS2_PERF_SCENE_TITLE_CINEMATIC
    if (camera->play->sceneId == FIXED_SCENE && camera->status == CAM_STAT_ACTIVE) {
        /* Actor culling reads camera fields as well as the view matrix. */
        camera->eye = camera->eyeNext = sEye;
        camera->at = sAt;
        camera->up = sUp;
        camera->fov = camera->play->view.fovy = 60.0f;
        camera->quakeOffset = (Vec3f){ 0, 0, 0 };
        View_SetScale(&camera->play->view, 1.0f);
        View_LookAt(&camera->play->view, &sEye, &sAt, &sUp);
    }
#endif
    return result;
}

static void empty_state(GameState* state) { (void)state; }
void __wrap_ConsoleLogo_Init(GameState* gameState) {
    gameState->main = gameState->destroy = empty_state;
    ps2_perf_scene_profile_init();
#if PS2_PERF_SCENE_TITLE_CINEMATIC
    /* Follow the retail title initializer, including its debug save, age,
     * entrance and cutscene layer. It does not open a player save. */
    SaveContext_Init();
    gSaveContext.fileNum = 0xff;
    Rand_Seed(PS2_PERF_SCENE_SEED);
    sFrames = sCaptureRequested = gPerfCaptureDone = gPerfViewComplete = 0;
    osSyncPrintf("[PS2 PERF SCENE CONFIG] views=1 mode=title_cinematic cards=disabled input=disabled hostROM=1 syntheticSave=1 profileEvery=60 fixtureFrameAligned=1 seedOverride=1 camera=originalMoving\n");
    osSyncPrintf("[PS2 PERF SCENE BEGIN] view=0 scene=%u entrance=%04x warmup=%u frames=%u seed=12345678\n",
        SCENE_HYRULE_FIELD, ENTR_HYRULE_FIELD_0, PS2_PERF_SCENE_WARMUP, PS2_PERF_SCENE_FRAMES);
    gameState->running = false;
    SET_NEXT_GAMESTATE(gameState, TitleSetup_Init, TitleSetupState);
#else
#if PS2_PERF_SCENE_CASTLE_GATE
    osSyncPrintf("[PS2 PERF SCENE CONFIG] views=3 mode=castle_gate cards=disabled input=disabled hostROM=1 syntheticSave=1 profileEvery=60 fixtureFrameAligned=1 seedOverride=1 dayTime=%u camera=gateHillSun\n", (unsigned)FIXED_TIME);
#else
    osSyncPrintf("[PS2 PERF SCENE CONFIG] views=3 cards=disabled input=disabled hostROM=1 syntheticSave=1 profileEvery=60 fixtureFrameAligned=1 seedOverride=1\n");
#endif
    prepare_view(gameState);
#endif
}
void __wrap_GameState_ReqPadData(GameState* state) { memset(state->input, 0, sizeof(state->input)); }

void __wrap_GameState_Update(GameState* state) {
    int isPlay = state->main == Play_Main;
#if !PS2_PERF_SCENE_TITLE_CINEMATIC
    if (isPlay) {
        Player* player = GET_PLAYER((PlayState*)state);
        if (player) {
            if (!sAnchorSettled) {
                CollisionPoly* floorPoly;
                s32 bgId;
                Vec3f ray = { sAnchor.x, sAnchor.y + 100.0f, sAnchor.z };
                f32 floor = BgCheck_EntityRaycastDown5((PlayState*)state, &((PlayState*)state)->colCtx,
                    &floorPoly, &bgId, &player->actor, &ray);
                if (floor == BGCHECK_Y_MIN) {
                    sFailures++;
                    osSyncPrintf("[PS2 PERF SCENE] FAIL fixture anchor has no floor view=%u\n", sView);
                } else {
                    f32 delta = floor - sAnchor.y;
                    sAnchor.y = floor;
                    sEye.y += delta;
                    sAt.y += delta;
                }
                sAnchorSettled = 1;
                /* Thousandths preserve the sub-unit camera offsets in logs. */
                osSyncPrintf("[PS2 PERF SCENE CAMERA] view=%u playerMilli=%d,%d,%d yaw=%d eyeMilli=%d,%d,%d atMilli=%d,%d,%d fov=60 time=%u settled=1\n",
                    sView, (int)(sAnchor.x * 1000), (int)(sAnchor.y * 1000), (int)(sAnchor.z * 1000), sBaseYaw,
                    (int)(sEye.x * 1000), (int)(sEye.y * 1000), (int)(sEye.z * 1000),
                    (int)(sAt.x * 1000), (int)(sAt.y * 1000), (int)(sAt.z * 1000), (unsigned)FIXED_TIME);
            }
            player->actor.freezeTimer = 3;
            player->actor.world.pos = sAnchor;
            player->actor.prevPos = sAnchor;
            player->actor.speed = 0;
            player->actor.velocity = (Vec3f){ 0, 0, 0 };
        }
        gSaveContext.save.dayTime = gSaveContext.skyboxTime = FIXED_TIME;
    }
#endif
    __real_GameState_Update(state);
    if (!isPlay) return;
    PlayState* play = (PlayState*)state;
    sFrames++;
    int capture = !sCaptureRequested && sFrames == PS2_PERF_SCENE_WARMUP + PS2_PERF_SCENE_FRAMES + 1;
    if (capture) {
        Gfx* command = state->gfxCtx->overlay.p++;
        command->words.w0 = PS2_GFX_CAPTURE_FRAME;
        command->words.w1 = (u32)&gPerfCaptureTarget;
        sCaptureRequested = 1;
    }
    unsigned slot = state->gfxCtx->gfxPoolIdx & 1;
    gPerfTaskTags[slot] = (PS2PerfTaskTag){ .root=state->gfxCtx->workBuffer,
        .view=sView, .frame=sFrames, .capture=capture };
#if PS2_PERF_SCENE_CASTLE_GATE
    /* Post-draw environment values belong to this graphics task. Projection is
     * recorded rather than asserted visible: walls may legitimately hide sun. */
    Vec3f sun = { play->view.eye.x + play->envCtx.sunPos.x,
                 play->view.eye.y + play->envCtx.sunPos.y,
                 play->view.eye.z + play->envCtx.sunPos.z }, screen;
    Play_GetScreenPos(play, &sun, &screen);
    gPerfTaskTags[slot].environment = (PS2PerfEnvironmentTag){
        .scene=play->sceneId, .room=play->roomCtx.curRoom.num,
        .dayTime=gSaveContext.save.dayTime, .sunDisabled=play->envCtx.sunMoonDisabled,
        .skyboxDisabled=play->envCtx.skyboxDisabled, .skyboxConfig=play->envCtx.skyboxConfig,
        .cutsceneState=play->csCtx.state,
        .sunX=(int)(play->envCtx.sunPos.x*1000), .sunY=(int)(play->envCtx.sunPos.y*1000),
        .sunZ=(int)(play->envCtx.sunPos.z*1000), .screenX=(int)(screen.x*1000),
        .screenY=(int)(screen.y*1000), .glareMilli=(int)(play->envCtx.glareAlpha*1000),
        .flareMilli=(int)(play->envCtx.lensFlareAlphaScale*1000) };
#endif
#if PS2_PERF_SCENE_TITLE_CINEMATIC
    /* Buffered with the matching display-list task, then emitted after the
     * timing window. Do not alter the original camera, actors, or clock. */
    Camera* camera = GET_ACTIVE_CAM(play);
    Player* player = GET_PLAYER(play);
    Actor* horse = player ? player->rideActor : NULL;
    PS2PerfMotionTag* motion = &gPerfTaskTags[slot].motion;
    *motion = (PS2PerfMotionTag){ .scene=play->sceneId, .room=play->roomCtx.curRoom.num,
        .gameMode=gSaveContext.gameMode, .linkAge=gSaveContext.save.linkAge,
        .cutsceneState=play->csCtx.state, .cutsceneFrame=play->csCtx.curFrame,
        .riding=horse && horse->id == ACTOR_EN_HORSE,
        .horseDrawable=horse && horse->id == ACTOR_EN_HORSE && horse->draw != NULL,
        .eyeX=(int)(camera->eye.x*1000), .eyeY=(int)(camera->eye.y*1000), .eyeZ=(int)(camera->eye.z*1000),
        .atX=(int)(camera->at.x*1000), .atY=(int)(camera->at.y*1000), .atZ=(int)(camera->at.z*1000),
        .fovMilli=(int)(camera->fov*1000),
        .playerX=player ? (int)(player->actor.world.pos.x*1000) : 0,
        .playerY=player ? (int)(player->actor.world.pos.y*1000) : 0,
        .playerZ=player ? (int)(player->actor.world.pos.z*1000) : 0 };
#endif
    if (!gPerfViewComplete) return;
    int correct = state->running && play->sceneId ==
#if PS2_PERF_SCENE_TITLE_CINEMATIC
                  SCENE_HYRULE_FIELD && gSaveContext.gameMode == GAMEMODE_TITLE_SCREEN &&
#else
                  FIXED_SCENE &&
#endif
                  !play->roomCtx.status &&
                  play->roomCtx.curRoom.segment && gPerfCaptureDone == 1;
    sFailures += !correct;
    osSyncPrintf("[PS2 PERF SCENE VIEW] %s view=%u scene=%d room=%d frames=%u capture=%u\n",
        correct ? "PASS" : "FAIL", sView, play->sceneId, play->roomCtx.curRoom.num, sFrames, gPerfCaptureDone);
    state->running = false;
    if (sView + 1 == PS2_PERF_SCENE_VIEWS) {
        state->init = NULL; state->size = 0;
        osSyncPrintf("[PS2 PERF SCENE RESULT] %s views=%u failures=%u\n", sFailures ? "FAIL" : "PASS", PS2_PERF_SCENE_VIEWS, sFailures);
    }
#if !PS2_PERF_SCENE_TITLE_CINEMATIC
    else { SET_NEXT_GAMESTATE(&play->state, Play_Init, PlayState); sAdvance = 1; }
#endif
}
void __wrap_GameState_Destroy(GameState* state) {
    __real_GameState_Destroy(state);
#if !PS2_PERF_SCENE_TITLE_CINEMATIC
    if (sAdvance) { sAdvance = 0; sView++; prepare_view(state); }
#endif
}
