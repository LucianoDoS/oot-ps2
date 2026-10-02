#include "audio.h"
#include "string.h"
extern int printf(const char*, ...);

int ps2_audio_envelope_smoke(void) {
    int failures = 0;
    /* Raw sequence data may be unaligned and remains big-endian. The
     * font envelope has already been converted and must stay native. */
    u8 sequence[] = {0, 0,2,0x7f,0xff, 0xff,0xfe,0,3,
        0,1,0,0, 0xff,0xff,0,0};
    EnvelopePoint native[] = {{2,32767}, {-2,3}, {1,0}, {-1,0}};
    struct { AudioTableHeader header; AudioTableEntry entries[2]; } table = {0};
    table.entries[0].romAddr = 1; /* sequence alias */
    table.entries[1].size = sizeof(sequence);
    gAudioCtx.sequenceTable = (AudioTable*)&table;
    gAudioCtx.numSequences = 2;
    gAudioCtx.audioBufferParameters.numSequencePlayers = 1;
    gAudioCtx.audioBufferParameters.ticksPerUpdateScaled = 1;
    gAudioCtx.seqPlayers[0].seqId = 0;
    gAudioCtx.seqPlayers[0].seqData = sequence;
    AdsrState encoded = {0}, converted = {0};
    Audio_AdsrInit(&encoded, (EnvelopePoint*)(sequence + 1), NULL);
    Audio_AdsrInit(&converted, native, NULL);
    if (!encoded.unk_18[0] || converted.unk_18[0]) failures++;
    encoded.action.s.state = converted.action.s.state = ADSR_STATE_INITIAL;
    for (int i = 0; i < 12; i++) {
        f32 a = Audio_AdsrUpdate(&encoded), b = Audio_AdsrUpdate(&converted);
        if (a != b || encoded.envIndex != converted.envIndex ||
            encoded.delay != converted.delay || encoded.action.asByte != converted.action.asByte) failures++;
    }
    if (encoded.current != 1 || encoded.action.s.state != ADSR_STATE_HANG) failures++;
    /* Native initialization after sequence initialization clears the flag. */
    Audio_AdsrInit(&encoded, native, NULL);
    if (encoded.unk_18[0]) failures++;
    gAudioCtx.sequenceTable = NULL;
    gAudioCtx.seqPlayers[0].seqData = NULL;
    printf("[PS2 ENVELOPE TEST] %s: native/font and unaligned BE sequence, alias and jump (%d)\n",
           failures ? "FAIL" : "PASS", failures);
    return failures;
}
