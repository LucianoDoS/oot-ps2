#include <stdio.h>
#include <string.h>
#include "ps2_texture_content.h"
#ifdef _EE
#include "ps2_gfx_profile.h"
#endif
extern u32 ps2_texture_hash(const u8*, u32, u32);

static u8 source[2][32784] __attribute__((aligned(64)));
static u8 copy[32784] __attribute__((aligned(64)));
static u8 evictions[96][2048] __attribute__((aligned(64)));

/* The original renderer chained the second effect per row. Keep this
 * independent oracle rather than comparing two calls to the cache. */
static u32 reference_hash(const PS2TextureSourceSpan* spans, unsigned count) {
    u32 hash = 0;
    for (unsigned i = 0; i < count; i++) {
        const PS2TextureSourceSpan* span = &spans[i];
        unsigned stride = span->stride ? span->stride : span->rowBytes;
        if (!span->hashRows && stride == span->rowBytes)
            hash = ps2_texture_hash(span->source, span->rowBytes * span->rows, hash);
        else for (unsigned row = 0; row < span->rows; row++)
            hash = ps2_texture_hash(span->source + row * stride, span->rowBytes, hash);
    }
    return hash;
}
static unsigned check_hash(const PS2TextureSourceSpan* spans, unsigned count) {
    return reference_hash(spans, count) != ps2_texture_content_hash(spans, count);
}

int ps2_texture_content_smoke(void) {
    unsigned failures = 0, comparisons = 0, hashes = 0;
    ps2_texture_content_test_enable(true);
    ps2_texture_content_test_reset();
    for (unsigned i = 0; i < sizeof(source[0]); i++) {
        source[0][i] = (i * 73 + (i >> 7) * 19 + 159) & 255;
        source[1][i] = (i * 31 + (i >> 9) * 37 + 93) & 255;
    }
    /* Every byte position, all source alignments, all stripe tails. In
     * particular upper 64-bit differences must not disappear in the fold. */
    for (unsigned offset = 0; offset < 16; offset++) {
        for (unsigned bytes = 0; bytes <= 159; bytes++) {
            memcpy(copy, source[0] + offset, bytes);
            failures += !ps2_texture_content_test_equal(source[0] + offset, copy, bytes);
            comparisons++;
            for (unsigned position = 0; position < bytes; position++) {
                copy[position] ^= 0x81;
                failures += ps2_texture_content_test_equal(source[0] + offset, copy, bytes);
                copy[position] ^= 0x81; comparisons++;
            }
            if (bytes >= 64) {
                for (unsigned position = 0; position < 16; position++) {
                    copy[position] ^= 0x81; copy[position + 16] ^= 0x81;
                    failures += ps2_texture_content_test_equal(source[0] + offset, copy, bytes);
                    copy[position] ^= 0x81; copy[position + 16] ^= 0x81; comparisons++;
                }
            }
            /* Unaligned saved copies exercise the scalar fallback. */
            memcpy(copy + 1, source[0] + offset, bytes);
            failures += !ps2_texture_content_test_equal(source[0] + offset, copy + 1, bytes);
            comparisons++;
        }
    }
    for (unsigned offset = 0; offset < 16; offset++) {
        for (unsigned width = 13; width <= 65; width += 13) {
            for (unsigned paired = 0; paired < 2; paired++) {
                for (unsigned rowsHashed = 0; rowsHashed < 2; rowsHashed++) {
                    PS2TextureSourceSpan spans[2] = {
                        {source[0] + offset, width, width + 19, 23, rowsHashed},
                        {source[1] + 15 - offset, 32, 40, 32, true}
                    };
                    ps2_texture_content_test_reset();
                    failures += check_hash(spans, 1 + paired); hashes++;
                    failures += check_hash(spans, 1 + paired); hashes++;
                    if (ps2_texture_content_stats(false).hits != 1) failures++;
                    /* Row padding is not loaded and must not invalidate. */
                    source[0][offset + width] ^= 0x80;
                    failures += check_hash(spans, 1 + paired); hashes++;
                    if (ps2_texture_content_stats(false).hits != 2) failures++;
                    source[0][offset + width] ^= 0x80;
                    /* The last texel of every row, including source two. */
                    for (unsigned input = 0; input <= paired; input++) {
                        PS2TextureSourceSpan* span = &spans[input];
                        for (unsigned row = 0; row < span->rows; row++) {
                            unsigned at = row * span->stride + span->rowBytes - 1;
                            u8* mutable = (u8*)span->source;
                            mutable[at] ^= 0x5b;
                            failures += check_hash(spans, 1 + paired); hashes++;
                            failures += check_hash(spans, 1 + paired); hashes++;
                            mutable[at] ^= 0x5b;
                            failures += check_hash(spans, 1 + paired); hashes++;
                        }
                    }
                }
            }
        }
    }
    /* Descriptor replacement, pool wrap, RAM reuse and same-pointer shape
     * changes. More live sources than the 64 descriptors and 128 KiB pool. */
    ps2_texture_content_test_reset();
    for (unsigned i = 0; i < 96; i++) for (unsigned j = 0; j < 2048; j++)
        evictions[i][j] = (i * 37 + j * 71 + (j >> 8)) & 255;
    for (unsigned pass = 0; pass < 5; pass++) for (unsigned i = 0; i < 96; i++) {
        unsigned index = pass & 1 ? 95 - i : i;
        PS2TextureSourceSpan span = {evictions[index], 64, 64, 32, false};
        failures += check_hash(&span, 1); hashes++;
        failures += check_hash(&span, 1); hashes++;
        evictions[index][(i * 17 + pass * 31) & 2047] ^= 0x80;
        failures += check_hash(&span, 1); hashes++;
        span.hashRows = true;
        failures += check_hash(&span, 1); hashes++;
        span.rowBytes = 32; span.rows = 32; span.stride = 0;
        failures += check_hash(&span, 1); hashes++;
    }
    const unsigned sizes[] = {0, 1, 15, 255, 256, 16384, 16385, 32768};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        PS2TextureSourceSpan span = {source[0], sizes[i], 0, 1, false};
        failures += check_hash(&span, 1); hashes++;
        failures += check_hash(&span, 1); hashes++;
    }
    printf("[PS2 TEXTURE CONTENT TEST] comparisons=%u hashes=%u alignment, every byte, strided/pair mutation, padding, shape and bounded evictions failures=%u\n",
        comparisons, hashes, failures);
#ifdef _EE
    const unsigned lengths[] = {256, 2048, 8192};
    for (unsigned offset = 0; offset <= 3; offset += 3) {
        for (unsigned size = 0; size < 3; size++) {
            PS2TextureSourceSpan span = {source[0] + offset, lengths[size], 0, 1, false};
            u32 checksum[2] = {0}, cycles[2];
            for (unsigned cached = 0; cached < 2; cached++) {
                ps2_texture_content_test_reset();
                ps2_texture_content_test_enable(cached);
                ps2_texture_content_hash(&span, 1); /* Include warm bindings only. */
                u32 start = ps2_gfx_cycles();
                for (unsigned repeat = 0; repeat < 1000; repeat++)
                    checksum[cached] += ps2_texture_content_hash(&span, 1);
                cycles[cached] = ps2_gfx_cycles() - start;
            }
            failures += checksum[0] != checksum[1];
            printf("[PS2 TEXTURE CONTENT BENCH] offset=%u bytes=%u hashCycles=%u exactCycles=%u checksum=%08x failures=%u\n",
                offset, lengths[size], cycles[0], cycles[1], checksum[1], failures);
        }
    }
#endif
    ps2_texture_content_test_enable(true);
    ps2_texture_content_test_reset();
    return failures ? 1 : 0;
}

#ifdef PS2_TEXTURE_CONTENT_HOST_TEST
int main(void) { return ps2_texture_content_smoke(); }
#endif
