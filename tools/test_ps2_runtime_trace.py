#!/usr/bin/env python3
"""Test the actual logger with a host filesystem and mocked EE scheduling."""
from pathlib import Path
import subprocess
import tempfile

KERNEL = r'''
#include <stdint.h>
typedef struct { void (*func)(void*); void* stack; unsigned stack_size; void* gp_reg; int initial_priority; } ee_thread_t;
int DIntr(void); void EIntr(void); int CreateThread(ee_thread_t*); int StartThread(int, void*);
void ExitThread(void); int DeleteThread(int);
'''
DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
static int interrupts=1, failWrite;
static uint64_t now;
static int short_write(int fd, const void* bytes, unsigned size) {
    if (failWrite) return -1;
    return write(fd, bytes, size > 31 ? 31 : size);
}
#define write short_write
#include "TRACE_SOURCE"
#undef write
void* _gp;
volatile unsigned gPS2DspCyclesTotal;
Ps2TraceTicks osGetTime(void) { return now; }
void ps2_rom_cache_stats(unsigned* hits, unsigned* reads) { *hits=12; *reads=5; }
int DIntr(void) { int old=interrupts; interrupts=0; return old; }
void EIntr(void) { interrupts=1; }
int CreateThread(ee_thread_t* thread) { return thread->initial_priority == 51 ? 1 : -1; }
int StartThread(int id, void* arg) { (void)id; (void)arg; return 0; }
int DeleteThread(int id) { (void)id; return 0; }
void ExitThread(void) {}
static char contents[65536];
static const char* read_log(void) {
    FILE* file=fopen(sPath,"rb");
    if (!file) return "";
    unsigned bytes=fread(contents,1,sizeof(contents)-1,file);
    contents[bytes]=0; fclose(file); return contents;
}
int main(int argc, char** argv) {
    (void)argc;
    ps2_runtime_trace_init(argv[1]);
    ps2_runtime_trace_start();
    if (!sRecording || !interrupts) return 1;
    for (int i=0;i<10;i++) ps2_runtime_frame(468750,93750,46875);
    ps2_runtime_audio(0,48000,2,1); ps2_runtime_audio(1920,48000,2,1);
    ps2_runtime_rom_read(0x1234,16384,2343750,0,1);
    ps2_runtime_rom_read(0x5678,8192,46875,-1,0);
    ps2_runtime_rom_wait(468750);
    now=46875000; trace_tick();
    if (strstr(read_log(),"SAMPLE")) return 2; /* No producer writes to disk. */
    now=5ull*46875000; trace_tick();
    const char* log=read_log();
    if (!strstr(log,"frames=10 fps10=100 gfxMs=100,20,10") ||
        !strstr(log,"reads=2 bytes=16384 seeks=1 errors=1 ioMs=51 waitMs=10 worstReadMs=50") ||
        !strstr(log,"queueBytesMin=0 queueBytesMax=1920 polls=2 zeroPolls=1")) return 3;
    if (!interrupts) return 4;
    interrupts=0; ps2_runtime_event("already disabled"); if (interrupts) return 5;
    interrupts=1;
    for (int i=0;i<200;i++) ps2_runtime_event("overflow id=%u",i);
    if (sCount!=128 || sLost<72) return 6;
    flush_records();
    log=read_log();
    if (!strstr(log,"overflow id=199\n") || strstr(log,"overflow id=0\n")) return 7;
    failWrite=1; flush_records();
    if (sRecording || !interrupts) return 8; /* USB error must not block the game. */
    failWrite=0; sCount=sFirst=0;
    ps2_runtime_trace_init(argv[1]);
    sBytes=FILE_LIMIT-40;
    ps2_runtime_event("this record does not fit the remaining file budget 0123456789");
    flush_records();
    if (sRecording || !interrupts) return 9;
    puts("PS2 runtime logger: PASS batching, stats, short writes, overflow, IRQ preservation, error and limit");
    return 0;
}
'''

def main():
    source = Path('src/platform/ps2/ps2_runtime_trace.c').resolve()
    with tempfile.TemporaryDirectory(prefix='ps2-runtime-test-') as folder:
        root = Path(folder)
        (root / 'kernel.h').write_text(KERNEL)
        (root / 'driver.c').write_text(DRIVER.replace('TRACE_SOURCE', str(source)))
        executable = root / 'test'
        subprocess.run(['cc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-DPS2_RUNTIME_DIAGNOSTIC=1',
            '-I', str(root), '-Iinclude/ps2', str(root / 'driver.c'), '-o', str(executable)], check=True)
        subprocess.run([str(executable), str(root / 'OOT.Z64')], check=True)

if __name__ == '__main__':
    main()
