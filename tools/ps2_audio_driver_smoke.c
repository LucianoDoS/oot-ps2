/* Link the real EE driver against an audsrv test double. The game's AI
 * feedback must preserve the IOP reserve and reject incomplete PCM blocks. */
#include <audsrv.h>
#include <stdio.h>
#include <string.h>
#include "ps2_types.h"
extern void PS2Audio_Init(void);
extern s32 osAiSetFrequency(u32);
extern u32 osAiGetLength(void);
extern int PS2Audio_SubmitBuffer(void*, unsigned);
static int queued, available, sent, plays;
static int formatFrequency, capturedBytes;
static s16 captured[8192], whole[96], split[96];
int audsrv_init(void) { return 0; }
int audsrv_set_format(struct audsrv_fmt_t* format) {
    formatFrequency = format->freq;
    return format->channels == 2 && format->bits == 16 ? 0 : -1;
}
int audsrv_set_volume(int volume) { (void)volume; return 0; }
int audsrv_queued(void) { return queued; }
int audsrv_available(void) { return available; }
int audsrv_play_audio(const char* buffer, int bytes) {
    plays++;
    capturedBytes = bytes;
    if (bytes <= sizeof(captured)) memcpy(captured, buffer, bytes);
    return sent < 0 ? sent : bytes;
}
int ps2_audio_driver_smoke(void) {
    int failures = 0;
    PS2Audio_Init();
    if (osAiSetFrequency(32000) != 32000 || formatFrequency != 48000) failures++;
    queued = 0; if (osAiGetLength()) failures++;
    queued = 3840; if (osAiGetLength()) failures++;
    queued = 4608; if (osAiGetLength() != 512) failures++;
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
    /* Stereo ramp: 32 input frames become 48 output frames. Splitting
     * after 11 inputs must preserve both fractional phase and history. */
    for (int i = 0; i < 32; i++) { pcm[2*i] = i*300; pcm[2*i+1] = -i*600; }
    sent = 0; available = sizeof(captured);
    osAiSetFrequency(32000);
    if (PS2Audio_SubmitBuffer(pcm, 128) || capturedBytes != sizeof(whole)) failures++;
    memcpy(whole, captured, sizeof(whole));
    for (int i = 0; i < 48; i++) {
        /* One input-frame interpolation latency, clamped at startup. */
        int position = i * 2 - 3;
        int left = position > 0 ? position * 100 : 0;
        if (whole[2*i] != left || whole[2*i+1] != -2*left) failures++;
    }
    osAiSetFrequency(32000);
    if (PS2Audio_SubmitBuffer(pcm, 44) || capturedBytes != 68) failures++;
    memcpy(split, captured, 68);
    int firstBytes = capturedBytes;
    available = 3;
    int previousPlays = plays;
    if (PS2Audio_SubmitBuffer(pcm + 22, 84) != -1 || plays != previousPlays) failures++;
    available = sizeof(captured);
    if (PS2Audio_SubmitBuffer(pcm + 22, 84) || capturedBytes != 124) failures++;
    memcpy((char*)split + firstBytes, captured, 124);
    if (memcmp(whole, split, sizeof(whole))) failures++;
    printf("[PS2 AUDIO DRIVER TEST] %s: AI reserve, 48 kHz continuous stereo, whole blocks and errors (%d)\n",
           failures ? "FAIL" : "PASS", failures);
    return failures;
}
