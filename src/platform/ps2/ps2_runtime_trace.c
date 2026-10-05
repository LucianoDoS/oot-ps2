/* Optional USB logger. Producers touch RAM only; one periodic EE
 * thread appends and closes batches. Never call file I/O from an ISR. */
#include <kernel.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "ps2_runtime_trace.h"

#define RECORDS 128u
#define RECORD_BYTES 320u
#define FILE_LIMIT (2u * 1024u * 1024u)
#define TICKS_PER_SECOND 46875000ull
extern Ps2TraceTicks osGetTime(void);
extern void ps2_rom_cache_stats(unsigned*, unsigned*);
extern volatile unsigned gPS2DspCyclesTotal;
extern void* _gp;
static char sRecords[RECORDS][RECORD_BYTES], sPath[256];
static char sBatch[RECORD_BYTES * 32];
static unsigned char sStack[16384] __attribute__((aligned(64)));
static unsigned sFirst, sCount, sLost, sBytes, sSequence;
static int sRecording, sClockReady, sThread = -1;
static Ps2TraceTicks sOrigin, sLastSample, sLastFlush;
static unsigned sDspPrevious;
typedef struct {
    unsigned frames, reads, bytes, seeks, errors, polls, empty, queueMin, queueMax;
    unsigned late, dropped, audioRate;
    Ps2TraceTicks translate, submit, flip, io, wait, worstRead;
} TraceStats;
static TraceStats sStats;

static void unlock(int previous) { if (previous) EIntr(); }
static unsigned millis(Ps2TraceTicks ticks) { return ticks * 1000 / TICKS_PER_SECOND; }

void ps2_runtime_event(const char* format, ...) {
    if (!sRecording) return;
    char record[RECORD_BYTES];
    unsigned at = sClockReady ? millis(osGetTime() - sOrigin) : 0;
    int prefix = snprintf(record, sizeof(record), "tMs=%u ", at);
    va_list args;
    va_start(args, format);
    vsnprintf(record + prefix, sizeof(record) - prefix, format, args);
    va_end(args);
    unsigned length = strcspn(record, "\r\n");
    if (length > sizeof(record) - 2) length = sizeof(record) - 2;
    record[length++] = '\n';
    record[length] = 0;
    int previous = DIntr();
    if (sCount == RECORDS) { sFirst = (sFirst + 1) % RECORDS; sCount--; sLost++; }
    memcpy(sRecords[(sFirst + sCount++) % RECORDS], record, length + 1);
    unlock(previous);
}

static int write_all(int fd, const char* text, unsigned bytes) {
    while (bytes) {
        int wrote = write(fd, text, bytes);
        if (wrote <= 0 || (unsigned)wrote > bytes) return 0;
        bytes -= wrote; text += wrote;
    }
    return 1;
}

static void flush_records(void) {
    if (!sRecording || !sCount) return;
    Ps2TraceTicks begin = sClockReady ? osGetTime() : 0;
    unsigned total = 0;
    int fd = open(sPath, O_WRONLY | O_APPEND);
    if (fd < 0) goto failed;
    int previous = DIntr();
    unsigned remaining = sCount;
    unlock(previous);
    while (remaining) {
        unsigned bytes = 0;
        previous = DIntr();
        while (sCount && remaining) {
            unsigned length = strlen(sRecords[sFirst]);
            if (bytes + length > sizeof(sBatch)) break;
            if (sBytes + total + bytes + length > FILE_LIMIT - 32) break;
            memcpy(sBatch + bytes, sRecords[sFirst], length);
            bytes += length;
            sFirst = (sFirst + 1) % RECORDS; sCount--; remaining--;
        }
        unlock(previous);
        if (!bytes) break;
        if (!write_all(fd, sBatch, bytes)) { close(fd); goto failed; }
        total += bytes;
    }
    int limited = sCount && sBytes + total + RECORD_BYTES > FILE_LIMIT - 32;
    if (limited) {
        const char footer[] = "LOG_LIMIT gameContinues=1\n";
        if (sBytes + total + sizeof(footer) - 1 <= FILE_LIMIT) {
            if (!write_all(fd, footer, sizeof(footer) - 1)) { close(fd); goto failed; }
            total += sizeof(footer) - 1;
        }
    }
    if (close(fd) < 0) goto failed;
    sBytes += total;
    if (limited) {
        sRecording = 0;
        printf("[PS2 RUNTIME LOG] LIMIT reached; logging stopped, game continues\n");
        return;
    }
    /* Report the logger's own disturbance in the NEXT batch. */
    ps2_runtime_event("FLUSH bytes=%u durationMs=%u fileBytes=%u lostRecords=%u",
        total, sClockReady ? millis(osGetTime() - begin) : 0, sBytes, sLost);
    return;
failed:
    sRecording = 0;
    printf("[PS2 RUNTIME LOG] ERROR writing %s; game continues\n", sPath);
}

static void trace_tick(void) {
    Ps2TraceTicks now = osGetTime();
    unsigned elapsed = millis(now - sLastSample);
    if (!sRecording || elapsed < 1000) return;
    int previous = DIntr();
    TraceStats stats = sStats;
    memset(&sStats, 0, sizeof(sStats));
    sStats.queueMin = ~0u;
    /* Late/dropped are monotonic driver counters, not interval counts. */
    sStats.late = stats.late; sStats.dropped = stats.dropped;
    sStats.audioRate = stats.audioRate;
    unlock(previous);
    unsigned hits, reads, dsp = gPS2DspCyclesTotal;
    ps2_rom_cache_stats(&hits, &reads);
    ps2_runtime_event("SAMPLE seq=%u intervalMs=%u frames=%u fps10=%u gfxMs=%u,%u,%u dspCycles=%u lost=%u",
        ++sSequence, elapsed, stats.frames, stats.frames * 10000 / elapsed,
        millis(stats.translate), millis(stats.submit), millis(stats.flip), dsp - sDspPrevious, sLost);
    ps2_runtime_event("USB reads=%u bytes=%u seeks=%u errors=%u ioMs=%u waitMs=%u worstReadMs=%u cacheHits=%u cacheReads=%u",
        stats.reads, stats.bytes, stats.seeks, stats.errors, millis(stats.io), millis(stats.wait),
        millis(stats.worstRead), hits, reads);
    ps2_runtime_event("AUDIO queueBytesMin=%u queueBytesMax=%u polls=%u zeroPolls=%u outputHz=%u lateTotal=%u droppedTotal=%u",
        stats.polls ? stats.queueMin : 0, stats.queueMax, stats.polls, stats.empty,
        stats.audioRate, stats.late, stats.dropped);
    sDspPrevious = dsp; sLastSample = now;
    if (now - sLastFlush >= 5 * TICKS_PER_SECOND) {
        flush_records(); sLastFlush = osGetTime();
    }
}

static void trace_thread(void* unused) {
    (void)unused;
    while (sRecording) { usleep(1000000); trace_tick(); }
    ExitThread();
}

void ps2_runtime_trace_init(const char* dataPath) {
    if (!dataPath) return;
    const char* slash = strrchr(dataPath, '/');
    if (!slash) slash = strchr(dataPath, ':');
    unsigned prefix = slash ? (unsigned)(slash - dataPath + 1) : 0;
    if (prefix + sizeof("DIAG.LOG") > sizeof(sPath)) return;
    memcpy(sPath, dataPath, prefix);
    memcpy(sPath + prefix, "DIAG.LOG", sizeof("DIAG.LOG"));
    const char header[] = "OOT PS2 runtime log v1 " __DATE__ " " __TIME__ "\n"
        "Units: wall milliseconds; gfxMs=translation,GS submission,presentation.\n"
        "fps10=displayed FPS x10. USB/read timings include thread preemption.\n"
        "Queue sampled by audio thread; zeroPolls is not an exact IOP underrun count.\n"
        "FLUSH records measure logger I/O overhead. Max file 2 MiB; batches every 5 seconds.\n";
    int fd = open(sPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) { printf("[PS2 RUNTIME LOG] ERROR creating %s\n", sPath); return; }
    int okay = write_all(fd, header, sizeof(header) - 1);
    int closed = close(fd);
    if (!okay || closed < 0) { printf("[PS2 RUNTIME LOG] ERROR initializing %s\n", sPath); return; }
    sBytes = sizeof(header) - 1;
    sRecording = 1; sStats.queueMin = ~0u;
    printf("[PS2 RUNTIME LOG] ACTIVE %s\n", sPath);
}

void ps2_runtime_trace_start(void) {
    if (!sRecording || sThread >= 0) return;
    sOrigin = sLastSample = sLastFlush = osGetTime(); sClockReady = 1;
    ee_thread_t thread;
    memset(&thread, 0, sizeof(thread));
    thread.func = trace_thread; thread.stack = sStack; thread.stack_size = sizeof(sStack);
    /* Below VI/DMA/scheduler, above a graphics worker spinning on vsync:
     * otherwise that worker can starve logging indefinitely. */
    thread.gp_reg = &_gp; thread.initial_priority = 51;
    sThread = CreateThread(&thread);
    ps2_runtime_event("LOGGER_START priority=51 batchSeconds=5 reserveRamBytes=%u", (unsigned)(sizeof(sRecords) + sizeof(sBatch) + sizeof(sStack)));
    if (sThread < 0 || StartThread(sThread, NULL) < 0) {
        ps2_runtime_event("ERROR logger thread could not start");
        flush_records(); sRecording = 0;
        if (sThread >= 0) DeleteThread(sThread);
        sThread = -1;
    }
}

void ps2_runtime_frame(Ps2TraceTicks translate, Ps2TraceTicks submit, Ps2TraceTicks flip) {
    if (!sRecording) return;
    int previous = DIntr();
    sStats.frames++; sStats.translate += translate; sStats.submit += submit; sStats.flip += flip;
    unlock(previous);
}
void ps2_runtime_rom_read(unsigned offset, unsigned bytes, Ps2TraceTicks ticks, int result, int seek) {
    if (!sRecording) return;
    int previous = DIntr();
    sStats.reads++; sStats.bytes += result == 0 ? bytes : 0;
    sStats.seeks += seek != 0; sStats.errors += result != 0; sStats.io += ticks;
    if (ticks > sStats.worstRead) sStats.worstRead = ticks;
    unlock(previous);
    if (result || millis(ticks) >= 50)
        ps2_runtime_event("ROM_READ offset=%08x bytes=%u elapsedMs=%u result=%d seek=%d", offset, bytes, millis(ticks), result, seek);
}
void ps2_runtime_rom_wait(Ps2TraceTicks ticks) {
    if (!sRecording) return;
    int previous = DIntr(); sStats.wait += ticks; unlock(previous);
}
void ps2_runtime_audio(unsigned queue, unsigned rate, unsigned late, unsigned dropped) {
    if (!sRecording) return;
    int previous = DIntr();
    sStats.polls++; sStats.empty += queue == 0;
    if (queue < sStats.queueMin) sStats.queueMin = queue;
    if (queue > sStats.queueMax) sStats.queueMax = queue;
    sStats.audioRate = rate; sStats.late = late; sStats.dropped = dropped;
    unlock(previous);
}

/* Capture relevant EE diagnostics without redirecting every game PRINTF
 * to USB. Formatting and append stay outside interrupt context. */
int __wrap_printf(const char* format, ...) {
    va_list args, copy;
    va_start(args, format);
    if (sRecording && (strncmp(format, "[PS2", 4) == 0 || strncmp(format, "[osRom]", 7) == 0 ||
        strncmp(format, "[Audio]", 7) == 0 || strncmp(format, "[OoT-PS2]", 9) == 0)) {
        char message[RECORD_BYTES];
        va_copy(copy, args); vsnprintf(message, sizeof(message), format, copy); va_end(copy);
        ps2_runtime_event("%s", message);
    }
    int result = vprintf(format, args);
    va_end(args);
    return result;
}
