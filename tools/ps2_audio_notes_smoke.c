/* Exercise production note updates using a real, low-address EE layer. */
#include "audio.h"
#include "string.h"
extern int printf(const char*, ...);
static Note note;
static SequenceLayer layer;
static SequenceChannel channel;
static NoteSampleState ticks[3];
static TunedSample sample;
int ps2_audio_notes_smoke(void) {
    int failures=0;
    memset(&note,0,sizeof(note)); memset(&layer,0,sizeof(layer));
    memset(&channel,0,sizeof(channel)); memset(ticks,0,sizeof(ticks));
    gAudioCtx.notes=&note; gAudioCtx.numNotes=1; gAudioCtx.sampleStates=ticks;
    gAudioCtx.sampleStateOffset=0; gAudioCtx.soundOutputMode=SOUND_OUTPUT_MONO;
    gAudioCtx.audioBufferParameters.resampleRate=1.0f;
    layer.enabled=true; layer.note=&note; layer.channel=&channel;
    layer.noteFreqScale=1.25f; layer.noteVelocity=0.5f; layer.notePan=64;
    channel.seqPlayer=&gAudioCtx.seqPlayers[0]; channel.seqPlayer->muted=false;
    note.playbackState.parentLayer=&layer; note.playbackState.priority=3;
    note.playbackState.vibratoFreqScale=1.0f; note.playbackState.portamentoFreqScale=1.0f;
    note.playbackState.adsr.action.s.state=ADSR_STATE_HANG;
    note.playbackState.adsr.current=0.5f;
    note.sampleState.bitField0.enabled=true; note.sampleState.bitField0.needsInit=true;
    note.sampleState.tunedSample=&sample;
    Audio_ProcessNotes();
    printf("[PS2 LIVE NOTE] first enabled=%u init=%u pitch=%04x volume=%u,%u\n",ticks[0].bitField0.enabled,ticks[0].bitField0.needsInit,ticks[0].resamplingRateFixedPoint,ticks[0].targetVolLeft,ticks[0].targetVolRight);
    if (!ticks[0].bitField0.enabled || !ticks[0].bitField0.needsInit ||
        ticks[0].tunedSample!=&sample || ticks[0].resamplingRateFixedPoint!=0xa000 ||
        ticks[0].targetVolLeft!=723 || ticks[0].targetVolRight!=723) failures++;
    /* The next tick must see a mute, not replay the preceding loud note. */
    gAudioCtx.sampleStateOffset=1; channel.seqPlayer->muted=true;
    channel.muteBehavior=MUTE_BEHAVIOR_3; layer.noteFreqScale=0.75f;
    Audio_ProcessNotes();
    if (ticks[1].targetVolLeft || ticks[1].targetVolRight || ticks[1].resamplingRateFixedPoint) failures++;
    gAudioCtx.sampleStateOffset=2; channel.seqPlayer->muted=false;
    note.sampleState.bitField0.needsInit=false; layer.noteVelocity=1.0f;
    Audio_ProcessNotes();
    if (ticks[2].bitField0.needsInit || ticks[2].resamplingRateFixedPoint!=0x6000 ||
        ticks[2].targetVolLeft!=1447 || ticks[2].targetVolRight!=1447) failures++;
    gAudioCtx.notes=NULL; gAudioCtx.numNotes=0; gAudioCtx.sampleStates=NULL; gAudioCtx.sampleStateOffset=0;
    printf("[PS2 LIVE NOTE TEST] %s: native layer, init, pitch, volume and mute per tick (%d)\n",failures?"FAIL":"PASS",failures);
    return failures;
}
