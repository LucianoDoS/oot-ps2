/* SDK-only capture writer, invoked on the render worker after GS readback. */
#include <stdio.h>
#include "rdp_to_gs.h"
#include "ps2_offscreen.h"

static unsigned char s_image[320 * 240 * 2] __attribute__((aligned(64)));
PS2RenderTarget gRoomCaptureTarget = {s_image, 320, 240};
volatile unsigned gRoomCaptureDone;
unsigned gRoomCaptureCase;
extern void __real_ps2_capture_frame(RDPState*, const void*);

void __wrap_ps2_capture_frame(RDPState* state, const void* descriptor) {
    __real_ps2_capture_frame(state, descriptor);
    if (descriptor != &gRoomCaptureTarget) return;
    char path[80];
    snprintf(path, sizeof(path), "host:room-capture-%04u.ppm", gRoomCaptureCase);
    FILE* out = fopen(path, "wb");
    int ok = out && fprintf(out, "P6\n320 240\n255\n") > 0;
    unsigned char row[320 * 3];
    for (unsigned y = 0; y < 240 && ok; y++) {
        for (unsigned x = 0; x < 320; x++) {
            unsigned at = 2 * (y * 320 + x);
            unsigned pixel = (s_image[at] << 8) | s_image[at + 1];
            row[x * 3] = ((pixel >> 11) & 31) * 255 / 31;
            row[x * 3 + 1] = ((pixel >> 6) & 31) * 255 / 31;
            row[x * 3 + 2] = ((pixel >> 1) & 31) * 255 / 31;
        }
        ok = fwrite(row, 1, sizeof(row), out) == sizeof(row);
    }
    if (out && fclose(out)) ok = 0;
    printf("[PS2 ROOM CAPTURE] %s case=%u file=%s\n", ok ? "PASS" : "FAIL", gRoomCaptureCase, path);
    gRoomCaptureDone = ok ? 1 : 2;
}
