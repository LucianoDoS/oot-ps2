/* OoT PS2: explicit occupancy prevents replay of stale PCM on underrun.
 * Callers serialize access; this header is also exercised by EE diagnostics. */
#ifndef OOT_PCM_RING_H
#define OOT_PCM_RING_H
#include <string.h>
#define PCM_RING_CAPACITY 20480u
typedef struct {
    unsigned size, read, write, used;
    unsigned char bytes[PCM_RING_CAPACITY];
} PcmRing;
static void pcm_ring_reset(PcmRing* ring, unsigned size) {
    ring->size = size;
    ring->read = ring->write = ring->used = 0;
}
static unsigned pcm_ring_write(PcmRing* ring, const void* input, unsigned count) {
    const unsigned char* source = input;
    if (count > ring->size - ring->used) count = ring->size - ring->used;
    unsigned first = count;
    if (first > ring->size - ring->write) first = ring->size - ring->write;
    memcpy(ring->bytes + ring->write, source, first);
    memcpy(ring->bytes, source + first, count - first);
    ring->write = (ring->write + count) % ring->size;
    ring->used += count;
    return count;
}
static unsigned pcm_ring_read(PcmRing* ring, void* output, unsigned count, int silence) {
    unsigned char* destination = output;
    unsigned copied = count > ring->used ? ring->used : count;
    unsigned first = copied;
    if (first > ring->size - ring->read) first = ring->size - ring->read;
    memcpy(destination, ring->bytes + ring->read, first);
    memcpy(destination + first, ring->bytes, copied - first);
    memset(destination + copied, silence, count - copied);
    ring->read = (ring->read + copied) % ring->size;
    ring->used -= copied;
    return copied;
}
#endif
