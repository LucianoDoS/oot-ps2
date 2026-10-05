#ifndef PS2_BOOT_TRACE_H
#define PS2_BOOT_TRACE_H

/* USB game variant only. Screen output stops before gsKit owns the GS.
 * File records are bounded startup milestones, never a per-frame logger. */
void ps2_boot_trace_init(void);
void ps2_boot_trace_stage(const char* message);
void ps2_boot_trace_result(const char* name, int result);
void ps2_boot_trace_file_ready(const char* data_path);
void ps2_boot_trace_screen_off(void);
void ps2_boot_trace_game_message(const char* message);

#endif
