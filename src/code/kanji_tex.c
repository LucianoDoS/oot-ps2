/* Native equivalent documented in kanji_tex.s; table transcribed from that file. */
#include "kanji_tex.h"
#include "font.h"

static const s16 low_codepoints_lookup_table[] = {
#include "kanji_tex_lookup.inc.c"
};

s32 KanjiTex_CodepointToOffset(s32 codepoint) {
    s32 i = (codepoint & 0xFF) - 0x40;
    if (i >= 0x40) i--;
    if (codepoint < 0x8800) {
        i += ((codepoint >> 8) - 0x81) * 0xBC;
        if (i < 0 || i >= (s32)(sizeof(low_codepoints_lookup_table) / sizeof(s16))) return 0;
        return low_codepoints_lookup_table[i] * FONT_CHAR_TEX_SIZE;
    }
    i += 0x30A + ((codepoint >> 8) - 0x88) * 0xBC;
    return i * FONT_CHAR_TEX_SIZE;
}
