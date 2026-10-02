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
static uint32_t round_lane(uint32_t lane, uint32_t value) {
    return rotate(lane + value*0x85ebca77u, 13)*0x9e3779b1u;
}
uint32_t ps2_texture_hash(const unsigned char* source, uint32_t bytes, uint32_t seed) {
    uint32_t remaining = bytes, hash;
    if (remaining >= 16) {
        uint32_t a=seed+0x9e3779b1u+0x85ebca77u, b=seed+0x85ebca77u;
        uint32_t c=seed, d=seed-0x9e3779b1u;
        do {
            a=round_lane(a,word(source)); b=round_lane(b,word(source+4));
            c=round_lane(c,word(source+8)); d=round_lane(d,word(source+12));
            source+=16; remaining-=16;
        } while (remaining >= 16);
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
