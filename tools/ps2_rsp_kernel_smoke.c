/* Exact native-EE DSP fixtures. References retain the original masked
 * resampler and int64 chronological filter, independent of the fast kernels. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../src/platform/ps2/audio/rsp_hle/alist.h"
#include "../src/platform/ps2/audio/rsp_hle/audio.h"
#include "../src/platform/ps2/audio/rsp_hle/hle_internal.h"

struct kernel_fixture {
    struct hle_t hle;
    unsigned char ram[512] __attribute__((aligned(16)));
};
static struct kernel_fixture actual, expected;
static uint32_t random_state=0x592a6f87u;

#ifdef PS2_RENDERER_SMOKE_CAPTURE
extern unsigned ps2_resample_direct_runs, ps2_resample_masked_runs;
#endif

static uint32_t kernel_cycles(void) {
#ifdef _EE
    uint32_t value;
    __asm__ volatile("mfc0 %0, $9" : "=r"(value) :: "memory");
    return value;
#else
    return 0;
#endif
}

static uint16_t next_value(void) {
    random_state=random_state*1664525u+1013904223u;
    return random_state>>16;
}

static void fill_fixture(struct kernel_fixture* f, unsigned pattern) {
    memset(f,0,sizeof(*f));
    for (unsigned i=0;i<sizeof(f->hle.alist_buffer)/2;i++) {
        int16_t value=(int16_t)next_value();
        if (pattern<4) value=pattern==0 ? 0 : pattern==1 ? 32767 :
            pattern==2 ? -32768 : (i&1 ? 32767 : -32768);
        ((int16_t*)f->hle.alist_buffer)[i]=value;
    }
    for (unsigned i=0;i<sizeof(f->ram)/2;i++) ((uint16_t*)f->ram)[i]=next_value();
    f->hle.dram=f->ram;
}

static void clone_fixture(struct kernel_fixture* to, const struct kernel_fixture* from) {
    memcpy(to,from,sizeof(*to));
    to->hle.dram=from->hle.dram==from->ram ? to->ram : to->hle.alist_buffer;
}

/* Compare all bytes, including untouched synthesis state and buffer guards.
 * Only the fixtures' different RAM base pointers are normalized. */
static bool fixture_equal(struct kernel_fixture* a, struct kernel_fixture* b) {
    unsigned char* ap=a->hle.dram;
    unsigned char* bp=b->hle.dram;
    a->hle.dram=b->hle.dram=NULL;
    bool same=memcmp(a,b,sizeof(*a))==0;
    a->hle.dram=ap; b->hle.dram=bp;
    return same;
}

static int16_t* reference_sample(struct hle_t* hle, unsigned pos) {
    return (int16_t*)hle->alist_buffer+(pos&0xfff);
}

static uint16_t* reference_ram(struct hle_t* hle, uint32_t address) {
    return (uint16_t*)((uintptr_t)hle->dram+(address&0xffffff));
}

static __attribute__((noinline)) void resample_reference(struct hle_t* hle,
    bool init, uint16_t dmemo, uint16_t dmemi, uint16_t count,
    uint32_t pitch, uint32_t address) {
    uint16_t ipos=(dmemi>>1)-4, opos=dmemo>>1;
    uint32_t phase;
    count>>=1;
    if (init) {
        for (unsigned k=0;k<4;k++) *reference_sample(hle,ipos+k)=0;
        phase=0;
    } else {
        for (unsigned k=0;k<4;k++)
            *reference_sample(hle,ipos+k)=*reference_ram(hle,address+k*2);
        phase=*reference_ram(hle,address+8);
    }
    while (count) {
        const int16_t* lut=RESAMPLE_LUT+((phase&0xfc00)>>8);
        uint32_t sum=(uint32_t)((int32_t)*reference_sample(hle,ipos)*lut[0]) +
            (uint32_t)((int32_t)*reference_sample(hle,ipos+1)*lut[1]) +
            (uint32_t)((int32_t)*reference_sample(hle,ipos+2)*lut[2]) +
            (uint32_t)((int32_t)*reference_sample(hle,ipos+3)*lut[3]);
        int32_t value=(int32_t)sum>>15;
        *reference_sample(hle,opos++)=value>32767 ? 32767 : value< -32768 ? -32768 : value;
        phase+=pitch;
        ipos+=phase>>16;
        phase&=0xffff;
        --count;
    }
    for (unsigned k=0;k<4;k++)
        *reference_ram(hle,address+k*2)=*reference_sample(hle,ipos+k);
    *reference_ram(hle,address+8)=phase;
}

static __attribute__((noinline)) void filter_reference(struct hle_t* hle,
    uint16_t dmem, uint16_t count, uint32_t address, const uint32_t lut[2]) {
    int16_t output[0x3c0];
    if ((count&15) || count>sizeof(output) || (unsigned)dmem+count>sizeof(hle->alist_buffer) || !count)
        return;
    int16_t* coefficients=(int16_t*)reference_ram(hle,lut[0]);
    int16_t* other=(int16_t*)reference_ram(hle,lut[1]);
    int16_t* previous=(int16_t*)reference_ram(hle,address);
    int16_t* input=(int16_t*)(hle->alist_buffer+dmem);
    for (unsigned k=0;k<8;k++) {
        int32_t value=(other[k]+coefficients[k])>>1;
        coefficients[k]=value;
        other[k]=value;
    }
    int16_t* out=output;
    for (unsigned block=0;block<count;block+=16) {
        for (int lane=0;lane<8;lane++) {
            int64_t value=0x4000;
            for (int tap=0;tap<8;tap++) {
                int index=lane-tap;
                int16_t sample=index<0 ? previous[8+index] : input[index];
                value+=(int32_t)sample*coefficients[tap];
            }
            out[lane]=(int16_t)(value>>15);
        }
        previous=input;
        input+=8;
        out+=8;
    }
    memcpy(reference_ram(hle,address),input-8,16);
    memcpy(hle->alist_buffer+dmem,output,count);
}

static unsigned resample_fixtures(void) {
    const uint16_t counts[]={0,1,2,3,16,31,32,64,128,400};
    const uint32_t pitches[]={0,1,0x7fff,0x8000,0xffff,0x10000,0x18000,0x1fffe,
        0x80000000,0xffff0000,0xffffffff,0xfffffffe,0x100,0x3ff};
    unsigned failures=0,cases=0;
#ifdef PS2_RENDERER_SMOKE_CAPTURE
    unsigned direct_start=ps2_resample_direct_runs, masked_start=ps2_resample_masked_runs;
#endif
    for (unsigned fixture=0;fixture<128;fixture++) {
        for (unsigned c=0;c<sizeof(counts)/sizeof(counts[0]);c++) {
            for (unsigned alias=0;alias<8;alias++) {
                uint16_t in=0x408,out=0xc00;
                uint32_t state=0x40;
                fill_fixture(&actual,fixture);
                if (alias==1) out=in-8;
                if (alias==2) out=in-6;
                if (alias==3) out=in-10;
                if (alias==4 || alias==5) {
                    actual.hle.dram=actual.hle.alist_buffer;
                    state=alias==4 ? out : in-8;
                }
                /* Masked indices remain in the real 2048-sample buffer.
                 * The high aliases force the unchanged scalar fallback. */
                if (alias==6) in+=0x2000;
                if (alias==7) out+=0x2000;
                *reference_ram(&actual.hle,state+8)=((fixture&63)<<10)|(next_value()&0x3ff);
                clone_fixture(&expected,&actual);
                uint32_t pitch=pitches[fixture%(sizeof(pitches)/sizeof(pitches[0]))];
                bool init=(fixture>>6)!=0;
                alist_resample(&actual.hle,init,false,out,in,counts[c],pitch,state);
                resample_reference(&expected.hle,init,out,in,counts[c],pitch,state);
                if (!fixture_equal(&actual,&expected)) {
                    if (!failures) printf("[PS2 RESAMPLE KERNEL TEST] first mismatch fixture=%u count=%u alias=%u pitch=%u\n",
                        fixture,counts[c],alias,pitch);
                    failures++;
                }
                cases++;
            }
        }
    }
    /* Exact buffer-end guards, all 64 LUT phases, and odd byte addresses. */
    for (unsigned phase=0;phase<64;phase++) {
        for (unsigned edge=0;edge<4;edge++) {
            fill_fixture(&actual,phase+4);
            uint16_t in=edge==0 ? 0x408 : edge==1 ? 4096 : edge==2 ? 4094 : 4095;
            uint16_t out=edge<2 ? 4094 : 4095;
            uint32_t pitch=edge<2 ? 0 : 0x10000;
            *reference_ram(&actual.hle,0x48)=phase<<10;
            clone_fixture(&expected,&actual);
            alist_resample(&actual.hle,false,false,out,in,2,pitch,0x40);
            resample_reference(&expected.hle,false,out,in,2,pitch,0x40);
            if (!fixture_equal(&actual,&expected)) failures++;
            cases++;
        }
    }
    printf("[PS2 RESAMPLE KERNEL TEST] cases=%u init/continue, 64 phases, pitch wrap, aliases, boundaries failures=%u\n",cases,failures);
#ifdef PS2_RENDERER_SMOKE_CAPTURE
    printf("[PS2 RESAMPLE KERNEL PATHS] direct=%u masked=%u\n",
        ps2_resample_direct_runs-direct_start,ps2_resample_masked_runs-masked_start);
    if (ps2_resample_direct_runs==direct_start || ps2_resample_masked_runs==masked_start) failures++;
#endif
    return failures;
}

static unsigned filter_fixtures(void) {
    const uint16_t counts[]={0,16,32,48,64,128,256,1920};
    unsigned failures=0,cases=0;
    for (unsigned fixture=0;fixture<64;fixture++) {
        for (unsigned c=0;c<sizeof(counts)/sizeof(counts[0]);c++) {
            for (unsigned alias=0;alias<10;alias++) {
                uint32_t state=0x40,lut[2]={0x100,0x120};
                uint16_t dmem=0x600;
                fill_fixture(&actual,fixture);
                if (alias==1) lut[1]=lut[0];
                if (alias==2) lut[1]=lut[0]+2;
                if (alias==3) lut[1]=lut[0]-2;
                if (alias==4) state=lut[0];
                if (alias==5) { lut[0]=state+2; lut[1]=state+16; }
                if (alias>=6) {
                    actual.hle.dram=actual.hle.alist_buffer;
                    state=0x100; lut[0]=0x200; lut[1]=0x240;
                }
                if (alias==7) state=dmem;
                if (alias==8) { lut[0]=dmem; lut[1]=dmem+2; }
                if (alias==9) { state=dmem+counts[c]; lut[0]=state+16; lut[1]=state+32; }
                /* Include sums well above/below signed 32-bit range. */
                if (fixture<4 && alias==0) for (unsigned k=0;k<8;k++) {
                    int16_t value=fixture&1 ? -32768 : 32767;
                    *reference_ram(&actual.hle,lut[0]+k*2)=value;
                    *reference_ram(&actual.hle,lut[1]+k*2)=value;
                    *reference_ram(&actual.hle,state+k*2)=value;
                }
                clone_fixture(&expected,&actual);
                alist_filter(&actual.hle,dmem,counts[c],state,lut);
                filter_reference(&expected.hle,dmem,counts[c],state,lut);
                if (!fixture_equal(&actual,&expected)) {
                    if (!failures) printf("[PS2 FILTER KERNEL TEST] first mismatch fixture=%u count=%u alias=%u\n",
                        fixture,counts[c],alias);
                    failures++;
                }
                cases++;
            }
        }
    }
    /* The output may finish exactly at the end of the synthesis buffer. */
    fill_fixture(&actual,8); clone_fixture(&expected,&actual);
    const uint32_t lut[2]={0x100,0x120};
    alist_filter(&actual.hle,4080,16,0x40,lut);
    filter_reference(&expected.hle,4080,16,0x40,lut);
    if (!fixture_equal(&actual,&expected)) failures++;
    printf("[PS2 FILTER KERNEL TEST] cases=%u fullscale/wrapping, LUT/history/input aliases, max count and buffer end failures=%u\n",
        cases+1,failures);
    return failures;
}

static unsigned kernel_continuity(void) {
    unsigned failures=0;
    int16_t full[64],parts[64],raw[64];
    const uint32_t lut[2]={0x100,0x120};
    fill_fixture(&actual,9);
    for (unsigned k=0;k<8;k++)
        *reference_ram(&actual.hle,lut[1]+k*2)=*reference_ram(&actual.hle,lut[0]+k*2);
    clone_fixture(&expected,&actual);
    memcpy(raw,actual.hle.alist_buffer+0x600,sizeof(raw));
    alist_filter(&actual.hle,0x600,128,0x40,lut);
    memcpy(full,actual.hle.alist_buffer+0x600,sizeof(full));
    alist_filter(&expected.hle,0x600,64,0x40,lut);
    memcpy(parts,expected.hle.alist_buffer+0x600,64);
    memcpy(expected.hle.alist_buffer+0x600,raw+32,64);
    alist_filter(&expected.hle,0x600,64,0x40,lut);
    memcpy(parts+32,expected.hle.alist_buffer+0x600,64);
    if (memcmp(full,parts,sizeof(full)) || memcmp(actual.ram,expected.ram,sizeof(actual.ram))) failures++;

    /* Splitting the resampler must retain both four-sample history and phase.
     * At pitch .75, 32 output samples consume exactly 24 input samples. */
    fill_fixture(&actual,10); clone_fixture(&expected,&actual);
    alist_resample(&actual.hle,true,false,0xc00,0x408,128,0xc000,0x40);
    alist_resample(&expected.hle,true,false,0xc00,0x408,64,0xc000,0x40);
    alist_resample(&expected.hle,false,false,0xc40,0x438,64,0xc000,0x40);
    if (!fixture_equal(&actual,&expected)) failures++;
    printf("[PS2 RSP KERNEL CONTINUITY] filter raw-history and fractional resample split failures=%u\n",failures);
    return failures;
}

static unsigned kernel_benchmarks(void) {
    unsigned failures=0;
    uint32_t before,old_cycles,new_cycles;
    fill_fixture(&actual,8); clone_fixture(&expected,&actual);
    before=kernel_cycles();
    for (unsigned i=0;i<1000;i++)
        resample_reference(&expected.hle,i==0,0xc00,0x408,512,0xc000,0x40);
    old_cycles=kernel_cycles()-before;
    before=kernel_cycles();
    for (unsigned i=0;i<1000;i++)
        alist_resample(&actual.hle,i==0,false,0xc00,0x408,512,0xc000,0x40);
    new_cycles=kernel_cycles()-before;
    if (!fixture_equal(&actual,&expected)) failures++;
    printf("[PS2 RESAMPLE KERNEL BENCH] oldCycles=%u newCycles=%u failures=%u\n",old_cycles,new_cycles,failures);

    fill_fixture(&actual,9); clone_fixture(&expected,&actual);
    const uint32_t lut[2]={0x100,0x120};
    before=kernel_cycles();
    for (unsigned i=0;i<1000;i++) filter_reference(&expected.hle,0x600,512,0x40,lut);
    old_cycles=kernel_cycles()-before;
    before=kernel_cycles();
    for (unsigned i=0;i<1000;i++) alist_filter(&actual.hle,0x600,512,0x40,lut);
    new_cycles=kernel_cycles()-before;
    if (!fixture_equal(&actual,&expected)) failures++;
    printf("[PS2 FILTER KERNEL BENCH] oldCycles=%u newCycles=%u failures=%u\n",old_cycles,new_cycles,failures);
    return failures;
}

int ps2_rsp_kernel_smoke_run(void) {
    unsigned failures=resample_fixtures();
    failures+=filter_fixtures();
    failures+=kernel_continuity();
    failures+=kernel_benchmarks();
    printf("[PS2 RSP KERNEL TEST] %s failures=%u\n",failures ? "FAIL" : "PASS",failures);
    return failures;
}
