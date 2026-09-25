/* Compiled with the game's actual types, and run on the EE. */
#include "ultra64.h"

int ps2_math_smoke(void) {
    Mtx matrix;
    f32 source[4][4] = {
        { 1.0f, -2.5f, 0.25f, 0.0f },
        { -0.125f, 3.75f, 16.0f, -16.0f },
        { 0.5f, 0.0f, 1.0f, -1.0f },
        { 100.25f, -200.5f, 0.0f, 1.0f },
    };
    f32 result[4][4];
    guMtxF2L(source, &matrix);
    // Check storage independently of the inverse (two wrong functions can round-trip).
    if (matrix.intPart[0][0] != 1 || matrix.intPart[0][1] != (u16)-3 ||
        matrix.fracPart[0][1] != 0x8000 || matrix.fracPart[0][2] != 0x4000) {
        return 1;
    }
    guMtxL2F(result, &matrix);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (source[i][j] != result[i][j]) return 2;
        }
    }
    if (sins(0) != 0 || sins(0x4000) != 32767 || sins(0xC000) != -32767 ||
        coss(0) != 32767 || coss(0x8000) != -32767) return 3;
    return 0;
}
