/* Independent implementation of the XXH32 algorithm, for mutable texture RAM.
 * Specification: https://github.com/Cyan4973/xxHash/blob/dev/doc/xxhash_spec.md
 * Specification copyright (c) Yann Collet. Permission is granted to copy and
 * distribute the document for any purpose without charge, provided its
 * copyright and notice are preserved and substantive changes are marked.
 * This is an EE implementation, not a copy of the reference source. */
#include <stdint.h>
#include <string.h>
static uint32_t rotate(uint32_t value, unsigned bits) {
    return (value << bits) | (value >> (32-bits));
}
static uint32_t word(const unsigned char* source) {
    uint32_t value;
    /* Native EE is little-endian. memcpy permits unaligned texture rows. */
    memcpy(&value, source, sizeof(value));
    return value;
}
static uint32_t aligned_word(const unsigned char* source) {
    uint32_t value;
    /* Alignment is checked once for the stripe loop. Without this hint,
     * EE GCC implements memcpy with four byte loads and four stack stores. */
    memcpy(&value, __builtin_assume_aligned(source, 4), sizeof(value));
    return value;
}
static uint32_t round_lane(uint32_t lane, uint32_t value) {
    return rotate(lane + value*0x85ebca77u, 13)*0x9e3779b1u;
}
#ifdef _EE
/* PMULTUW multiplies words 0 and 2. Expand each pair into those positions,
 * then pack the low 32 bits back into the four independent XXH32 lanes.
 * Complete stripes aligned to sixteen bytes use LQ. Eight-byte stripes use
 * two LDs and PCPYLD. Other alignments use paired LDL/LDR loads at offsets
 * inside the same complete stripe. Tails retain the scalar path. Every byte
 * is checked, without rounded-up reads past the final stripe. */
#define XXH32_STRIPE_ROUND \
        "pextlw %[low],$zero,%[product]\n\t" \
        "pextuw %[high],$zero,%[product]\n\t" \
        "pmultuw %[low],%[low],%[prime2]\n\t" \
        "pmultuw %[high],%[high],%[prime2]\n\t" \
        "ppacw %[product],%[high],%[low]\n\t" \
        "paddw %[state],%[state],%[product]\n\t" \
        "psllw %[low],%[state],13\n\t" \
        "psrlw %[high],%[state],19\n\t" \
        "por %[state],%[low],%[high]\n\t" \
        "pextlw %[low],$zero,%[state]\n\t" \
        "pextuw %[high],$zero,%[state]\n\t" \
        "pmultuw %[low],%[low],%[prime1]\n\t" \
        "pmultuw %[high],%[high],%[prime1]\n\t" \
        "ppacw %[state],%[high],%[low]\n\t"
static void vector_stripes(const unsigned char** cursor, uint32_t* length, uint32_t lanes[4]) {
    typedef unsigned int vector128 __attribute__((mode(TI)));
    static const uint32_t primes[8] __attribute__((aligned(16))) = {
        0x9e3779b1u,0x9e3779b1u,0x9e3779b1u,0x9e3779b1u,
        0x85ebca77u,0x85ebca77u,0x85ebca77u,0x85ebca77u
    };
    const unsigned char* source=*cursor;
    uint32_t remaining=*length, more;
    vector128 state, low, high, product, prime1, prime2;
    __asm__ volatile (
        ".set push\n\t.set noreorder\n\t"
        "lq %[state],0(%[lanes])\n\t"
        "lq %[prime1],0(%[primes])\n\t"
        "lq %[prime2],16(%[primes])\n\t"
        "andi %[more],%[source],15\n\t"
        "bnez %[more],2f\n\t"
        "nop\n\t"
        "1:\n\t"
        "lq %[product],0(%[source])\n\t"
        XXH32_STRIPE_ROUND
        "addiu %[source],%[source],16\n\t"
        "addiu %[remaining],%[remaining],-16\n\t"
        "sltiu %[more],%[remaining],16\n\t"
        "beq %[more],$zero,1b\n\t"
        "nop\n\t"
        "b 4f\n\t"
        "nop\n\t"
        "2:\n\t"
        "andi %[more],%[source],7\n\t"
        "bnez %[more],3f\n\t"
        "nop\n\t"
        "5:\n\t"
        "ld %[low],0(%[source])\n\t"
        "ld %[high],8(%[source])\n\t"
        "pcpyld %[product],%[high],%[low]\n\t"
        XXH32_STRIPE_ROUND
        "addiu %[source],%[source],16\n\t"
        "addiu %[remaining],%[remaining],-16\n\t"
        "sltiu %[more],%[remaining],16\n\t"
        "beq %[more],$zero,5b\n\t"
        "nop\n\t"
        "b 4f\n\t"
        "nop\n\t"
        "3:\n\t"
        "ldl %[low],7(%[source])\n\t"
        "ldr %[low],0(%[source])\n\t"
        "ldl %[high],15(%[source])\n\t"
        "ldr %[high],8(%[source])\n\t"
        "pcpyld %[product],%[high],%[low]\n\t"
        XXH32_STRIPE_ROUND
        "addiu %[source],%[source],16\n\t"
        "addiu %[remaining],%[remaining],-16\n\t"
        "sltiu %[more],%[remaining],16\n\t"
        "beq %[more],$zero,3b\n\t"
        "nop\n\t"
        "4:\n\t"
        "sq %[state],0(%[lanes])\n\t"
        ".set pop\n\t"
        : [source] "+&r"(source), [remaining] "+&r"(remaining),
          [state] "=&r"(state), [low] "=&r"(low), [high] "=&r"(high),
          [product] "=&r"(product), [prime1] "=&r"(prime1), [prime2] "=&r"(prime2),
          [more] "=&r"(more)
        : [lanes] "r"(lanes), [primes] "r"(primes)
        : "memory", "hi", "lo");
    *cursor=source; *length=remaining;
}
#undef XXH32_STRIPE_ROUND
#endif
uint32_t ps2_texture_hash(const unsigned char* source, uint32_t bytes, uint32_t seed) {
    uint32_t remaining = bytes, hash;
    if (remaining >= 16) {
        uint32_t a=seed+0x9e3779b1u+0x85ebca77u, b=seed+0x85ebca77u;
        uint32_t c=seed, d=seed-0x9e3779b1u;
#ifdef _EE
        /* Four-byte-only rows already have efficient LW scalar stripes.
         * Keep that path; MMI benefits eight-byte and arbitrary-byte rows. */
        if (((uintptr_t)source & 7) != 4) {
            uint32_t lanes[4] __attribute__((aligned(16)))={a,b,c,d};
            vector_stripes(&source,&remaining,lanes);
            a=lanes[0]; b=lanes[1]; c=lanes[2]; d=lanes[3];
        } else
#endif
        if (((uintptr_t)source & 3) == 0) {
            do {
                a=round_lane(a,aligned_word(source)); b=round_lane(b,aligned_word(source+4));
                c=round_lane(c,aligned_word(source+8)); d=round_lane(d,aligned_word(source+12));
                source+=16; remaining-=16;
            } while (remaining >= 16);
        } else {
            do {
                a=round_lane(a,word(source)); b=round_lane(b,word(source+4));
                c=round_lane(c,word(source+8)); d=round_lane(d,word(source+12));
                source+=16; remaining-=16;
            } while (remaining >= 16);
        }
        hash=rotate(a,1)+rotate(b,7)+rotate(c,12)+rotate(d,18);
    } else hash=seed+0x165667b1u;
    hash+=bytes;
    while (remaining >= 4) {
        hash=rotate(hash+word(source)*0xc2b2ae3du,17)*0x27d4eb2fu;
        source+=4; remaining-=4;
    }
    while (remaining--) hash=rotate(hash+*source++*0x165667b1u,11)*0x9e3779b1u;
    hash^=hash>>15; hash*=0x85ebca77u;
    hash^=hash>>13; hash*=0xc2b2ae3du;
    return hash^(hash>>16);
}
