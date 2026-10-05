#!/usr/bin/env python3
"""Exercise cache ownership with a CPU/RAM model; run with a host C compiler.

PCSX2's ordinary EE mode does not expose the dirty-cache loss observed on a
console. This model tests neighbour preservation and requested-line writeback;
BOOTTEST separately exercises the actual SDK through cached/uncached aliases.
"""
from pathlib import Path
import subprocess
import tempfile


HEADER = r'''
#include <stdint.h>
typedef int32_t s32;
#define WRITEBACK_DCACHE 0
#define INVALIDATE_ICACHE 2
void SyncDCache(void*, void*);
void FlushCache(s32);
'''

DRIVER = r'''
#include <kernel.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
extern void osInvalDCache(void*, s32);
extern void osWritebackDCache(void*, s32);
extern void osWritebackDCacheAll(void);
static unsigned char cpu[256] __attribute__((aligned(64))), ram[256];
void SyncDCache(void* first, void* last) {
    uintptr_t begin=(uintptr_t)first & ~(uintptr_t)63;
    uintptr_t end=(uintptr_t)last & ~(uintptr_t)63;
    if (begin<(uintptr_t)cpu || end>=(uintptr_t)(cpu+256)) abort();
    for (uintptr_t at=begin; at<=end; at+=64)
        memcpy(ram+at-(uintptr_t)cpu, (void*)at, 64);
}
void FlushCache(s32 operation) {
    if (operation==0) memcpy(ram,cpu,256);
    else if (operation==1) memcpy(cpu,ram,256); /* dirty stores are lost */
}
static void reset(void) {
    memset(ram,0,256);
    for (int i=0;i<256;i++) cpu[i]=(unsigned char)(i^0xa5);
}
int main(void) {
    reset();
    osInvalDCache(cpu+3,70);
    for (int i=0;i<256;i++) {
        if (cpu[i]!=(unsigned char)(i^0xa5)) return 1;
        if (ram[i]!=(i<128 ? (unsigned char)(i^0xa5) : 0)) return 2;
    }
    reset();
    osWritebackDCache(cpu+127,2);
    for (int i=0;i<256;i++)
        if (ram[i]!=((i>=64 && i<192) ? (unsigned char)(i^0xa5) : 0)) return 3;
    reset();
    osInvalDCache(cpu+64,64); /* must not touch the next cache line */
    for (int i=0;i<256;i++)
        if (ram[i]!=((i>=64 && i<128) ? (unsigned char)(i^0xa5) : 0)) return 4;
    reset();
    osInvalDCache(NULL,64);
    osInvalDCache(cpu,0);
    osWritebackDCache(cpu,-1);
    for (int i=0;i<256;i++)
        if (ram[i] || cpu[i]!=(unsigned char)(i^0xa5)) return 5;
    osWritebackDCacheAll();
    if (memcmp(cpu,ram,256)) return 6;
    puts("PS2 cache model: PASS");
    return 0;
}
'''


def main():
    source = Path('src/platform/ps2/ps2_cache.c').read_text()
    with tempfile.TemporaryDirectory(prefix='ps2-cache-test-') as folder:
        root = Path(folder)
        (root / 'kernel.h').write_text(HEADER)
        (root / 'driver.c').write_text(DRIVER)
        for broken in (False, True):
            text = source
            if broken:
                expected = 'void osInvalDCache(void* address, s32 bytes) { sync_range(address, bytes); }'
                assert expected in text
                text = text.replace(expected, 'void osInvalDCache(void* address, s32 bytes) { FlushCache(1); }')
            (root / 'cache.c').write_text(text)
            exe = root / 'test'
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Wno-unused-parameter',
                            '-I', str(root), str(root / 'driver.c'), str(root / 'cache.c'), '-o', str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            if broken:
                assert result.returncode == 1, 'Dirty neighbour loss must be detected'
                print('Old global invalidation rejected: PASS')
            else:
                assert result.returncode == 0, result.stdout + result.stderr
                print(result.stdout.strip())


if __name__ == '__main__':
    main()
