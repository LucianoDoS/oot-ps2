/* Link the real EE driver against an audsrv test double. The game's AI
 * feedback must preserve the IOP reserve and reject incomplete PCM blocks. */
#include <audsrv.h>
#include <stdio.h>
#include "ps2_types.h"
extern void PS2Audio_Init(void);
extern s32 osAiSetFrequency(u32);
extern u32 osAiGetLength(void);
extern int PS2Audio_SubmitBuffer(void*, unsigned);
static int queued, available, sent, plays;
int audsrv_init(void) { return 0; }
int audsrv_set_format(struct audsrv_fmt_t* format) { return format->channels == 2 && format->bits == 16 ? 0 : -1; }
int audsrv_set_volume(int volume) { (void)volume; return 0; }
int audsrv_queued(void) { return queued; }
int audsrv_available(void) { return available; }
int audsrv_play_audio(const char* buffer, int bytes) { (void)buffer; plays++; return sent < 0 ? sent : bytes; }
int ps2_audio_driver_smoke(void) {
    int failures = 0;
    PS2Audio_Init();
    if (osAiSetFrequency(32000) != 32000) failures++;
    queued = 0; if (osAiGetLength()) failures++;
    queued = 2560; if (osAiGetLength()) failures++;
    queued = 3072; if (osAiGetLength() != 512) failures++;
    queued = -1; if (osAiGetLength()) failures++;
    if (osAiSetFrequency(48000) != 48000) failures++;
    queued = 3840; if (osAiGetLength()) failures++;
    queued = 4864; if (osAiGetLength() != 1024) failures++;
    static s16 pcm[1024];
    available = sizeof(pcm) - 4;
    if (PS2Audio_SubmitBuffer(pcm, sizeof(pcm)) != -1 || plays) failures++;
    available = sizeof(pcm); sent = 0;
    if (PS2Audio_SubmitBuffer(pcm, sizeof(pcm)) || plays != 1) failures++;
    if (PS2Audio_SubmitBuffer(pcm, sizeof(pcm)-1) != -1 || plays != 1) failures++;
    sent = -1;
    if (PS2Audio_SubmitBuffer(pcm, sizeof(pcm)) != -1) failures++;
    printf("[PS2 AUDIO DRIVER TEST] %s: AI reserve at 32/48 kHz, whole PCM blocks and errors (%d)\n",
           failures ? "FAIL" : "PASS", failures);
    return failures;
}
