#ifndef PS2_RUNTIME_TRACE_H
#define PS2_RUNTIME_TRACE_H
#ifdef PS2_RUNTIME_DIAGNOSTIC
typedef unsigned long long Ps2TraceTicks;
void ps2_runtime_trace_init(const char* dataPath);
void ps2_runtime_trace_start(void);
/* Request a final flush on the logger thread; the game keeps running. */
void ps2_runtime_trace_finish(void);
void ps2_runtime_vblank(void);
void ps2_runtime_event(const char* format, ...);
void ps2_runtime_frame(Ps2TraceTicks translate, Ps2TraceTicks submit, Ps2TraceTicks flip);
void ps2_runtime_rom_read(unsigned offset, unsigned bytes, Ps2TraceTicks ticks, int result, int seek);
void ps2_runtime_rom_wait(Ps2TraceTicks ticks);
void ps2_runtime_audio(unsigned queue, unsigned rate, unsigned late, unsigned dropped);
#else
#define ps2_runtime_trace_init(...) ((void)0)
#define ps2_runtime_trace_start(...) ((void)0)
#define ps2_runtime_trace_finish(...) ((void)0)
#define ps2_runtime_vblank(...) ((void)0)
#define ps2_runtime_event(...) ((void)0)
#define ps2_runtime_frame(...) ((void)0)
#define ps2_runtime_rom_read(...) ((void)0)
#define ps2_runtime_rom_wait(...) ((void)0)
#define ps2_runtime_audio(...) ((void)0)
#endif
#endif
