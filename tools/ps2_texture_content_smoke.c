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
static unsigned mutation_reuse_smoke(unsigned* hashes) {
    unsigned failures = 0;
    /* Two live 4 KiB snapshots fit easily. Appending a new copy for every
     * mutation would wrap the 128 KiB ring and evict the frequently used A. */
    for (unsigned paired = 0; paired < 2; paired++) {
        PS2TextureSourceSpan stable = {source[0], 4096, 0, 1, false};
        PS2TextureSourceSpan changing[2] = {
            {source[1], 4096, 0, 1, false}, {0}
        };
        if (paired) {
            changing[0] = (PS2TextureSourceSpan){source[0] + 8192, 64, 80, 32, false};
            changing[1] = (PS2TextureSourceSpan){source[1] + 8192, 64, 80, 32, true};
        }
        ps2_texture_content_test_reset();
        failures += check_hash(&stable, 1); (*hashes)++;
        failures += check_hash(changing, 1 + paired); (*hashes)++;
        for (unsigned repeat = 0; repeat < 100; repeat++) {
            PS2TextureSourceSpan* span = &changing[paired ? repeat & 1 : 0];
            unsigned stride = span->stride ? span->stride : span->rowBytes;
            unsigned row = repeat % span->rows;
            u8* mutable = (u8*)span->source;
            mutable[row * stride + span->rowBytes - 1] ^= 0x5b;
            failures += check_hash(changing, 1 + paired); (*hashes)++;
            failures += check_hash(changing, 1 + paired); (*hashes)++;
            if (paired) for (unsigned input = 0; input < 2; input++) {
                const PS2TextureSourceSpan* padded = &changing[input];
                unsigned at = row * padded->stride + padded->rowBytes;
                u8* padding = (u8*)padded->source;
                padding[at] ^= 0x80;
                failures += check_hash(changing, 2); (*hashes)++;
                padding[at] ^= 0x80;
            }
            failures += check_hash(&stable, 1); (*hashes)++;
        }
        PS2TextureContentStats stats = ps2_texture_content_stats(false);
        failures += stats.hits != (paired ? 400u : 200u);
        failures += stats.hashes != 102 || stats.hashedBytes != 102u * 4096u;
        failures += stats.mutations != 100 || stats.newSnapshots != 2;
        failures += stats.descriptorEvictions != 0 || stats.overlapEvictions != 0;
        failures += stats.smallBypass != 0 || stats.largeBypass != 0;
    }
    return failures;
}
static unsigned eviction_stats_smoke(unsigned* hashes) {
    unsigned failures = 0;
    /* 65 small sources need one descriptor eviction, but their copies do
     * not fill the ring. Nine 16 KiB windows fill the ring with spare slots. */
    ps2_texture_content_test_reset();
    for (unsigned i = 0; i < 65; i++) {
        PS2TextureSourceSpan span = {evictions[i], 256, 0, 1, false};
        failures += check_hash(&span, 1); (*hashes)++;
    }
    PS2TextureContentStats stats = ps2_texture_content_stats(false);
    failures += stats.newSnapshots != 65 || stats.hashes != 65;
    failures += stats.descriptorEvictions != 1 || stats.overlapEvictions != 0;
    ps2_texture_content_test_reset();
    for (unsigned i = 0; i < 9; i++) {
        PS2TextureSourceSpan span = {source[0] + i, 16384, 0, 1, false};
        failures += check_hash(&span, 1); (*hashes)++;
    }
    stats = ps2_texture_content_stats(false);
    failures += stats.newSnapshots != 9 || stats.hashes != 9;
    failures += stats.descriptorEvictions != 0 || stats.overlapEvictions != 1;
    return failures;
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
    failures += mutation_reuse_smoke(&hashes);
    /* Descriptor replacement, pool wrap, RAM reuse and same-pointer shape
     * changes. More live sources than the 64 descriptors and 128 KiB pool. */
    ps2_texture_content_test_reset();
    for (unsigned i = 0; i < 96; i++) for (unsigned j = 0; j < 2048; j++)
        evictions[i][j] = (i * 37 + j * 71 + (j >> 8)) & 255;
    failures += eviction_stats_smoke(&hashes);
    ps2_texture_content_test_reset();
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
    ps2_texture_content_test_reset();
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        PS2TextureSourceSpan span = {source[0], sizes[i], 0, 1, false};
        failures += check_hash(&span, 1); hashes++;
        failures += check_hash(&span, 1); hashes++;
    }
    PS2TextureContentStats limits = ps2_texture_content_stats(false);
    failures += limits.smallBypass != 8 || limits.largeBypass != 4;
    failures += limits.newSnapshots != 2 || limits.hits != 2 || limits.hashes != 14;
    printf("[PS2 TEXTURE CONTENT TEST] comparisons=%u hashes=%u alignment, every byte, strided/pair mutation, in-place reuse, padding, shape and bounded evictions failures=%u\n",
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
