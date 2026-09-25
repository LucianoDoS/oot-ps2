/**
 * ps2_seq.c — Audio Sequence (MIDI) API Stubs for OoT
 *
 * OoT plays background music using sequences similar to MIDI,
 * interpreted by the audio thread.
 */

#include "ps2_types.h"

/* ─────────────────────────────────────────────────────────── *
 *  OoT Sequence API Stubs
 * ─────────────────────────────────────────────────────────── */

void Audio_PlaySequence(u8 seqPlayerIndex, u16 seqId, u16 fadeTimer) {
    /* 
     * In a full port, this would start the MIDI sequencer for seqId,
     * assigning voices to play instrument samples.
     */
    (void)seqPlayerIndex;
    (void)seqId;
    (void)fadeTimer;
}

void Audio_StopSequence(u8 seqPlayerIndex, u16 fadeTimer) {
    (void)seqPlayerIndex;
    (void)fadeTimer;
}

void Audio_PlaySequenceWithCustomBank(u8 seqPlayerIndex, u16 seqId, u8 bankId, u16 fadeTimer) {
    (void)seqPlayerIndex;
    (void)seqId;
    (void)bankId;
    (void)fadeTimer;
}

void Audio_SetSequenceMode(u8 seqMode) {
    (void)seqMode;
}
