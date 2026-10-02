/* Exact-output fixtures run on the EE through the production audio adapter. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
extern int PS2RspAudio_Process(void*, unsigned);
static int16_t input[64] __attribute__((aligned(16)));
static int16_t resampledFull[32] __attribute__((aligned(16)));
static int16_t resampledParts[32] __attribute__((aligned(16)));
static int16_t output[32] __attribute__((aligned(16)));
static int16_t history[16] __attribute__((aligned(16)));
static int16_t book[16] __attribute__((aligned(16)));
static int16_t filterCoefficients[8] __attribute__((aligned(16)));
static int16_t filterState[16] __attribute__((aligned(16)));
static uint8_t compressed[64] __attribute__((aligned(16)));
static int16_t decodedFull[80] __attribute__((aligned(16)));
static int16_t decodedParts[80] __attribute__((aligned(16)));
static int16_t continued[48] __attribute__((aligned(16)));
static int16_t priorHistory[16] __attribute__((aligned(16)));
static uint32_t commands[12][2] __attribute__((aligned(16)));

int ps2_audio_smoke(void) {
    int failures = 0;
    for (int i = 0; i < 16; i++) input[i] = (i - 8) * 1024;
    commands[0][0] = 0x14020100; commands[0][1] = (uint32_t)input; /* load 32 bytes */
    commands[1][0] = 0x02000200; commands[1][1] = 32; /* clear mix destination */
    commands[2][0] = 0x0c024000; commands[2][1] = 0x01000200; /* gain .5 */
    commands[3][0] = 0x0d020300; commands[3][1] = 0x01000200; /* interleave L/R */
    commands[4][0] = 0x15040300; commands[4][1] = (uint32_t)output;
    if (PS2RspAudio_Process(commands, 5 * 8)) failures++;
    for (int i = 0; i < 16; i++)
        if (output[i * 2] != input[i] || output[i * 2 + 1] != input[i] / 2) failures++;
    /* Zero predictor ADPCM: every 4-bit residual is +1, with scale zero. */
    memset(compressed, 0x11, sizeof(compressed)); compressed[0] = 0;
    memset(output, 0x7f, sizeof(output));
    commands[0][0] = 0x14010100; commands[0][1] = (uint32_t)compressed;
    commands[1][0] = 0x0b000020; commands[1][1] = (uint32_t)book;
    commands[2][0] = 0x08000100; commands[2][1] = 0x02000020;
    commands[3][0] = 0x01010000; commands[3][1] = (uint32_t)history;
    commands[4][0] = 0x15040200; commands[4][1] = (uint32_t)output;
    if (PS2RspAudio_Process(commands, 5 * 8)) failures++;
    for (int i = 0; i < 16; i++)
        if (output[i] != 0 || output[i + 16] != 1 || history[i] != 1) failures++;
    for (int i = 0; i < 16; i++) compressed[i] = (uint8_t)(i - 8);
    commands[1][0] = 0x08000100; commands[1][1] = 0x02000020;
    commands[2][0] = 0x17010000; commands[2][1] = (uint32_t)history;
    commands[3][0] = 0x15040200; commands[3][1] = (uint32_t)output;
    if (PS2RspAudio_Process(commands, 4 * 8)) failures++;
    for (int i = 0; i < 16; i++)
        if (output[i] != 0 || output[i + 16] != (i - 8) * 256 ||
            history[i] != (i - 8) * 256) failures++;
    /* A note split across synthesis ticks must retain FIR history and
     * fractional pitch: output must match one uninterrupted block. */
    for (int i = 0; i < 64; i++) input[i] = (i % 13 - 6) * 4096;
    commands[0][0] = 0x14040100; commands[0][1] = (uint32_t)input;
    commands[1][0] = 0x08000100; commands[1][1] = 0x02000040;
    commands[2][0] = 0x05016000; commands[2][1] = (uint32_t)history; /* pitch .75, init */
    commands[3][0] = 0x15040200; commands[3][1] = (uint32_t)resampledFull;
    if (PS2RspAudio_Process(commands, 4 * 8)) failures++;
    commands[1][1] = 0x02000020;
    commands[3][0] = 0x15020200; commands[3][1] = (uint32_t)resampledParts;
    if (PS2RspAudio_Process(commands, 4 * 8)) failures++;
    commands[0][1] = (uint32_t)(input + 12);
    commands[2][0] = 0x05006000; /* continue */
    commands[3][1] = (uint32_t)(resampledParts + 16);
    if (PS2RspAudio_Process(commands, 4 * 8) ||
        memcmp(resampledFull, resampledParts, sizeof(resampledFull))) failures++;
    /* The filter sees native, chronological samples and coefficients.
     * An impulse must produce the taps in order, then carry its tail into
     * the next block. These distinct taps expose word-pair lane swaps. */
    memset(input, 0, sizeof(input)); input[6] = 16384;
    memset(filterState, 0, sizeof(filterState));
    for (int i = 0; i < 8; i++)
        filterState[8 + i] = filterCoefficients[i] = (i + 1) * 1024;
    commands[0][0] = 0x14010100; commands[0][1] = (uint32_t)input;
    commands[1][0] = 0x07020010; commands[1][1] = (uint32_t)filterCoefficients;
    commands[2][0] = 0x07000100; commands[2][1] = (uint32_t)filterState;
    commands[3][0] = 0x15010100; commands[3][1] = (uint32_t)output;
    if (PS2RspAudio_Process(commands, 4 * 8)) failures++;
    for (int i = 0; i < 8; i++) {
        int expected = i < 6 ? 0 : (i - 5) * 512;
        if (output[i] != expected) failures++;
    }
    commands[0][1] = (uint32_t)(input + 8);
    if (PS2RspAudio_Process(commands, 4 * 8)) failures++;
    for (int i = 0; i < 8; i++) {
        int expected = i < 6 ? (i + 3) * 512 : 0;
        if (output[i] != expected) failures++;
    }
    if (PS2RspAudio_Process(commands, 7) != -1) failures++;
    /* OoT's HILOGAIN byte is unsigned UQ4.4, including values above 7.9375.
     * Amplification must preserve polarity and saturate both signs. */
    const unsigned gains[] = {0x10,0x7f,0x80,0xff};
    for (unsigned g = 0; g < sizeof(gains)/sizeof(gains[0]); g++) {
        for (int i = 0; i < 16; i++) input[i] = (i-8)*1024;
        commands[0][0]=0x14020100; commands[0][1]=(uint32_t)input;
        commands[1][0]=0x0e000020 | (gains[g]<<16); commands[1][1]=0x01000000;
        commands[2][0]=0x15020100; commands[2][1]=(uint32_t)output;
        if (PS2RspAudio_Process(commands,3*8)) failures++;
        for (int i=0;i<16;i++) {
            int value=(input[i]*(int)gains[g])>>4;
            if (value>32767) value=32767;
            if (value< -32768) value= -32768;
            if (output[i]!=value) failures++;
        }
    }
    /* Voice samples span synthesis blocks: exercise a nonzero predictor
     * over four compressed frames, comparing continuous and split decoding.
     * The second block's prefix must equal the previous decoder history. */
    for (int i = 0; i < 8; i++) { book[i] = -64 + i*9; book[8+i] = 320-i*13; }
    for (int frame = 0; frame < 4; frame++) {
        compressed[frame*9] = 0x50;
        for (int i = 1; i < 9; i++) compressed[frame*9+i] = (frame*31+i*17) & 255;
    }
    commands[0][0] = 0x14040100; commands[0][1] = (uint32_t)compressed;
    commands[1][0] = 0x0b000020; commands[1][1] = (uint32_t)book;
    commands[2][0] = 0x08000100; commands[2][1] = 0x02000080;
    commands[3][0] = 0x01010000; commands[3][1] = (uint32_t)history;
    commands[4][0] = 0x150a0200; commands[4][1] = (uint32_t)decodedFull;
    if (PS2RspAudio_Process(commands, 5*8)) failures++;
    commands[2][1] = 0x02000040;
    commands[4][0] = 0x15060200; commands[4][1] = (uint32_t)decodedParts;
    if (PS2RspAudio_Process(commands, 5*8)) failures++;
    memcpy(priorHistory, history, sizeof(history));
    /* LOADBUFF rounds the RAM source down to eight bytes, just like the RSP.
     * The next two frames start two bytes into that aligned transfer. */
    commands[0][1] = (uint32_t)(compressed + 16);
    commands[2][0] = 0x08000102;
    commands[3][0] = 0x01000000;
    commands[4][1] = (uint32_t)continued;
    if (PS2RspAudio_Process(commands, 5*8) || memcmp(continued, priorHistory, sizeof(history))) {
        printf("[PS2 ADPCM TEST] continuation prefix failed\n"); failures++;
    }
    memcpy(decodedParts + 48, continued + 16, 64);
    if (memcmp(decodedFull, decodedParts, sizeof(decodedFull))) {
        for (int i = 0; i < 80; i++) if (decodedFull[i] != decodedParts[i]) {
            printf("[PS2 ADPCM TEST] split decoding failed at %d: %d != %d\n", i,decodedFull[i],decodedParts[i]); break;
        }
        failures++;
    }
    /* A_LOOP selects the supplied loop history, not the note's last block. */
    commands[1][0] = 0x0f000000; commands[1][1] = (uint32_t)priorHistory;
    commands[3][0] = 0x01020000;
    commands[4][1] = (uint32_t)decodedParts;
    if (PS2RspAudio_Process(commands, 5*8) || memcmp(continued, decodedParts, sizeof(continued))) {
        printf("[PS2 ADPCM TEST] loop history failed\n"); failures++;
    }
    printf("[PS2 AUDIO TEST] %s: PCM/mix/stereo, ADPCM/S8, resample/filter continuity and bounds (%d)\n",
           failures ? "FAIL" : "PASS", failures);
    return failures;
}
