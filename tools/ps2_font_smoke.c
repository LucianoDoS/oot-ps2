/* Link the game's actual font loader and supply a small big-endian ROM.
 * NTSC English names use Latin glyphs in the ordered SHIFT-JIS font. */
#include "font.h"
#include "dma.h"
#include "kanji_tex.h"
#include "string.h"
extern int printf(const char*, ...);

__asm__(".globl _jpn_message_data_staticSegmentStart\n"
        ".set _jpn_message_data_staticSegmentStart, 0x08000000\n"
        ".globl _message_0xFFFC_jpn\n.set _message_0xFFFC_jpn, 0x08000000\n"
        ".globl _message_0xFFFD_jpn\n.set _message_0xFFFD_jpn, 0x0800000c\n"
        ".globl _jpn_message_data_staticSegmentRomStart\n"
        ".set _jpn_message_data_staticSegmentRomStart, 0x1000\n"
        ".globl _kanjiSegmentRomStart\n.set _kanjiSegmentRomStart, 0x2000\n");

static int mode, glyphs, failures;
static const u16 letters[] = {0x824f, 0x8260, 0x8279, 0x8281}; /* 0 A Z a */

s32 DmaMgr_RequestSync(void* ram, uintptr_t vrom, size_t size) {
    if (vrom == 0x1000) {
        const u8 message[] = {0x82,0x4f, 0,0x0a, 0x82,0x60, 0x82,0x79, 0x82,0x81, 0x81,0x70};
        if (size != sizeof(message)) failures++;
        memcpy(ram, message, sizeof(message));
        if (mode) {
            /* Missing terminator: must stop at the declared length. */
            u8* bytes = ram;
            for (size_t i = 0; i < size; i += 2) { bytes[i] = 0; bytes[i + 1] = 0x0a; }
        }
    } else {
        if (glyphs >= 4 || vrom != 0x2000u + (uintptr_t)KanjiTex_CodepointToOffset(letters[glyphs])) failures++;
        if (size != FONT_CHAR_TEX_SIZE) failures++;
        memset(ram, ++glyphs, size);
    }
    return 0;
}

int ps2_font_smoke(void) {
    static Font font;
    memset(&font, 0x5a, sizeof(font));
    Font_LoadOrderedFont(&font);
    if (glyphs != 4 || font.msgBufWide[0] != 0x824f || font.msgBufWide[2] != 0x8260 ||
        font.msgBufWide[5] != 0x8170) failures++;
    for (int i = 0; i < 4 * FONT_CHAR_TEX_SIZE; i++)
        if (font.fontBuf[i] != i / FONT_CHAR_TEX_SIZE + 1) failures++;
    if (font.fontBuf[4 * FONT_CHAR_TEX_SIZE] != 0x5a) failures++;
    mode = 1;
    glyphs = 0;
    Font_LoadOrderedFont(&font);
    if (glyphs != 0) failures++;
    printf("[PS2 FONT TEST] %s: native ordered Latin glyphs, newline and bounded message (%d)\n",
           failures ? "FAIL" : "PASS", failures);
    return failures;
}
