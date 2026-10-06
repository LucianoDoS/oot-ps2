/* Exercise the real logo, options input, SRAM header and message decoder.
 * This ELF uses blank SRAM and disables controller input. */
#include "console_logo_state.h"
#include "controller.h"
#include "file_select_state.h"
#include "map_select_state.h"
#include "play_state.h"
#include "message.h"
#include "save.h"
#include "sram.h"
#include "ss_sram.h"
#include "transition.h"
#include "libc/string.h"
#include "ps2/ps2_locale.h"
#include "ps2/ps2_offscreen.h"
#include "../src/overlays/gamestates/ovl_file_choose/file_select.h"

extern void ConsoleLogo_Main(GameState*);
extern void FileSelect_Main(GameState*);
extern void ps2_runtime_test_update(GameState*);
extern void MapSelect_LoadGame(MapSelectState*,s32);
extern int memcmp(const void*,const void*,size_t); /* newlib, absent from N64 libc headers */
extern void ps2_locale_logo_report(void);
extern void ps2_runtime_trace_finish(void);
extern PS2RenderTarget gLocaleCaptureTarget;
extern volatile unsigned gLocaleCaptureDone, gLocaleTestPhase;
extern unsigned gLocaleCaptureCase;
static unsigned sStage,sFrames,sFailures,sPlayPhase;
static u8 sBefore[SRAM_SIZE],sAfter[SRAM_SIZE];
static const s16 sScenes[]={SCENE_LINKS_HOUSE,SCENE_KOKIRI_FOREST,SCENE_LINKS_HOUSE};
static const s16 sEntrances[]={ENTR_KOKIRI_FOREST_3,ENTR_LINKS_HOUSE_0};

void __wrap_GameState_ReqPadData(GameState* state) { memset(state->input,0,sizeof(state->input)); }

static void capture(GameState* state,unsigned index) {
    gLocaleCaptureDone=0; gLocaleCaptureCase=index;
    Gfx* command=state->gfxCtx->overlay.p++;
    command->words.w0=PS2_GFX_CAPTURE_FRAME;
    command->words.w1=(u32)&gLocaleCaptureTarget;
}

void __wrap_GameState_Update(GameState* state) {
    int logo=state->main==ConsoleLogo_Main;
    int menu=state->main==FileSelect_Main;
    if (menu) {
        FileSelectState* select=(FileSelectState*)state;
        if (sStage==0 && select->configMode==CM_MAIN_MENU) {
            sFailures+=!ps2_locale_available();
            SsSram_ReadWrite(0x08000000,sBefore,SRAM_SIZE,OS_READ);
            select->buttonIndex=FS_BTN_MAIN_OPTIONS;
            state->input[0].press.button=BTN_A;
            sStage=1; sFrames=0;
        } else if (sStage==1 && select->configMode==CM_OPTIONS_MENU) {
            sFrames++;
            if (sFrames==5 || sFrames==7) state->input[0].press.button=BTN_A;
            if (sFrames==9) state->input[0].rel.stick_x=60;
            if (sFrames==10) sFailures+=ps2_locale_language()!=PS2_LANGUAGE_SPANISH;
            if (sFrames>=30 && gLocaleCaptureDone) {
                state->input[0].press.button=BTN_B; sStage=2;
            }
        } else if (sStage==2 && select->configMode==CM_MAIN_MENU) {
            SsSram_ReadWrite(0x08000000,sAfter,SRAM_SIZE,OS_READ);
            sFailures+=sAfter[SRAM_HEADER_LANGUAGE]!=2;
            for (unsigned i=0;i<SRAM_SIZE;i++) if (i!=SRAM_HEADER_LANGUAGE) sFailures+=sBefore[i]!=sAfter[i];
            ps2_locale_set_language(PS2_LANGUAGE_ENGLISH);
            Sram_InitSram(state,&select->sramCtx);
            sFailures+=ps2_locale_language()!=PS2_LANGUAGE_SPANISH || gSaveContext.language!=LANGUAGE_ENG;
            osSyncPrintf("[PS2 LANGUAGE MENU] %s selection, SRAM reload, western tables and unchanged save slots failures=%u\n",
                sFailures?"FAIL":"PASS",sFailures);
            SaveContext_Init();
            gSaveContext.fileNum=0xff;
            gSaveContext.save.linkAge=LINK_AGE_CHILD;
            MapSelect_LoadGame((MapSelectState*)state,ENTR_LINKS_HOUSE_0);
            gLocaleTestPhase=2; sStage=3; sFrames=0;
        }
    }
    ps2_runtime_test_update(state);
    if (logo && !state->running) {
        ps2_locale_logo_report(); gLocaleTestPhase=1;
        GameState* current=state; /* SET_NEXT_GAMESTATE declares its own 'state'. */
        SET_NEXT_GAMESTATE(current,FileSelect_Init,FileSelectState);
    }
    if (menu && sStage==1 && sFrames==20) capture(state,0);
    if (state->main!=Play_Main || sStage!=3) return;
    PlayState* play=(PlayState*)state;
    if (play->sceneId!=sScenes[sPlayPhase]) return;
    sFrames++;
    if (!sPlayPhase && sFrames==40) {
        Message_StartTextbox(play,0x00a4,NULL);
        int found=0;
        for (unsigned i=0;i+13<=play->msgCtx.font.msgLength;i++)
            if (!memcmp(play->msgCtx.font.msgBuf+i,"Espada Kokiri",13)) found=1;
        sFailures+=!found;
        osSyncPrintf("[PS2 LANGUAGE MESSAGE] %s real Message_OpenText: Espada Kokiri\n",sFailures?"FAIL":"PASS");
    }
    if (!sPlayPhase && sFrames==100) capture(state,1);
    if (sFrames!=120) return;
    sFailures+=!play->roomCtx.curRoom.segment || play->roomCtx.status;
    osSyncPrintf("[PS2 LANGUAGE TRANSITION] PASS phase=%u scene=%d frames=120\n",sPlayPhase,play->sceneId);
    if (sPlayPhase==2) {
        osSyncPrintf("[PS2 LANGUAGE GAME RESULT] %s logo-options-message-house-forest-house failures=%u\n",
            sFailures?"FAIL":"PASS",sFailures);
        ps2_runtime_trace_finish(); /* Runner can stop after all FAT writes close. */
        sStage=4; return;
    }
    if (!sPlayPhase) { sFailures+=gLocaleCaptureDone!=1; Message_CloseTextbox(play); }
    play->nextEntranceIndex=sEntrances[sPlayPhase++];
    play->transitionType=TRANS_TYPE_FADE_BLACK_FAST;
    play->transitionTrigger=TRANS_TRIGGER_START;
    sFrames=0;
}
