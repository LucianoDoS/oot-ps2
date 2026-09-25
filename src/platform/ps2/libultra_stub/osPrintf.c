/* Adapt libultra's output callback to the SDK's native formatter. */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

void osSyncPrintfUnused(const char* format, ...) {
#if DEBUG_FEATURES
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
#endif
}

void rmonPrintf(const char* format, ...) {
#if DEBUG_FEATURES
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
#endif
}

#if DEBUG_FEATURES
void isPrintfInit(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
}

void* is_proutSyncPrintf(void* context, const char* text, size_t length) {
    return fwrite(text, 1, length, stdout) == length ? (void*)1 : NULL;
}
#endif

int _Printf(void* (*output)(void*, const char*, size_t), void* context,
            const char* format, va_list args) {
    char local[512];
    va_list copy;
    va_copy(copy, args);
    int length = vsnprintf(local, sizeof(local), format, copy);
    va_end(copy);
    if (length < 0) return length;
    char* buffer = local;
    if ((size_t)length >= sizeof(local)) {
        buffer = malloc((size_t)length + 1);
        if (buffer == NULL) return -1;
        va_copy(copy, args);
        int written = vsnprintf(buffer, (size_t)length + 1, format, copy);
        va_end(copy);
        if (written != length) {
            free(buffer);
            return -1;
        }
    }
    int result = length;
    if (length != 0 && output(context, buffer, (size_t)length) == NULL) result = -1;
    if (buffer != local) free(buffer);
    return result;
}
