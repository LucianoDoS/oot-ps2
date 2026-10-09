#include <stdio.h>
#include <string.h>
#include "ps2_texture_content.h"
#ifdef _EE
#include "ps2_gfx_profile.h"
#endif
extern u32 ps2_texture_hash(const u8*, u32, u32);
extern unsigned ps2_texture_content_test_slots(void);
extern void ps2_texture_content_test_set_slots(unsigned slots);

static u8 source[2][32784] __attribute__((aligned(64)));
static u8 copy[32784] __attribute__((aligned(64)));
static u8 evictions[129][2048] __attribute__((aligned(64)));
#ifdef _EE
static u8 compareCopies[96][512] __attribute__((aligned(64)));
#endif

/* Volatile byte reads keep this independent oracle bounded to the exact
 * requested interval; it cannot be folded into the MMI comparator. */
static __attribute__((noinline)) bool reference_equal(const u8* source, const u8* saved, unsigned bytes) {
    const volatile u8* input = source;
    const volatile u8* copy = saved;
    for (unsigned i = 0; i < bytes; i++) if (input[i] != copy[i]) return false;
    return true;
}
static unsigned check_equal(const u8* input, const u8* saved, unsigned bytes, unsigned* cases) {
    (*cases)++;
    return reference_equal(input, saved, bytes) != ps2_texture_content_test_equal(input, saved, bytes);
}

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
static unsigned compare_matrix_smoke(void) {
    const unsigned lengths[] = {0, 1, 7, 8, 15, 16, 17, 31, 32, 63, 64, 65,
        127, 128, 129, 143, 144, 145, 191, 192, 193, 255, 256, 257,
        271, 272, 273, 383, 384, 385, 511, 512, 513};
    const unsigned widths[] = {63, 64, 65, 127, 128, 129, 255, 256, 257};
    const unsigned fallbackLengths[] = {63, 127, 255, 513};
    unsigned failures = 0, cases = 0, hashCases = 0;
    for (unsigned block = 64; block <= 256; block *= 2) {
        ps2_texture_content_test_set_compare_bytes(block);
        failures += ps2_texture_content_test_compare_bytes() != block;
        for (unsigned offset = 0; offset < 16; offset++) {
            const u8* input = source[0] + 32 + offset;
            u8* saved = copy + 32;
            for (unsigned size = 0; size < sizeof(lengths) / sizeof(lengths[0]); size++) {
                unsigned bytes = lengths[size];
                memcpy(saved, input, bytes);
                /* Excluded sentinels catch accidentally compared prefix/tail
                 * bytes. They do not prove unused loads absent: inspect the
                 * EE load offsets and whole-block guards after compilation. */
                saved[-1] = input[-1] ^ 0xff;
                saved[bytes] = input[bytes] ^ 0xff;
                failures += check_equal(input, saved, bytes, &cases);
                for (unsigned position = 0; position < bytes; position++) {
                    saved[position] ^= 0x81;
                    failures += check_equal(input, saved, bytes, &cases);
                    saved[position] ^= 0x81;
                }
                if (bytes >= block) {
                    const unsigned distances[] = {16, block / 2, block - 16};
                    for (unsigned distance = 0; distance < 3; distance++) {
                        for (unsigned position = 0; position < 16; position++) {
                            saved[position] ^= 0x81;
                            saved[position + distances[distance]] ^= 0x81;
                            failures += check_equal(input, saved, bytes, &cases);
                            saved[position] ^= 0x81;
                            saved[position + distances[distance]] ^= 0x81;
                        }
                    }
                }
            }
            /* Every unaligned saved-copy offset must retain scalar fallback. */
            for (unsigned savedOffset = 1; savedOffset < 16; savedOffset++) {
                saved = copy + 32 + savedOffset;
                for (unsigned size = 0; size < sizeof(fallbackLengths) / sizeof(fallbackLengths[0]); size++) {
                    unsigned bytes = fallbackLengths[size];
                    memcpy(saved, input, bytes);
                    saved[-1] = input[-1] ^ 0xff;
                    saved[bytes] = input[bytes] ^ 0xff;
                    failures += check_equal(input, saved, bytes, &cases);
                    saved[0] ^= 0x81;
                    failures += check_equal(input, saved, bytes, &cases);
                    saved[0] ^= 0x81;
                    saved[bytes - 1] ^= 0x81;
                    failures += check_equal(input, saved, bytes, &cases);
                    saved[bytes - 1] ^= 0x81;
                }
            }
            for (unsigned size = 0; size < sizeof(widths) / sizeof(widths[0]); size++) {
                unsigned width = widths[size];
                PS2TextureSourceSpan spans[2] = {
                    {source[0] + 8192 + offset, width, width + 19, 5, false},
                    {source[1] + 8192 + 15 - offset, width + 3, width + 23, 5, true}
                };
                ps2_texture_content_test_reset();
                failures += check_hash(spans, 2); hashCases++;
                failures += check_hash(spans, 2); hashCases++;
                u8* padding = (u8*)spans[0].source;
                padding[width] ^= 0x80;
                failures += check_hash(spans, 2); hashCases++;
                padding[width] ^= 0x80;
                for (unsigned inputIndex = 0; inputIndex < 2; inputIndex++) {
                    PS2TextureSourceSpan* span = &spans[inputIndex];
                    u8* mutable = (u8*)span->source;
                    for (unsigned row = 0; row < span->rows; row++) {
                        unsigned at = row * span->stride + span->rowBytes - 1;
                        mutable[at] ^= 0x5b;
                        failures += check_hash(spans, 2); hashCases++;
                        failures += check_hash(spans, 2); hashCases++;
                        mutable[at] ^= 0x5b;
                        failures += check_hash(spans, 2); hashCases++;
                    }
                }
                PS2TextureContentStats stats = ps2_texture_content_stats(false);
                failures += stats.hits != 12 || stats.hashes != 21 || stats.mutations != 20;
                failures += stats.newSnapshots != 1 || stats.descriptorEvictions != 0 || stats.overlapEvictions != 0;
            }
        }
    }
    ps2_texture_content_test_set_compare_bytes(256);
    ps2_texture_content_test_reset();
    printf("[PS2 TEXTURE COMPARE TEST] blocks=64,128,256 alignments=16 savedAlignments=16 cases=%u hashCases=%u excludedSentinels=1 failures=%u\n",
        cases, hashCases, failures);
    return failures;
}
#ifdef _EE
static unsigned compare_bench_smoke(void) {
    const unsigned lengths[] = {256, 2048, 8192, 16384};
    const unsigned offsets[] = {0, 3, 4, 8};
    unsigned failures = 0;
    for (unsigned offsetIndex = 0; offsetIndex < 4; offsetIndex++) {
        unsigned offset = offsets[offsetIndex];
        for (unsigned size = 0; size < 4; size++) {
            unsigned bytes = lengths[size];
            const u8* input = source[0] + offset;
            memcpy(copy, input, bytes);
            failures += !reference_equal(input, copy, bytes);
            for (unsigned block = 64; block <= 256; block *= 2) {
                ps2_texture_content_test_set_compare_bytes(block);
                PS2TextureContentCompare compare = ps2_texture_content_test_comparator();
                failures += !compare(input, copy, bytes);
                u32 checksum = 0, start = ps2_gfx_cycles();
                for (unsigned repeat = 0; repeat < 1000; repeat++) checksum += compare(input, copy, bytes);
                u32 cycles = ps2_gfx_cycles() - start;
                failures += checksum != 1000;
                printf("[PS2 TEXTURE COMPARE BENCH] set=single block=%u offset=%u bytes=%u calls=1000 cycles=%u checksum=%u failures=%u\n",
                    block, offset, bytes, cycles, checksum, failures);
            }
        }
        /* A 48 KiB saved-copy working set plus 96 RAM windows exercises
         * repeated streams beyond a single small warm comparison buffer. */
        for (unsigned i = 0; i < 96; i++) {
            memcpy(compareCopies[i], evictions[i] + offset, 512);
            failures += !reference_equal(evictions[i] + offset, compareCopies[i], 512);
        }
        for (unsigned block = 64; block <= 256; block *= 2) {
            ps2_texture_content_test_set_compare_bytes(block);
            PS2TextureContentCompare compare = ps2_texture_content_test_comparator();
            for (unsigned i = 0; i < 96; i++) failures += !compare(evictions[i] + offset, compareCopies[i], 512);
            u32 checksum = 0, start = ps2_gfx_cycles();
            for (unsigned repeat = 0; repeat < 100; repeat++) for (unsigned i = 0; i < 96; i++)
                checksum += compare(evictions[i] + offset, compareCopies[i], 512);
            u32 cycles = ps2_gfx_cycles() - start;
            failures += checksum != 9600;
            printf("[PS2 TEXTURE COMPARE BENCH] set=stream block=%u offset=%u bytes=512 copies=96 copyBytes=49152 calls=9600 cycles=%u checksum=%u failures=%u\n",
                block, offset, cycles, checksum, failures);
        }
    }
    ps2_texture_content_test_set_compare_bytes(256);
    return failures;
}
#endif
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
    unsigned entries = ps2_texture_content_test_slots() + 1;
    /* Capacity plus one small sources need one descriptor eviction; copies
     * do not fill the ring. Nine 16 KiB windows fill it with spare slots. */
    ps2_texture_content_test_reset();
    for (unsigned i = 0; i < entries; i++) {
        PS2TextureSourceSpan span = {evictions[i], 256, 0, 1, false};
        failures += check_hash(&span, 1); (*hashes)++;
    }
    PS2TextureContentStats stats = ps2_texture_content_stats(false);
    failures += stats.newSnapshots != entries || stats.hashes != entries;
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
static unsigned descriptor_capacity_smoke(unsigned* hashes) {
    unsigned failures = 0;
    /* 96 source copies need 48 KiB, below the unchanged 128 KiB byte pool.
     * This isolates descriptor pressure from ring capacity and mutations. */
    ps2_texture_content_test_reset();
    for (unsigned i = 0; i < 96; i++) {
        PS2TextureSourceSpan span = {evictions[i], 512, 0, 1, false};
        failures += check_hash(&span, 1); (*hashes)++;
    }
    PS2TextureContentStats stats = ps2_texture_content_stats(true);
    failures += stats.hashes != 96 || stats.newSnapshots != 96;
    failures += stats.descriptorEvictions != 0 || stats.overlapEvictions != 0;
    for (unsigned pass = 0; pass < 4; pass++) for (unsigned i = 0; i < 96; i++) {
        PS2TextureSourceSpan span = {evictions[i], 512, 0, 1, false};
        failures += check_hash(&span, 1); (*hashes)++;
    }
    stats = ps2_texture_content_stats(false);
    failures += stats.hits != 384 || stats.hashes != 0 || stats.newSnapshots != 0;
    failures += stats.descriptorEvictions != 0 || stats.overlapEvictions != 0;
    for (unsigned i = 0; i < 96; i++) {
        PS2TextureSourceSpan span = {evictions[i], 512, 0, 1, false};
        evictions[i][511] ^= 0x81;
        failures += check_hash(&span, 1); (*hashes)++;
        failures += check_hash(&span, 1); (*hashes)++;
    }
    stats = ps2_texture_content_stats(false);
    failures += stats.hits != 480 || stats.hashes != 96 || stats.mutations != 96;
    failures += stats.hashedBytes != 96u * 512u || stats.newSnapshots != 0;
    failures += stats.descriptorEvictions != 0 || stats.overlapEvictions != 0;
    printf("[PS2 TEXTURE DESCRIPTORS TEST] sources=96 copyBytes=49152 warmHits=384 mutations=96 failures=%u\n", failures);
    return failures;
}

int ps2_texture_content_smoke(void) {
    unsigned failures = 0, comparisons = 0, hashes = 0;
    ps2_texture_content_test_set_compare_bytes(256);
    ps2_texture_content_test_set_slots(128);
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
     * changes pressure the 128 descriptors and unchanged 128 KiB pool. */
    ps2_texture_content_test_reset();
    for (unsigned i = 0; i < 129; i++) for (unsigned j = 0; j < 2048; j++)
        evictions[i][j] = (i * 37 + j * 71 + (j >> 8)) & 255;
    failures += descriptor_capacity_smoke(&hashes);
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
    failures += compare_matrix_smoke();
    printf("[PS2 TEXTURE CONTENT TEST] comparisons=%u hashes=%u alignment, every byte, strided/pair mutation, in-place reuse, padding, shape and bounded evictions failures=%u\n",
        comparisons, hashes, failures);
#ifdef _EE
    failures += compare_bench_smoke();
    u32 referenceChecksum = 0;
    for (unsigned i = 0; i < 96; i++) {
        PS2TextureSourceSpan span = {evictions[i], 512, 0, 1, false};
        referenceChecksum += reference_hash(&span, 1) * 100u;
    }
    for (unsigned slots = 64; slots <= 128; slots *= 2) {
        ps2_texture_content_test_set_slots(slots);
        ps2_texture_content_test_reset();
        ps2_texture_content_test_enable(true);
        for (unsigned i = 0; i < 96; i++) {
            PS2TextureSourceSpan span = {evictions[i], 512, 0, 1, false};
            failures += check_hash(&span, 1);
        }
        ps2_texture_content_stats(true);
        u32 checksum = 0, start = ps2_gfx_cycles();
        for (unsigned repeat = 0; repeat < 100; repeat++) for (unsigned i = 0; i < 96; i++) {
            PS2TextureSourceSpan span = {evictions[i], 512, 0, 1, false};
            checksum += ps2_texture_content_hash(&span, 1);
        }
        u32 cycles = ps2_gfx_cycles() - start;
        PS2TextureContentStats stats = ps2_texture_content_stats(false);
        failures += checksum != referenceChecksum;
        failures += stats.hits != (slots == 128 ? 9600u : 0u);
        failures += stats.hashes != (slots == 128 ? 0u : 9600u);
        printf("[PS2 TEXTURE DESCRIPTORS BENCH] slots=%u sources=96 copyBytes=49152 calls=9600 cycles=%u exactHits=%u hashes=%u descriptorEvictions=%u overlapEvictions=%u checksum=%08x failures=%u\n",
            slots, cycles, stats.hits, stats.hashes, stats.descriptorEvictions,
            stats.overlapEvictions, checksum, failures);
    }
    ps2_texture_content_test_set_slots(128);
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
    ps2_texture_content_test_set_compare_bytes(256);
    ps2_texture_content_test_reset();
    return failures ? 1 : 0;
}

#ifdef PS2_TEXTURE_CONTENT_HOST_TEST
int main(void) { return ps2_texture_content_smoke(); }
#endif
