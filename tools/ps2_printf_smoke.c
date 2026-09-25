#include "ultra64.h"
#include "string.h"

static void* collect(void* context, const char* text, size_t length) {
    memcpy(context, text, length);
    return (char*)context + length;
}

static void* reject(void* context, const char* text, size_t length) {
    return NULL;
}

static int format(PrintCallback callback, void* context, const char* text, ...) {
    va_list args;
    va_start(args, text);
    int result = _Printf(callback, context, text, args);
    va_end(args);
    return result;
}

int ps2_printf_smoke(void) {
    char buffer[704];
    memset(buffer, 0x5A, sizeof(buffer));
    const char* expected = "-42 002a 1.25 ok";
    int count = format(collect, buffer, "%d %04x %.2f %s", -42, 42, 1.25, "ok");
    if (count != 16 || buffer[16] != 0x5A) return 1;
    for (int i = 0; i < 16; i++) if (buffer[i] != expected[i]) return 1;
    count = format(collect, buffer, "%0700d", 7);
    if (count != 700 || buffer[699] != '7' || buffer[700] != 0x5A) return 2;
    for (int i = 0; i < 699; i++) if (buffer[i] != '0') return 3;
    if (format(reject, buffer, "%s", "failure") != -1) return 4;
    if (format(collect, buffer, "%s", "") != 0) return 5;
    return 0;
}
