/* Exact source snapshots avoid multiplying every texel into XXH32 again
 * on unchanged bindings. Source RAM is still read on every binding, including
 * both inputs of a blend. GPU retirement and material keys are unchanged. */
#include <string.h>
#include "ps2_texture_content.h"

extern u32 ps2_texture_hash(const u8*, u32, u32);

#define SNAPSHOT_BYTES (128u * 1024u)
#define SNAPSHOT_MAX_BYTES (16u * 1024u)
#define SNAPSHOT_MIN_BYTES 256u
#define SNAPSHOT_SLOTS 128u
#define SNAPSHOT_BUCKETS 64u
#define SNAPSHOT_END 0xffffu

typedef struct {
    PS2TextureSourceSpan spans[2];
    u32 offset, bytes, hash, used;
    u16 next;
    u8 count, valid;
} Snapshot;
static u8 sCopies[SNAPSHOT_BYTES] __attribute__((aligned(64)));
static Snapshot sSnapshots[SNAPSHOT_SLOTS];
static u16 sHeads[SNAPSHOT_BUCKETS];
static u32 sCursor, sClock;
static bool sReady;
static PS2TextureContentStats sStats;

#ifdef PS2_RENDERER_SMOKE_CAPTURE
static bool sDisabled;
static unsigned sCompareBytes = 256;
void ps2_texture_content_test_set_compare_bytes(unsigned bytes) {
    if (bytes == 64 || bytes == 128 || bytes == 256) sCompareBytes = bytes;
}
unsigned ps2_texture_content_test_compare_bytes(void) { return sCompareBytes; }
static unsigned sSlotLimit = SNAPSHOT_SLOTS;
#define SNAPSHOT_ACTIVE_SLOTS sSlotLimit
unsigned ps2_texture_content_test_slots(void) { return sSlotLimit; }
/* Smoke callers reset after changing the limit before binding any source. */
void ps2_texture_content_test_set_slots(unsigned slots) {
    if (slots == 64u || slots == SNAPSHOT_SLOTS) sSlotLimit = slots;
}
void ps2_texture_content_test_enable(bool enabled) { sDisabled = !enabled; }
void ps2_texture_content_test_reset(void) {
    memset(sSnapshots, 0, sizeof(sSnapshots));
    memset(&sStats, 0, sizeof(sStats));
    sReady = false; sCursor = sClock = 0;
}
#else
#define SNAPSHOT_ACTIVE_SLOTS SNAPSHOT_SLOTS
#endif

/* Copies and each saved row start at a 16-byte boundary. Complete stripes
 * never load past the requested range. PCPYUD folds the upper doubleword
 * into the branch-tested lower doubleword; reference instruction semantics:
 * https://github.com/PCSX2/pcsx2/blob/master/pcsx2/MMI.cpp */
static bool source_equal_64(const u8* source, const u8* copy, u32 bytes) {
#ifdef _EE
    if (bytes >= 64 && !((uintptr_t)copy & 15)) {
        typedef unsigned int Vector128 __attribute__((mode(TI)));
        Vector128 input, saved, high, difference, combined;
        u32 mode, equal;
        /* Four stripes amortize branch/reduction overhead. Every differing
         * bit is ORed into the block; opposite differences cannot cancel. */
#define COMPARE_AT(offset) \
            "lq %[saved]," #offset "(%[copy])\n\t" \
            "pxor %[difference],%[input],%[saved]\n\t" \
            "por %[combined],%[combined],%[difference]\n\t"
#define ALIGNED_AT(offset) \
            "lq %[input]," #offset "(%[source])\n\t" COMPARE_AT(offset)
#define EIGHT_AT(offset, upper) \
            "ld %[input]," #offset "(%[source])\n\t" \
            "ld %[high]," #upper "(%[source])\n\t" \
            "pcpyld %[input],%[high],%[input]\n\t" COMPARE_AT(offset)
#define BYTE_AT(offset, lowerEnd, upper, upperEnd) \
            "ldl %[input]," #lowerEnd "(%[source])\n\t" \
            "ldr %[input]," #offset "(%[source])\n\t" \
            "ldl %[high]," #upperEnd "(%[source])\n\t" \
            "ldr %[high]," #upper "(%[source])\n\t" \
            "pcpyld %[input],%[high],%[input]\n\t" COMPARE_AT(offset)
        __asm__ volatile (
            ".set push\n\t.set noreorder\n\t"
            "addiu %[equal],$zero,0\n\t"
            "andi %[mode],%[source],15\n\t"
            "bnez %[mode],2f\n\t"
            "nop\n\t"
            "1:\n\t"
            "pxor %[combined],$zero,$zero\n\t"
            ALIGNED_AT(0) ALIGNED_AT(16) ALIGNED_AT(32) ALIGNED_AT(48)
            "b 6f\n\t"
            "addiu %[mode],$zero,0\n\t"
            "2:\n\t"
            "andi %[mode],%[source],7\n\t"
            "bnez %[mode],3f\n\t"
            "nop\n\t"
            "5:\n\t"
            "pxor %[combined],$zero,$zero\n\t"
            EIGHT_AT(0,8) EIGHT_AT(16,24) EIGHT_AT(32,40) EIGHT_AT(48,56)
            "b 6f\n\t"
            "addiu %[mode],$zero,1\n\t"
            "3:\n\t"
            "pxor %[combined],$zero,$zero\n\t"
            BYTE_AT(0,7,8,15) BYTE_AT(16,23,24,31) BYTE_AT(32,39,40,47) BYTE_AT(48,55,56,63)
            "addiu %[mode],$zero,2\n\t"
            "6:\n\t"
            "pcpyud %[high],%[combined],$zero\n\t"
            "or %[combined],%[combined],%[high]\n\t"
            "bnez %[combined],4f\n\t"
            "nop\n\t"
            "addiu %[source],%[source],64\n\t"
            "addiu %[copy],%[copy],64\n\t"
            "addiu %[bytes],%[bytes],-64\n\t"
            "sltiu %[equal],%[bytes],64\n\t"
            "bnez %[equal],4f\n\t"
            "nop\n\t"
            "beq %[mode],$zero,1b\n\t"
            "nop\n\t"
            "addiu %[mode],%[mode],-1\n\t"
            "beq %[mode],$zero,5b\n\t"
            "nop\n\t"
            "b 3b\n\t"
            "nop\n\t"
            "4:\n\t"
            ".set pop\n\t"
            : [source] "+&r"(source), [copy] "+&r"(copy), [bytes] "+&r"(bytes),
              [input] "=&r"(input), [saved] "=&r"(saved), [high] "=&r"(high),
              [difference] "=&r"(difference), [combined] "=&r"(combined),
              [mode] "=&r"(mode), [equal] "=&r"(equal)
            : : "memory");
#undef BYTE_AT
#undef EIGHT_AT
#undef ALIGNED_AT
#undef COMPARE_AT
        if (!equal) return false;
    }
    if (bytes >= 16 && !((uintptr_t)copy & 15)) {
        typedef unsigned int Vector128 __attribute__((mode(TI)));
        Vector128 input, saved, high, difference;
        u32 more, equal;
        __asm__ volatile (
            ".set push\n\t.set noreorder\n\t"
            "addiu %[equal],$zero,0\n\t"
            "andi %[more],%[source],15\n\t"
            "bnez %[more],2f\n\t"
            "nop\n\t"
            "1:\n\t"
            "lq %[input],0(%[source])\n\t"
            "b 6f\n\t"
            "addiu %[more],$zero,0\n\t"
            "2:\n\t"
            "andi %[more],%[source],7\n\t"
            "bnez %[more],3f\n\t"
            "nop\n\t"
            "5:\n\t"
            "ld %[input],0(%[source])\n\t"
            "ld %[high],8(%[source])\n\t"
            "pcpyld %[input],%[high],%[input]\n\t"
            "b 6f\n\t"
            "addiu %[more],$zero,1\n\t"
            "3:\n\t"
            "ldl %[input],7(%[source])\n\t"
            "ldr %[input],0(%[source])\n\t"
            "ldl %[high],15(%[source])\n\t"
            "ldr %[high],8(%[source])\n\t"
            "pcpyld %[input],%[high],%[input]\n\t"
            "addiu %[more],$zero,2\n\t"
            "6:\n\t"
            "lq %[saved],0(%[copy])\n\t"
            "pxor %[difference],%[input],%[saved]\n\t"
            "pcpyud %[high],%[difference],$zero\n\t"
            "or %[difference],%[difference],%[high]\n\t"
            "bnez %[difference],4f\n\t"
            "nop\n\t"
            "addiu %[source],%[source],16\n\t"
            "addiu %[copy],%[copy],16\n\t"
            "addiu %[bytes],%[bytes],-16\n\t"
            "sltiu %[equal],%[bytes],16\n\t"
            "bnez %[equal],4f\n\t"
            "nop\n\t"
            "beq %[more],$zero,1b\n\t"
            "nop\n\t"
            "addiu %[more],%[more],-1\n\t"
            "beq %[more],$zero,5b\n\t"
            "nop\n\t"
            "b 3b\n\t"
            "nop\n\t"
            "4:\n\t"
            ".set pop\n\t"
            : [source] "+&r"(source), [copy] "+&r"(copy), [bytes] "+&r"(bytes),
              [input] "=&r"(input), [saved] "=&r"(saved), [high] "=&r"(high),
              [difference] "=&r"(difference), [more] "=&r"(more), [equal] "=&r"(equal)
            : : "memory");
        if (!equal) return false;
    }
#endif
    return !memcmp(source, copy, bytes);
}

#ifdef _EE
/* Sixteen stripes amortize loop/reduction cost on unchanged source windows.
 * Only complete 256-byte blocks enter the production path; the original
 * 64/16/scalar comparator handles tails and unaligned saved copies. The
 * 128-byte comparator remains a smoke benchmark reference. */
#define WIDE_COMPARE_AT(offset) \
            "lq %[saved]," #offset "(%[copy])\n\t" \
            "pxor %[difference],%[input],%[saved]\n\t" \
            "por %[combined],%[combined],%[difference]\n\t"
#define WIDE_ALIGNED_AT(offset) \
            "lq %[input]," #offset "(%[source])\n\t" WIDE_COMPARE_AT(offset)
#define WIDE_EIGHT_AT(offset, upper) \
            "ld %[input]," #offset "(%[source])\n\t" \
            "ld %[high]," #upper "(%[source])\n\t" \
            "pcpyld %[input],%[high],%[input]\n\t" WIDE_COMPARE_AT(offset)
#define WIDE_BYTE_AT(offset, lowerEnd, upper, upperEnd) \
            "ldl %[input]," #lowerEnd "(%[source])\n\t" \
            "ldr %[input]," #offset "(%[source])\n\t" \
            "ldl %[high]," #upperEnd "(%[source])\n\t" \
            "ldr %[high]," #upper "(%[source])\n\t" \
            "pcpyld %[input],%[high],%[input]\n\t" WIDE_COMPARE_AT(offset)
#define WIDE_ALIGNED_128 \
            WIDE_ALIGNED_AT(0) WIDE_ALIGNED_AT(16) WIDE_ALIGNED_AT(32) WIDE_ALIGNED_AT(48) \
            WIDE_ALIGNED_AT(64) WIDE_ALIGNED_AT(80) WIDE_ALIGNED_AT(96) WIDE_ALIGNED_AT(112)
#define WIDE_EIGHT_128 \
            WIDE_EIGHT_AT(0,8) WIDE_EIGHT_AT(16,24) WIDE_EIGHT_AT(32,40) WIDE_EIGHT_AT(48,56) \
            WIDE_EIGHT_AT(64,72) WIDE_EIGHT_AT(80,88) WIDE_EIGHT_AT(96,104) WIDE_EIGHT_AT(112,120)
#define WIDE_BYTE_128 \
            WIDE_BYTE_AT(0,7,8,15) WIDE_BYTE_AT(16,23,24,31) \
            WIDE_BYTE_AT(32,39,40,47) WIDE_BYTE_AT(48,55,56,63) \
            WIDE_BYTE_AT(64,71,72,79) WIDE_BYTE_AT(80,87,88,95) \
            WIDE_BYTE_AT(96,103,104,111) WIDE_BYTE_AT(112,119,120,127)
#define WIDE_ALIGNED_256 WIDE_ALIGNED_128 \
            WIDE_ALIGNED_AT(128) WIDE_ALIGNED_AT(144) WIDE_ALIGNED_AT(160) WIDE_ALIGNED_AT(176) \
            WIDE_ALIGNED_AT(192) WIDE_ALIGNED_AT(208) WIDE_ALIGNED_AT(224) WIDE_ALIGNED_AT(240)
#define WIDE_EIGHT_256 WIDE_EIGHT_128 \
            WIDE_EIGHT_AT(128,136) WIDE_EIGHT_AT(144,152) WIDE_EIGHT_AT(160,168) WIDE_EIGHT_AT(176,184) \
            WIDE_EIGHT_AT(192,200) WIDE_EIGHT_AT(208,216) WIDE_EIGHT_AT(224,232) WIDE_EIGHT_AT(240,248)
#define WIDE_BYTE_256 WIDE_BYTE_128 \
            WIDE_BYTE_AT(128,135,136,143) WIDE_BYTE_AT(144,151,152,159) \
            WIDE_BYTE_AT(160,167,168,175) WIDE_BYTE_AT(176,183,184,191) \
            WIDE_BYTE_AT(192,199,200,207) WIDE_BYTE_AT(208,215,216,223) \
            WIDE_BYTE_AT(224,231,232,239) WIDE_BYTE_AT(240,247,248,255)
#define DEFINE_WIDE_EQUAL(name, block, aligned, eight, arbitrary) \
static bool name(const u8* source, const u8* copy, u32 bytes) { \
    if (bytes >= block && !((uintptr_t)copy & 15)) { \
        typedef unsigned int Vector128 __attribute__((mode(TI))); \
        Vector128 input, saved, high, difference, combined; \
        u32 mode, equal; \
        __asm__ volatile ( \
            ".set push\n\t.set noreorder\n\t" \
            "addiu %[equal],$zero,0\n\t" \
            "andi %[mode],%[source],15\n\t" \
            "bnez %[mode],2f\n\t" \
            "nop\n\t" \
            "1:\n\t" \
            "pxor %[combined],$zero,$zero\n\t" \
            aligned \
            "b 6f\n\t" \
            "addiu %[mode],$zero,0\n\t" \
            "2:\n\t" \
            "andi %[mode],%[source],7\n\t" \
            "bnez %[mode],3f\n\t" \
            "nop\n\t" \
            "5:\n\t" \
            "pxor %[combined],$zero,$zero\n\t" \
            eight \
            "b 6f\n\t" \
            "addiu %[mode],$zero,1\n\t" \
            "3:\n\t" \
            "pxor %[combined],$zero,$zero\n\t" \
            arbitrary \
            "addiu %[mode],$zero,2\n\t" \
            "6:\n\t" \
            "pcpyud %[high],%[combined],$zero\n\t" \
            "or %[combined],%[combined],%[high]\n\t" \
            "bnez %[combined],4f\n\t" \
            "nop\n\t" \
            "addiu %[source],%[source]," #block "\n\t" \
            "addiu %[copy],%[copy]," #block "\n\t" \
            "addiu %[bytes],%[bytes],-" #block "\n\t" \
            "sltiu %[equal],%[bytes]," #block "\n\t" \
            "bnez %[equal],4f\n\t" \
            "nop\n\t" \
            "beq %[mode],$zero,1b\n\t" \
            "nop\n\t" \
            "addiu %[mode],%[mode],-1\n\t" \
            "beq %[mode],$zero,5b\n\t" \
            "nop\n\t" \
            "b 3b\n\t" \
            "nop\n\t" \
            "4:\n\t" \
            ".set pop\n\t" \
            : [source] "+&r"(source), [copy] "+&r"(copy), [bytes] "+&r"(bytes), \
              [input] "=&r"(input), [saved] "=&r"(saved), [high] "=&r"(high), \
              [difference] "=&r"(difference), [combined] "=&r"(combined), \
              [mode] "=&r"(mode), [equal] "=&r"(equal) \
            : : "memory"); \
        if (!equal) return false; \
    } \
    return source_equal_64(source, copy, bytes); \
}
#ifdef PS2_RENDERER_SMOKE_CAPTURE
DEFINE_WIDE_EQUAL(source_equal_128, 128, WIDE_ALIGNED_128, WIDE_EIGHT_128, WIDE_BYTE_128)
#endif
DEFINE_WIDE_EQUAL(source_equal_256, 256, WIDE_ALIGNED_256, WIDE_EIGHT_256, WIDE_BYTE_256)
#undef DEFINE_WIDE_EQUAL
#undef WIDE_BYTE_256
#undef WIDE_EIGHT_256
#undef WIDE_ALIGNED_256
#undef WIDE_BYTE_128
#undef WIDE_EIGHT_128
#undef WIDE_ALIGNED_128
#undef WIDE_BYTE_AT
#undef WIDE_EIGHT_AT
#undef WIDE_ALIGNED_AT
#undef WIDE_COMPARE_AT
#endif
static bool source_equal(const u8* source, const u8* copy, u32 bytes) {
#ifdef _EE
    return source_equal_256(source, copy, bytes);
#else
    return source_equal_64(source, copy, bytes);
#endif
}
#ifdef PS2_RENDERER_SMOKE_CAPTURE
static bool source_equal_selected(const u8* source, const u8* copy, u32 bytes) {
#ifdef _EE
    if (sCompareBytes == 128) return source_equal_128(source, copy, bytes);
#endif
    if (sCompareBytes == 256) return source_equal(source, copy, bytes);
    return source_equal_64(source, copy, bytes);
}
/* Select outside the measured loop so each benchmark pays the same indirect
 * call cost instead of comparing differently ordered selector branches. */
PS2TextureContentCompare ps2_texture_content_test_comparator(void) {
#ifdef _EE
    if (sCompareBytes == 128) return source_equal_128;
    if (sCompareBytes == 256) return source_equal_256;
#endif
    return source_equal_64;
}
/* Route smoke cache validation and direct tests through the same selector.
 * Production uses the 256-byte wrapper and has no selection branch. */
#define source_equal source_equal_selected
bool ps2_texture_content_test_equal(const u8* source, const u8* copy, unsigned bytes) {
    return source_equal(source, copy, bytes);
}
#endif

static unsigned bucket(const u8* source) {
    u32 bits = (uintptr_t)source >> 4;
    return (bits ^ (bits >> 8) ^ (bits >> 16)) & (SNAPSHOT_BUCKETS - 1);
}
static void unlink_snapshot(unsigned slot) {
    u16* link = &sHeads[bucket(sSnapshots[slot].spans[0].source)];
    while (*link != SNAPSHOT_END) {
        if (*link == slot) { *link = sSnapshots[slot].next; return; }
        link = &sSnapshots[*link].next;
    }
}
static bool same_spans(const Snapshot* saved, const PS2TextureSourceSpan* spans, unsigned count) {
    if (saved->count != count) return false;
    for (unsigned i = 0; i < count; i++) {
        const PS2TextureSourceSpan* previous = &saved->spans[i];
        const PS2TextureSourceSpan* span = &spans[i];
        if (previous->source != span->source || previous->rowBytes != span->rowBytes ||
            previous->stride != span->stride || previous->rows != span->rows ||
            previous->hashRows != span->hashRows) return false;
    }
    return true;
}
static bool unchanged(const Snapshot* saved, const PS2TextureSourceSpan* spans, unsigned count) {
    const u8* copy = sCopies + saved->offset;
    for (unsigned i = 0; i < count; i++) {
        const PS2TextureSourceSpan* span = &spans[i];
        unsigned stride = span->stride ? span->stride : span->rowBytes;
        unsigned savedStride = (span->rowBytes + 15u) & ~15u;
        /* Common rows are contiguous and already aligned in the copy. */
        if (stride == span->rowBytes && savedStride == span->rowBytes) {
            unsigned bytes = span->rows * span->rowBytes;
            sStats.comparedBytes += bytes;
            if (!source_equal(span->source, copy, bytes)) return false;
            copy += bytes;
        } else for (unsigned row = 0; row < span->rows; row++) {
            sStats.comparedBytes += span->rowBytes;
            if (!source_equal(span->source + row * stride, copy, span->rowBytes)) return false;
            copy += savedStride;
        }
    }
    return true;
}
static u32 hash_spans(const PS2TextureSourceSpan* spans, unsigned count) {
    u32 hash = 0;
    sStats.hashes++;
    for (unsigned i = 0; i < count; i++) {
        const PS2TextureSourceSpan* span = &spans[i];
        unsigned stride = span->stride ? span->stride : span->rowBytes;
        sStats.hashedBytes += span->rowBytes * span->rows;
        if (!span->hashRows && stride == span->rowBytes)
            hash = ps2_texture_hash(span->source, span->rowBytes * span->rows, hash);
        else for (unsigned row = 0; row < span->rows; row++)
            hash = ps2_texture_hash(span->source + row * stride, span->rowBytes, hash);
    }
    return hash;
}
static void copy_spans(u8* destination, const PS2TextureSourceSpan* spans, unsigned count) {
    for (unsigned i = 0; i < count; i++) {
        const PS2TextureSourceSpan* span = &spans[i];
        unsigned stride = span->stride ? span->stride : span->rowBytes;
        unsigned savedStride = (span->rowBytes + 15u) & ~15u;
        if (stride == span->rowBytes && savedStride == span->rowBytes) {
            unsigned total = span->rowBytes * span->rows;
            memcpy(destination, span->source, total); destination += total;
        } else for (unsigned row = 0; row < span->rows; row++) {
            memcpy(destination, span->source + row * stride, span->rowBytes);
            destination += savedStride;
        }
    }
}

u32 ps2_texture_content_hash(const PS2TextureSourceSpan* spans, unsigned count) {
    u32 loadedBytes = 0;
    u64 copyBytes = 0;
    for (unsigned i = 0; i < count; i++) {
        loadedBytes += spans[i].rowBytes * spans[i].rows;
        copyBytes += (u64)((spans[i].rowBytes + 15u) & ~15u) * spans[i].rows;
    }
    if (!count || count > 2
#ifdef PS2_RENDERER_SMOKE_CAPTURE
        || sDisabled
#endif
    ) return hash_spans(spans, count);
    if (loadedBytes < SNAPSHOT_MIN_BYTES) {
        sStats.smallBypass++;
        return hash_spans(spans, count);
    }
    if (copyBytes > SNAPSHOT_MAX_BYTES) {
        sStats.largeBypass++;
        return hash_spans(spans, count);
    }
    if (!sReady) { memset(sHeads, 0xff, sizeof(sHeads)); sReady = true; }
    unsigned selectedBucket = bucket(spans[0].source);
    for (unsigned slot = sHeads[selectedBucket]; slot != SNAPSHOT_END; slot = sSnapshots[slot].next) {
        Snapshot* saved = &sSnapshots[slot];
        if (!same_spans(saved, spans, count)) continue;
        if (unchanged(saved, spans, count)) {
            saved->used = ++sClock;
            sStats.hits++;
            return saved->hash;
        }
        /* Identical spans keep the same bounded, aligned copy layout. A
         * mutation replaces its bytes in place so animation cannot advance
         * the ring over unrelated snapshots on every update. Hash both inputs
         * in the original row/seed order before replacing the saved copy. */
        saved->hash = hash_spans(spans, count);
        copy_spans(sCopies + saved->offset, spans, count);
        saved->used = ++sClock;
        sStats.mutations++;
        return saved->hash;
    }
    u32 hash = hash_spans(spans, count);
    u32 bytes = copyBytes;
    if (sCursor + bytes > SNAPSHOT_BYTES) sCursor = 0;
    unsigned slot = SNAPSHOT_ACTIVE_SLOTS;
    for (unsigned i = 0; i < SNAPSHOT_ACTIVE_SLOTS; i++) {
        Snapshot* saved = &sSnapshots[i];
        if (saved->valid && saved->offset < sCursor + bytes && sCursor < saved->offset + saved->bytes) {
            unlink_snapshot(i); saved->valid = 0;
            sStats.overlapEvictions++;
        }
        if (!saved->valid) slot = i;
    }
    if (slot == SNAPSHOT_ACTIVE_SLOTS) {
        slot = 0;
        for (unsigned i = 1; i < SNAPSHOT_ACTIVE_SLOTS; i++)
            if (sSnapshots[i].used < sSnapshots[slot].used) slot = i;
        unlink_snapshot(slot);
        sStats.descriptorEvictions++;
    }
    Snapshot* saved = &sSnapshots[slot];
    saved->offset = sCursor; saved->bytes = bytes; saved->hash = hash;
    saved->used = ++sClock; saved->count = count; saved->valid = 1;
    memcpy(saved->spans, spans, count * sizeof(*spans));
    copy_spans(sCopies + sCursor, spans, count);
    sCursor += bytes;
    saved->next = sHeads[selectedBucket]; sHeads[selectedBucket] = slot;
    sStats.newSnapshots++;
    return hash;
}

PS2TextureContentStats ps2_texture_content_stats(bool reset) {
    PS2TextureContentStats stats = sStats;
    if (reset) memset(&sStats, 0, sizeof(sStats));
    return stats;
}
