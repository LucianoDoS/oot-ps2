/**
 * ps2_sfx.c — Sound Effects API Stubs for OoT
 *
 * OoT calls functions like Audio_PlaySoundGeneral to trigger SFX.
 * This file translates those requests into calls to our ps2_audio_driver.
 */

#include "ps2_types.h"

/* Forward declare driver API */
extern int Audio_PlaySfx(const s16* samples, u32 numSamples, s16 volL, s16 volR, u32 pitch, bool loop);

/* ─────────────────────────────────────────────────────────── *
 *  OoT SFX API Stubs
 * ─────────────────────────────────────────────────────────── */

void Audio_PlaySoundGeneral(u16 sfxId, void* pos, u8 token, 
                            float* freqScale, float* vol, float* reverbAdd) {
    /* 
     * In a full port, this function would:
     * 1. Look up sfxId in the sound tables.
     * 2. Find the ADPCM sample data.
     * 3. Decode ADPCM to PCM (or use offline-converted PCM).
     * 4. Calculate spatial volume/panning based on 'pos' (Vec3f*).
     * 5. Call Audio_PlaySfx().
     */
     
    (void)sfxId;
    (void)pos;
    (void)token;
    (void)freqScale;
    (void)vol;
    (void)reverbAdd;
}

void Audio_PlaySoundCommon(u16 sfxId, void* pos) {
    Audio_PlaySoundGeneral(sfxId, pos, 0, NULL, NULL, NULL);
}

void Audio_StopSfxByPos(void* pos) {
    (void)pos;
}

void Audio_StopSfxByPosAndId(void* pos, u16 sfxId) {
    (void)pos;
    (void)sfxId;
}
