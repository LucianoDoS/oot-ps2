/* Update browser assets on an existing formatted card. Never write SRAM or
 * format a card; compare both complete save files before and after installation. */
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <libmc.h>
#define NEWLIB_PORT_AWARE
#include <io_common.h>
#include <stdio.h>
#include <string.h>
#include "ps2_save_icon.h"

static const char* s_paths[] = {"/OOT_PS2/save.dat", "/OOT_PS2/save.bak"};
static u8 s_before[2][0x8040] __attribute__((aligned(64)));
static u8 s_after[0x8040] __attribute__((aligned(64)));
static sceMcTblGetDir s_directory[16] __attribute__((aligned(64)));

static int wait_result(int submitted) {
    int result;
    if (submitted < 0 || mcSync(MC_WAIT, NULL, &result) < 0) return -1;
    return result;
}

static int read_save(const char* path, u8* data) {
    int fd = wait_result(mcOpen(0, 0, path, FIO_O_RDONLY));
    if (fd == -4) return 0; /* No file; it must remain absent. */
    if (fd < 0) return -1;
    int size = wait_result(mcSeek(fd, 0, SEEK_END));
    int bytes = -1;
    if (size > 0 && size < (int)sizeof(s_after) && wait_result(mcSeek(fd, 0, SEEK_SET)) >= 0)
        bytes = wait_result(mcRead(fd, data, size));
    int closed = wait_result(mcClose(fd));
    return closed < 0 || bytes != size ? -1 : bytes;
}

static void list_directory(const char* path) {
    memset(s_directory, 0, sizeof(s_directory));
    int count = wait_result(mcGetDir(0, 0, path, 0, 16, s_directory));
    for (int i = 0; i < count && i < 16; i++) {
        printf("[PS2 ICON DIRECTORY] %s name=%.*s size=%u attributes=%04x\n", path, 32,
               s_directory[i].EntryName, (unsigned)s_directory[i].FileSizeByte,
               s_directory[i].AttrFile);
    }
}

static void finish(int failed, const char* message) {
    printf("[PS2 ICON INSTALL] %s: %s\n", failed ? "FAIL" : "PASS", message);
    fflush(stdout);
    SleepThread();
}

int main(void) {
    SifInitRpc(0);
    while (!SifIopReset("", 0)) {}
    while (!SifIopSync()) {}
    SifInitRpc(0);
    SifLoadFileInit();
    if (SifLoadModule("rom0:SIO2MAN", 0, NULL) < 0 ||
        SifLoadModule("rom0:MCMAN", 0, NULL) < 0 ||
        SifLoadModule("rom0:MCSERV", 0, NULL) < 0 || mcInit(MC_TYPE_MC) < 0) {
        finish(1, "IOP modules"); return 1;
    }
    int type = 0, space = 0, formatted = 0;
    if (wait_result(mcGetInfo(0, 0, &type, &space, &formatted)) < -1 ||
        type != MC_TYPE_PS2 || !formatted) {
        finish(1, "An existing formatted PS2 card is required"); return 1;
    }
    list_directory("/*");
    int sizes[2];
    for (unsigned i = 0; i < 2; i++) {
        sizes[i] = read_save(s_paths[i], s_before[i]);
        if (sizes[i] < 0) { finish(1, "Cannot snapshot the save files"); return 1; }
    }
    if (!sizes[0] && !sizes[1]) { finish(1, "Existing OoT save missing"); return 1; }
    if (ps2_save_icon_install(0, 0, "OOT_PS2") < 0) { finish(1, "Icon installation/readback"); return 1; }
    list_directory("/OOT_PS2/*");
    for (unsigned i = 0; i < 2; i++) {
        int size = read_save(s_paths[i], s_after);
        if (size != sizes[i] || memcmp(s_before[i], s_after, sizes[i])) {
            finish(1, "Save files changed"); return 1;
        }
    }
    finish(0, "Browser icon/title verified; both complete save files unchanged");
    return 0;
}
