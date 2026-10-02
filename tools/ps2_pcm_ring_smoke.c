/* Exercise the exact IOP ring on EE: full, wrap, starvation and restart. */
#include <stdio.h>
#include <string.h>
#include "../src/platform/ps2/iop/audsrv/src/pcm_ring.h"
int ps2_pcm_ring_smoke(void) {
    static PcmRing ring;
    unsigned char input[64], output[64];
    int failures = 0;
    for (unsigned i = 0; i < sizeof(input); i++) input[i] = i + 1;
    pcm_ring_reset(&ring, 32);
    if (pcm_ring_write(&ring, input, 32) != 32 || ring.used != 32 || ring.write != ring.read) failures++;
    if (pcm_ring_write(&ring, input, 4)) failures++;
    if (pcm_ring_read(&ring, output, 20, 0) != 20 || memcmp(output,input,20)) failures++;
    if (pcm_ring_write(&ring, input+32, 20) != 20 || ring.used != 32) failures++;
    if (pcm_ring_read(&ring, output, 40, 0) != 32 || memcmp(output,input+20,32)) failures++;
    for (int i = 32; i < 40; i++) if (output[i]) failures++;
    for (int pass = 0; pass < 20; pass++) {
        memset(output, 0xaa, sizeof(output));
        if (pcm_ring_read(&ring, output, 32, 0) || ring.used || ring.read != ring.write) failures++;
        for (int i = 0; i < 32; i++) if (output[i]) failures++;
    }
    if (pcm_ring_write(&ring,input,12) != 12 || pcm_ring_read(&ring,output,32,0) != 12 || memcmp(output,input,12)) failures++;
    for (int i = 12; i < 32; i++) if (output[i]) failures++;
    pcm_ring_write(&ring,input,32);
    pcm_ring_reset(&ring,16);
    if (pcm_ring_read(&ring,output,16,128) || ring.used) failures++;
    for (int i = 0; i < 16; i++) if (output[i] != 128) failures++;
    printf("[PS2 PCM RING TEST] %s: full, wrap, underrun silence and reset (%d)\n", failures ? "FAIL" : "PASS", failures);
    return failures;
}
