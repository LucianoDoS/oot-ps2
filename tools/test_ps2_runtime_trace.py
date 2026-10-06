#!/usr/bin/env python3
"""Test the actual logger with a host filesystem and mocked EE scheduling."""
from pathlib import Path
import subprocess
import tempfile

KERNEL = r'''
#include <stdint.h>
typedef struct { void (*func)(void*); void* stack; unsigned stack_size; void* gp_reg; int initial_priority; } ee_thread_t;
typedef struct { int init_count, max_count; } ee_sema_t;
int DIntr(void); void EIntr(void); int CreateThread(ee_thread_t*); int StartThread(int, void*);
void ExitThread(void); int DeleteThread(int);
int CreateSema(ee_sema_t*); int DeleteSema(int); int WaitSema(int); int SignalSema(int);
'''
DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
static int interrupts=1, failWrite, wakeCount, wakeSignals, failSema, snapshots;
static int partialWrites=1, writeCalls, largestWrite;
static uint64_t now;
static int short_write(int fd, const void* bytes, unsigned size) {
    if (failWrite) return -1;
    if ((uintptr_t)bytes % 64 || size > 4096 || !interrupts) return -1;
    writeCalls++;
    if ((int)size > largestWrite) largestWrite=size;
    return write(fd, bytes, partialWrites && size > 31 ? 31 : size);
}
#define write short_write
#include "TRACE_SOURCE"
#undef write
void* _gp;
volatile unsigned gPS2DspCyclesTotal;
void ps2_boot_trace_stage(const char* message) { (void)message; }
void ps2_boot_trace_result(const char* name, int result) { (void)name; (void)result; }
Ps2TraceTicks osGetTime(void) { return now; }
void ps2_rom_cache_stats(unsigned* hits, unsigned* reads) { *hits=12; *reads=5; }
void ps2_runtime_stall_snapshot(void) { snapshots++; }
int DIntr(void) { int old=interrupts; interrupts=0; return old; }
void EIntr(void) { interrupts=1; }
int CreateThread(ee_thread_t* thread) { return thread->initial_priority == 51 ? 1 : -1; }
int StartThread(int id, void* arg) { (void)id; (void)arg; return 0; }
int DeleteThread(int id) { (void)id; return 0; }
void ExitThread(void) {}
int CreateSema(ee_sema_t* sema) { wakeCount=sema->init_count; return failSema || sema->max_count != 1 ? -1 : 2; }
int DeleteSema(int id) { (void)id; return 0; }
int SignalSema(int id) { if (id != 2) return -1; wakeSignals++; if (wakeCount) return -1; wakeCount=1; return 0; }
int WaitSema(int id) { (void)id; return -1; } /* Deliberate failure must end the worker. */
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
    if (!sRecording || !interrupts || !strstr(read_log(),"LOGGER_START")) return 1;
    int fd=open(sPath,O_RDONLY);
    int diskSize=lseek(fd,0,SEEK_END); close(fd);
    if (sBytes % 512 || diskSize != (int)sBytes) return 14;
    for (int i=0;i<10;i++) ps2_runtime_frame(468750,93750,46875);
    ps2_runtime_audio(0,48000,2,1); ps2_runtime_audio(1920,48000,2,1);
    ps2_runtime_rom_read(0x1234,16384,2343750,0,1);
    ps2_runtime_rom_read(0x5678,8192,46875,-1,0);
    ps2_runtime_rom_wait(468750);
    now=46875000; trace_tick();
    if (strstr(read_log(),"SAMPLE")) return 2; /* No producer writes to disk. */
    now=FLUSH_SECONDS*46875000ull; trace_tick();
    const char* log=read_log();
    if (!strstr(log,"frames=10 fps10=100 gfxMs=100,20,10") ||
        !strstr(log,"reads=2 bytes=16384 seeks=1 errors=1 ioMs=51 waitMs=10 worstReadMs=50") ||
        !strstr(log,"queueBytesMin=0 queueBytesMax=1920 polls=2 zeroPolls=1")) return 3;
    if (!interrupts) return 4;
    for (int i=0;i<120;i++) ps2_runtime_vblank();
    if (wakeSignals!=2 || wakeCount!=1 || strstr(read_log(),"clockFallback=1")) return 10;
    trace_tick(); /* A stuck Timer 0 must not prevent the next sample. */
    flush_records();
    if (!strstr(read_log(),"retraces=120 clockMs=0 clockFallback=1")) return 11;
    if (snapshots != 1) return 19; /* Two intervals without a completed frame. */
    now += 46875000; trace_tick();
    if (snapshots != 1) return 20; /* No per-second snapshot flood. */
    ps2_runtime_frame(0,0,0); now += 46875000; trace_tick();
    if (sIdleSamples) return 21; /* Frame progress resets the stall detector. */
    interrupts=0; ps2_runtime_event("already disabled"); if (interrupts) return 5;
    interrupts=1;
    sCount=sFirst=0; partialWrites=0; writeCalls=largestWrite=0;
    for (int i=0;i<50;i++) ps2_runtime_event("BULK id=%u %s",i,
        "012345678901234567890123456789012345678901234567890123456789012345"
        "012345678901234567890123456789012345678901234567890123456789012345");
    flush_records();
    if (writeCalls!=2 || largestWrite!=4096 || sBytes%512 ||
        !strstr(read_log(),"BULK id=49 ")) return 22;
    for (unsigned i=0;i<RECORDS+72;i++) ps2_runtime_event("overflow id=%u",i);
    if (sCount!=RECORDS || sLost<72) return 6;
    flush_records();
    log=read_log();
    char last[64]; snprintf(last,sizeof(last),"overflow id=%u\n",RECORDS+71);
    if (!strstr(log,last) || strstr(log,"overflow id=0\n")) return 7;
    failWrite=1; flush_records();
    if (sRecording || !interrupts) return 8; /* USB error must not block the game. */
    failWrite=0; sCount=sFirst=0;
    ps2_runtime_trace_init(argv[1]);
    sBytes=FILE_LIMIT-512;
    fd=open(sPath,O_WRONLY); if (ftruncate(fd,sBytes)) return 17; close(fd);
    ps2_runtime_event("this record does not fit the remaining file budget 0123456789");
    flush_records();
    if (sRecording || !interrupts) return 9;
    fd=open(sPath,O_RDONLY);
    if (lseek(fd,0,SEEK_END)!=FILE_LIMIT) return 15;
    char footer[512]; lseek(fd,-512,SEEK_END);
    if (read(fd,footer,512)!=512) return 18;
    close(fd);
    if (strncmp(footer,"LOG_LIMIT gameContinues=1\n",25)) return 16;
    sCount=sFirst=0; ps2_runtime_trace_init(argv[1]);
    trace_thread(NULL);
    if (sRecording || !strstr(read_log(),"LOGGER_ENTER wake=vblank") ||
        !strstr(read_log(),"ERROR logger wait failed")) return 12;
    sCount=sFirst=0; sThread=sWake=-1; failSema=1;
    ps2_runtime_trace_init(argv[1]); ps2_runtime_trace_start();
    if (sRecording || !strstr(read_log(),"ERROR logger semaphore")) return 13;
    puts("PS2 runtime logger: PASS aligned 4 KiB transfers, padding, partial writes, exact disk cap, batching, stats, overflow, IRQ preservation, vblank wake, stalled clock and wait failures");
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
        for automatic in (False, True):
            flags=['-DPS2_RUNTIME_AUTOMATIC=1'] if automatic else []
            subprocess.run(['cc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-DPS2_RUNTIME_DIAGNOSTIC=1',
                *flags, '-I', str(root), '-Iinclude/ps2', str(root / 'driver.c'), '-o', str(executable)], check=True)
            subprocess.run([str(executable), str(root / 'OOT.Z64')], check=True)

if __name__ == '__main__':
    main()
