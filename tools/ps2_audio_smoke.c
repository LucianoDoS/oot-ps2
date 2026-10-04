/* Exact-output fixtures run on the EE through the production audio adapter. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../src/platform/ps2/audio/rsp_hle/audio.h"
#include "../src/platform/ps2/audio/rsp_hle/alist.h"
#include "../src/platform/ps2/audio/rsp_hle/memory.h"
extern int PS2RspAudio_Process(void*, unsigned);
extern int ps2_rsp_kernel_smoke_run(void);
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

/* Original sequential predictor, with explicit 32-bit wrapping for extreme
 * coefficients. Keep each write ordered to test overlapping input/book data. */
static __attribute__((noinline)) void residuals_reference(int16_t* dst,const int16_t* src,
    const int16_t* coefficients,const int16_t* last,unsigned count) {
    int16_t l1=last[0],l2=last[1];
    for (unsigned i=0;i<count;i++) {
        uint32_t acc=(uint32_t)(int32_t)src[i]<<11;
        acc+=(uint32_t)((int32_t)coefficients[i]*l1);
        acc+=(uint32_t)((int32_t)coefficients[8+i]*l2);
        for (unsigned j=0;j<i;j++) acc+=(uint32_t)((int32_t)coefficients[8+j]*src[i-1-j]);
        int32_t value=(int32_t)acc>>11;
        dst[i]=value>32767 ? 32767 : value< -32768 ? -32768 : value;
    }
}

static uint32_t audio_cycles(void) {
    uint32_t value;
    __asm__ volatile("mfc0 %0, $9" : "=r"(value) :: "memory");
    return value;
}

static int16_t envmix_clamp(int value) {
    return value > 32767 ? 32767 : value < -32768 ? -32768 : value;
}

/* Independent copy of the original per-sample control reads and write order. */
static __attribute__((noinline)) void envmix_reference(struct hle_t* hle, bool swapWet,
    const uint16_t offsets[5], unsigned count, uint16_t* values,
    uint16_t* steps, const int16_t* masks) {
    int16_t* src=(int16_t*)(hle->alist_buffer+offsets[0]);
    int16_t* dl=(int16_t*)(hle->alist_buffer+offsets[1]);
    int16_t* dr=(int16_t*)(hle->alist_buffer+offsets[2]);
    int16_t* wl=(int16_t*)(hle->alist_buffer+offsets[swapWet ? 4 : 3]);
    int16_t* wr=(int16_t*)(hle->alist_buffer+offsets[swapWet ? 3 : 4]);
    count=(count+7)&~7u;
    while (count) {
        for (unsigned i=0;i<8;i++) {
            int16_t l=(((int32_t)src[i^S]*(uint32_t)values[0])>>16)^masks[0];
            int16_t r=(((int32_t)src[i^S]*(uint32_t)values[1])>>16)^masks[1];
            int16_t lw=(((int32_t)l*(uint32_t)values[2])>>16)^masks[2];
            int16_t rw=(((int32_t)r*(uint32_t)values[2])>>16)^masks[3];
            dl[i^S]=envmix_clamp(dl[i^S]+l);
            dr[i^S]=envmix_clamp(dr[i^S]+r);
            wl[i^S]=envmix_clamp(wl[i^S]+lw);
            wr[i^S]=envmix_clamp(wr[i^S]+rw);
        }
        values[0]+=steps[0]; values[1]+=steps[1]; values[2]+=steps[2];
        src+=8; dl+=8; dr+=8; wl+=8; wr+=8; count-=8;
    }
}

static int envmix_smoke(void) {
    static struct hle_t actualHle,referenceHle;
    uint32_t random=0x47cb9081;
    const unsigned counts[]={0,1,7,8,9,16,31,32,63,64};
    unsigned failures=0;
    for (unsigned fixture=0;fixture<512;fixture++) {
        for (unsigned c=0;c<10;c++) for (unsigned alias=0;alias<8;alias++) {
            uint16_t offsets[5]={0x100,0x400,0x600,0x800,0xa00};
            uint16_t av[3],rv[3],as[3],rs[3]; int16_t ax[4],rx[4];
            for (unsigned i=0;i<sizeof(actualHle.alist_buffer)/2;i++) {
                random=random*1664525u+1013904223u;
                ((int16_t*)actualHle.alist_buffer)[i]=(int16_t)(random>>16);
            }
            for (unsigned i=0;i<3;i++) {
                random=random*1664525u+1013904223u; av[i]=random>>16;
                random=random*1664525u+1013904223u; as[i]=random>>16;
            }
            for (unsigned i=0;i<4;i++) ax[i]=(fixture&1) ? -(int16_t)((fixture>>i)&1) : (int16_t)(random>>(i*4));
            if (fixture<4) for (unsigned i=0;i<3;i++) av[i]=(uint16_t[]){0,0xffff,0x8000,1}[fixture];
            if (alias==1) offsets[1]=offsets[0];
            if (alias==2) offsets[2]=offsets[1];
            if (alias==3) offsets[1]=offsets[0]+2;
            if (alias==4) offsets[3]=offsets[0];
            memcpy(referenceHle.alist_buffer,actualHle.alist_buffer,sizeof(actualHle.alist_buffer));
            memcpy(rv,av,sizeof(av)); memcpy(rs,as,sizeof(as)); memcpy(rx,ax,sizeof(ax));
            uint16_t* avp=alias==5 ? (uint16_t*)(actualHle.alist_buffer+offsets[1]+2) : av;
            uint16_t* rvp=alias==5 ? (uint16_t*)(referenceHle.alist_buffer+offsets[1]+2) : rv;
            uint16_t* asp=alias==7 ? (uint16_t*)(actualHle.alist_buffer+offsets[3]+4) : as;
            uint16_t* rsp=alias==7 ? (uint16_t*)(referenceHle.alist_buffer+offsets[3]+4) : rs;
            int16_t* axp=alias==6 ? (int16_t*)(actualHle.alist_buffer+offsets[2]+6) : ax;
            int16_t* rxp=alias==6 ? (int16_t*)(referenceHle.alist_buffer+offsets[2]+6) : rx;
            bool swapWet=fixture&1;
            alist_envmix_nead(&actualHle,swapWet,offsets[1],offsets[2],offsets[3],offsets[4],offsets[0],counts[c],avp,asp,axp);
            envmix_reference(&referenceHle,swapWet,offsets,counts[c],rvp,rsp,rxp);
            if (memcmp(actualHle.alist_buffer,referenceHle.alist_buffer,sizeof(actualHle.alist_buffer)) ||
                memcmp(av,rv,sizeof(av)) || memcmp(as,rs,sizeof(as)) || memcmp(ax,rx,sizeof(ax))) {
                if (!failures) printf("[PS2 ENVMIX TEST] first mismatch fixture=%u count=%u alias=%u\n",fixture,counts[c],alias);
                failures++;
            }
        }
    }
    printf("[PS2 ENVMIX TEST] 40960 cases: gain ramps, saturation, XOR, swap and aliases failures=%u\n",failures);
    memset(actualHle.alist_buffer,0,sizeof(actualHle.alist_buffer));
    for (unsigned i=0;i<256;i++) ((int16_t*)(actualHle.alist_buffer+0x100))[i]=(i%13-6)*4096;
    memcpy(referenceHle.alist_buffer,actualHle.alist_buffer,sizeof(actualHle.alist_buffer));
    const uint16_t offsets[5]={0x100,0x400,0x600,0x800,0xa00};
    uint16_t av[3]={0x1234,0x4567,0xabcd},rv[3]; memcpy(rv,av,sizeof(av));
    uint16_t steps[3]={0x132,0xf123,0x11}; int16_t masks[4]={0,-1,0,-1};
    uint32_t start=audio_cycles();
    for (unsigned i=0;i<1000;i++) envmix_reference(&referenceHle,i&1,offsets,256,rv,steps,masks);
    uint32_t oldCycles=audio_cycles()-start;
    start=audio_cycles();
    for (unsigned i=0;i<1000;i++) alist_envmix_nead(&actualHle,i&1,offsets[1],offsets[2],offsets[3],offsets[4],offsets[0],256,av,steps,masks);
    uint32_t newCycles=audio_cycles()-start;
    if (memcmp(actualHle.alist_buffer,referenceHle.alist_buffer,sizeof(actualHle.alist_buffer)) || memcmp(av,rv,sizeof(av))) failures++;
    printf("[PS2 ENVMIX BENCH] oldCycles=%u newCycles=%u failures=%u\n",oldCycles,newCycles,failures);
    return failures;
}

int ps2_audio_smoke(void) {
    int failures = envmix_smoke();
    failures += ps2_rsp_kernel_smoke_run();
    uint32_t random=0xa317ca09;
    int16_t actual[64],expected[64];
    const unsigned aliases[][3]={{8,24,40},{8,8,40},{9,8,40},{7,8,40},{40,8,40}};
    for (unsigned fixture=0;fixture<4096;fixture++) {
        for (unsigned i=0;i<64;i++) {
            random=random*1664525u+1013904223u;
            actual[i]=(int16_t)(random>>16);
        }
        if (fixture<4) for (unsigned i=0;i<64;i++)
            actual[i]=fixture==0 ? 0 : fixture==1 ? 32767 : fixture==2 ? -32768 : (i&1 ? 32767 : -32768);
        for (unsigned count=0;count<=8;count++) {
            memcpy(expected,actual,sizeof(actual));
            int16_t result[10],reference[10];
            memset(result,0x5a,sizeof(result)); memcpy(reference,result,sizeof(result));
            adpcm_compute_residuals(result,actual+8,actual+40,actual+6,count);
            residuals_reference(reference,expected+8,expected+40,expected+6,count);
            if (memcmp(result,reference,sizeof(result))) failures++;
        }
        /* Copies make the input/book mutations from aliasing comparable. */
        for (unsigned a=0;a<5;a++) {
            int16_t aliased[64]; memcpy(aliased,actual,sizeof(actual));
            memcpy(expected,actual,sizeof(actual));
            adpcm_compute_residuals(aliased+aliases[a][0],aliased+aliases[a][1],aliased+aliases[a][2],aliased+6,8);
            residuals_reference(expected+aliases[a][0],expected+aliases[a][1],expected+aliases[a][2],expected+6,8);
            if (memcmp(aliased,expected,sizeof(expected))) failures++;
        }
    }
    printf("[PS2 ADPCM PREDICTOR TEST] 4096 fixtures, lengths 0-8, wrapping, saturation and aliasing failures=%d\n",failures);
    int16_t benchResult[8],benchReference[8];
    uint32_t start=audio_cycles();
    for (unsigned i=0;i<10000;i++) residuals_reference(benchReference,actual+8,actual+40,actual+(i&3),8);
    uint32_t oldCycles=audio_cycles()-start;
    start=audio_cycles();
    for (unsigned i=0;i<10000;i++) adpcm_compute_residuals(benchResult,actual+8,actual+40,actual+(i&3),8);
    uint32_t newCycles=audio_cycles()-start;
    if (memcmp(benchResult,benchReference,sizeof(benchResult))) failures++;
    printf("[PS2 ADPCM PREDICTOR BENCH] oldCycles=%u newCycles=%u failures=%d\n",oldCycles,newCycles,failures);
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
