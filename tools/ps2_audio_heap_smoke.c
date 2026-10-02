/* Exercise the game's allocator, including the formerly missing return. */
#include "audio.h"
#include "ultra64.h"
extern int printf(const char*, ...);
AudioContext gAudioCtx;
static u8 bank[8192] __attribute__((aligned(16)));

int ps2_audio_heap_smoke(void) {
    int failures = 0;
    Stereo stereo;
    stereo.asByte = 8;
    if (!stereo.s.strongRight || stereo.s.strongLeft || stereo.s.usesHeadsetPanEffects) failures++;
    stereo.asByte = 4;
    if (!stereo.s.strongLeft || stereo.s.strongRight) failures++;
    stereo.asByte = 2;
    if (!stereo.s.stereoHeadsetEffects || stereo.s.strongRight) failures++;
    stereo.asByte = 1;
    if (!stereo.s.usesHeadsetPanEffects || stereo.s.strongRight) failures++;
    stereo.asByte = 0x30;
    if (stereo.s.bit2 != 3 || stereo.s.strongRight || stereo.s.usesHeadsetPanEffects) failures++;
    AudioHeap_InitPool(&gAudioCtx.permanentPool, bank, sizeof(bank));
    void* first = AudioHeap_AllocPermanent(FONT_TABLE, 1, 6064);
    void* second = AudioHeap_AllocPermanent(SEQUENCE_TABLE, 2, 512);
    if (first != bank || second != bank + 6064 || first == &gAudioCtx) failures++;
    if (gAudioCtx.permanentCache[0].ramAddr != first ||
        gAudioCtx.permanentCache[0].id != 1 ||
        gAudioCtx.permanentCache[0].tableType != FONT_TABLE ||
        gAudioCtx.permanentCache[1].ramAddr != second) failures++;
    if (AudioHeap_AllocPermanent(FONT_TABLE, 3, 8192) != NULL) failures++;
    printf("[PS2 AUDIO HEAP TEST] %s: returned pointer, metadata, exhaustion (%d)\n",
                 failures ? "FAIL" : "PASS", failures);
    return failures;
}
