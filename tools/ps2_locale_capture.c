/* Test-only SDK readback writer and measured logo presentation cadence. */
#include <stdio.h>
#include "rdp_to_gs.h"
#include "ps2_offscreen.h"
extern u64 osGetTime(void);
extern void __real_rdp_to_gs_present(void);
extern void __real_ps2_capture_frame(RDPState*,const void*);
static unsigned char sPixels[320*240*2] __attribute__((aligned(64)));
PS2RenderTarget gLocaleCaptureTarget={sPixels,320,240};
volatile unsigned gLocaleCaptureDone,gLocaleTestPhase;
unsigned gLocaleCaptureCase;
static unsigned sLogoFrames,sLogoIntervals;
static u64 sLogoStart,sLogoEnd;

void __wrap_rdp_to_gs_present(void) {
    __real_rdp_to_gs_present();
    if (gLocaleTestPhase==0) {
        sLogoFrames++;
        if (sLogoFrames==30) sLogoStart=osGetTime();
        else if (sLogoFrames>30) { sLogoIntervals++; sLogoEnd=osGetTime(); }
    }
}
void ps2_locale_logo_report(void) {
    unsigned fps10=sLogoEnd>sLogoStart ? sLogoIntervals*468750000ull/(sLogoEnd-sLogoStart):0;
    printf("[PS2 LOGO CADENCE] %s presented=%u intervals=%u fps10=%u warmup=30\n",
        fps10>=580 && fps10<=610?"PASS":"FAIL",sLogoFrames,sLogoIntervals,fps10);
}
void __wrap_ps2_capture_frame(RDPState* state,const void* descriptor) {
    __real_ps2_capture_frame(state,descriptor);
    if (descriptor!=&gLocaleCaptureTarget) return;
    char path[96];
    snprintf(path,sizeof(path),"mass:/OOTPS2/locale-capture-%u.ppm",gLocaleCaptureCase);
    FILE* out=fopen(path,"wb");
    int ok=out && fprintf(out,"P6\n320 240\n255\n")>0;
    unsigned char row[320*3];
    for (unsigned y=0;y<240 && ok;y++) {
        for (unsigned x=0;x<320;x++) {
            unsigned at=2*(y*320+x),pixel=sPixels[at]<<8|sPixels[at+1];
            row[x*3]=((pixel>>11)&31)*255/31;
            row[x*3+1]=((pixel>>6)&31)*255/31;
            row[x*3+2]=((pixel>>1)&31)*255/31;
        }
        ok=fwrite(row,1,sizeof(row),out)==sizeof(row);
    }
    if (out && fclose(out)) ok=0;
    printf("[PS2 LANGUAGE CAPTURE] %s case=%u\n",ok?"PASS":"FAIL",gLocaleCaptureCase);
    gLocaleCaptureDone=ok?1:2;
}
