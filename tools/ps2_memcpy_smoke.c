#include <stdio.h>
#include <string.h>
#include "ps2_gfx_profile.h"

extern void* __wrap_memcpy(void*,const void*,size_t);
extern void* __real_memcpy(void*,const void*,size_t);

int ps2_memcpy_smoke(void) {
    static unsigned char source[3008] __attribute__((aligned(16)));
    static unsigned char actual[3008] __attribute__((aligned(16)));
    static unsigned char expected[3008] __attribute__((aligned(16)));
    const unsigned large[]={127,128,129,143,144,145,240,512,2880};
    unsigned checks=0;
    for (unsigned i=0;i<sizeof(source);i++) source[i]=(i*73u+19u)&255u;
    for (unsigned s=0;s<16;s++) for (unsigned d=0;d<16;d++)
        for (unsigned fixture=0;fixture<66+sizeof(large)/sizeof(*large);fixture++) {
            unsigned bytes=fixture<66?fixture:large[fixture-66];
            memset(actual,0xa5,sizeof(actual)); memset(expected,0xa5,sizeof(expected));
            for (unsigned i=0;i<bytes;i++) expected[32+d+i]=source[32+s+i];
            void* result=__wrap_memcpy(actual+32+d,source+32+s,bytes);
            if (result!=actual+32+d || memcmp(actual,expected,sizeof(actual))) {
                printf("[PS2 MEMCPY TEST] FAIL source=%u destination=%u bytes=%u\n",s,d,bytes);
                return 1;
            }
            checks++;
        }
    if (__wrap_memcpy(NULL,NULL,0)!=NULL) return 1;
    printf("[PS2 MEMCPY TEST] PASS: %u copies, source/destination alignments 0-15, tails, return and guards\n",checks);
    for (unsigned fixture=0;fixture<3;fixture++) {
        unsigned bytes=fixture==2?2880:144, offset=fixture==1?8:0;
        u32 start=ps2_gfx_cycles();
        for (unsigned i=0;i<8192;i++) __real_memcpy(actual,source+offset,bytes);
        u32 oldCycles=ps2_gfx_cycles()-start;
        start=ps2_gfx_cycles();
        for (unsigned i=0;i<8192;i++) __wrap_memcpy(actual,source+offset,bytes);
        u32 newCycles=ps2_gfx_cycles()-start;
        if (memcmp(actual,source+offset,bytes)) return 1;
        printf("[PS2 MEMCPY BENCH] bytes=%u sourceOffset=%u sdkCycles=%u eeCycles=%u\n",
            bytes,offset,oldCycles,newCycles);
    }
    return 0;
}
