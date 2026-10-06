#ifndef PS2_LOCALE_H
#define PS2_LOCALE_H
#include "ultra64/ultratypes.h"

enum { PS2_LANGUAGE_ENGLISH, PS2_LANGUAGE_SPANISH, PS2_LANGUAGE_COUNT };
const char* ps2_rom_path(void);
void ps2_locale_init(const char* romPath);
s32 ps2_locale_available(void);
s32 ps2_locale_language(void);
void ps2_locale_set_language(s32 language);
s32 ps2_locale_message(u16 id, void* destination, u32 capacity, u8* typePos, u32* length);
s32 ps2_locale_read(u32 vrom, void* destination, u32 size);
void ps2_locale_overlay(u32 vrom, void* destination, u32 size);
f32 ps2_locale_char_width(u8 character, f32 original);
const u8* ps2_locale_label(unsigned index);
#endif
