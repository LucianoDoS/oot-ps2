/* EE copies for the existing SDK memcpy call sites, including gsKit's
 * packed vertex packets. Unaligned inputs retain the SDK implementation.
 * Only complete, naturally aligned blocks use wider loads and stores. */
#include <stdint.h>
#include <string.h>

extern void* __real_memcpy(void* destination, const void* source, size_t bytes);

void* __wrap_memcpy(void* destination, const void* source, size_t bytes) {
    void* result=destination;
    unsigned char* output=destination;
    const unsigned char* input=source;
    uintptr_t alignment=(uintptr_t)output | (uintptr_t)input;
    if (!bytes) return result;
    if (bytes<16 || (alignment&7)) return __real_memcpy(destination,source,bytes);
    if (!(alignment&15)) {
        typedef unsigned int vector128 __attribute__((mode(TI)));
        while (bytes>=64) {
            vector128 a,b,c,d;
            __asm__ volatile (
                "lq %[a],0(%[input])\n\t"
                "lq %[b],16(%[input])\n\t"
                "lq %[c],32(%[input])\n\t"
                "lq %[d],48(%[input])\n\t"
                "sq %[a],0(%[output])\n\t"
                "sq %[b],16(%[output])\n\t"
                "sq %[c],32(%[output])\n\t"
                "sq %[d],48(%[output])\n\t"
                : [a] "=&r"(a), [b] "=&r"(b), [c] "=&r"(c), [d] "=&r"(d)
                : [input] "r"(input), [output] "r"(output) : "memory");
            input+=64; output+=64; bytes-=64;
        }
        while (bytes>=16) {
            vector128 value;
            __asm__ volatile ("lq %[value],0(%[input])\n\tsq %[value],0(%[output])\n\t"
                : [value] "=&r"(value)
                : [input] "r"(input), [output] "r"(output) : "memory");
            input+=16; output+=16; bytes-=16;
        }
    } else {
        /* Stack point arrays can be aligned to eight bytes while the GIF
         * queue is aligned to sixteen. LD/SD still halve the word copies. */
        while (bytes>=16) {
            uint64_t a,b;
            __asm__ volatile ("ld %[a],0(%[input])\n\tld %[b],8(%[input])\n\t"
                              "sd %[a],0(%[output])\n\tsd %[b],8(%[output])\n\t"
                : [a] "=&r"(a), [b] "=&r"(b)
                : [input] "r"(input), [output] "r"(output) : "memory");
            input+=16; output+=16; bytes-=16;
        }
    }
    if (bytes) __real_memcpy(output,input,bytes);
    return result;
}
