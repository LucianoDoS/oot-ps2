/* Browser metadata is independent of the alternating SRAM files. */
#include "ps2_save_icon.h"
#ifdef PS2_HAS_SAVE_ICON
#include <libmc.h>
#define NEWLIB_PORT_AWARE
#include <io_common.h>
#include <stdio.h>
#include <string.h>

static u8 s_compare[4096] __attribute__((aligned(64)));

static int wait_result(int submitted) {
    int result;
    if (submitted < 0 || mcSync(MC_WAIT, NULL, &result) < 0) return -1;
    return result;
}

static int file_matches(int port, int slot, const char* path, const u8* data, unsigned size) {
    int fd = wait_result(mcOpen(port, slot, path, FIO_O_RDONLY));
    if (fd < 0) return 0;
    int matches = wait_result(mcSeek(fd, 0, SEEK_END)) == (int)size;
    if (matches && wait_result(mcSeek(fd, 0, SEEK_SET)) < 0) matches = 0;
    for (unsigned offset = 0; matches && offset < size;) {
        unsigned count = size - offset;
        if (count > sizeof(s_compare)) count = sizeof(s_compare);
        if (wait_result(mcRead(fd, s_compare, count)) != (int)count ||
            memcmp(s_compare, data + offset, count)) matches = 0;
        offset += count;
    }
    if (wait_result(mcClose(fd)) < 0) matches = 0;
    return matches;
}

static int ensure_file(int port, int slot, const char* path, const u8* data, unsigned size) {
    if (file_matches(port, slot, path, data, size)) return 0;
    int fd = wait_result(mcOpen(port, slot, path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC));
    if (fd < 0) return -1;
    int written = wait_result(mcWrite(fd, data, size));
    int closed = wait_result(mcClose(fd));
    if (written != (int)size || closed < 0 || !file_matches(port, slot, path, data, size)) return -1;
    return 1;
}

int ps2_save_icon_install(int port, int slot, const char* directory) {
    char model[96], metadata[96];
    int length = snprintf(model, sizeof(model), "/%s/triforce.icn", directory);
    if (length < 0 || length >= (int)sizeof(model)) return -1;
    length = snprintf(metadata, sizeof(metadata), "/%s/icon.sys", directory);
    if (length < 0 || length >= (int)sizeof(metadata)) return -1;
    /* Publish the reference only after its complete model has been verified. */
    int changed = ensure_file(port, slot, model, ps2_save_icon_model, ps2_save_icon_model_size);
    if (changed < 0) return -1;
    int result = ensure_file(port, slot, metadata, ps2_save_icon_sys, ps2_save_icon_sys_size);
    return result < 0 ? -1 : changed || result;
}
#else
int ps2_save_icon_install(int port, int slot, const char* directory) {
    (void)port; (void)slot; (void)directory;
    return 0;
}
#endif
