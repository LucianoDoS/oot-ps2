#ifndef PS2_FPS_METER_H
#define PS2_FPS_METER_H
#include <stdbool.h>
#include <stdint.h>
#define PS2_FPS_UNKNOWN (~0u)
#define PS2_FPS_TICKS_PER_SECOND 46875000ull
typedef struct {
    uint64_t start;
    unsigned retraceStart, frames, fps10;
    bool started, fallback;
} PS2FpsMeter;
/* Count presented frames, independently of disk logging. A retrace fallback
 * is explicitly marked if the EE clock stops advancing. Unsigned retrace
 * subtraction also handles wraparound. The first frame starts the window. */
static inline bool ps2_fps_meter_frame(PS2FpsMeter* meter, uint64_t now, unsigned retraces) {
    if (!meter->started) {
        meter->started = true; meter->start = now; meter->retraceStart = retraces;
        meter->fps10 = PS2_FPS_UNKNOWN; return false;
    }
    meter->frames++;
    uint64_t elapsed = now - meter->start;
    unsigned blanks = retraces - meter->retraceStart;
    if (elapsed < PS2_FPS_TICKS_PER_SECOND && blanks < 60) return false;
    uint64_t retraceTicks = (uint64_t)blanks * (PS2_FPS_TICKS_PER_SECOND / 60);
    meter->fallback = blanks >= 60 && elapsed < retraceTicks / 2;
    if (meter->fallback) elapsed = retraceTicks;
    meter->fps10 = (uint64_t)meter->frames * (10 * PS2_FPS_TICKS_PER_SECOND) / elapsed;
    meter->frames = 0; meter->start = now; meter->retraceStart = retraces;
    return true;
}
#endif
