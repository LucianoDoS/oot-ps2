#ifndef PS2_SAVE_ICON_H
#define PS2_SAVE_ICON_H
#include "ps2_types.h"
extern const u8 ps2_save_icon_sys[], ps2_save_icon_model[];
extern const unsigned int ps2_save_icon_sys_size, ps2_save_icon_model_size;
/* 0 means unchanged, 1 installed/repaired, -1 failed. SRAM remains independent. */
int ps2_save_icon_install(int port, int slot, const char* directory);
#endif
