/* Standalone real libmc test, isolated from game saves by PS2_SAVE_DIR.
 * Formatting requires an explicit host token and an unformatted test card. */
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <libmc.h>
#define NEWLIB_PORT_AWARE
#include <io_common.h>
#include <stdio.h>
#include <string.h>
#include "ps2_types.h"
#include "ps2_save_icon.h"
extern void ps2_save_init(void);
extern int ps2_save_read(void*, u32);
extern int ps2_save_write(const void*, u32);
static u8 expected[0x8000], recovered[0x8000];
static u8 iconBuffer[0x10000] __attribute__((aligned(64)));

static int wait_result(int submitted) {
    int result;
    if (submitted < 0 || mcSync(MC_WAIT, NULL, &result) < 0) return -1;
    return result;
}
static void finish(int failed, const char* what) {
    printf("[PS2 CARD TEST] %s: %s\n", failed ? "FAIL" : "PASS", what);
    fflush(stdout);
    SleepThread();
}
static int check_icon_file(const char* path, const u8* expectedData, unsigned expectedSize) {
    int fd = wait_result(mcOpen(0, 0, path, FIO_O_RDONLY));
    if (fd < 0 || expectedSize > sizeof(iconBuffer)) return -1;
    memset(iconBuffer, 0, sizeof(iconBuffer));
    int bytes = wait_result(mcRead(fd, iconBuffer, sizeof(iconBuffer)));
    int closed = wait_result(mcClose(fd));
    return closed < 0 || bytes != (int)expectedSize || memcmp(iconBuffer, expectedData, expectedSize) ? -1 : 0;
}
static int check_icons(void) {
    if (check_icon_file("/OOT_PS2_TEST/icon.sys", ps2_save_icon_sys, ps2_save_icon_sys_size) ||
        check_icon_file("/OOT_PS2_TEST/triforce.icn", ps2_save_icon_model, ps2_save_icon_model_size) ||
        ps2_save_icon_install(0, 0, "OOT_PS2_TEST") != 0) return -1;
    printf("[PS2 CARD TEST] Browser files read back; unchanged installation skips writes\n");
    return 0;
}
static int check_icon_repair(void) {
    int fd = wait_result(mcOpen(0, 0, "/OOT_PS2_TEST/triforce.icn", FIO_O_WRONLY | FIO_O_TRUNC));
    if (fd < 0) return -1;
    int written = wait_result(mcWrite(fd, "bad", 3));
    int closed = wait_result(mcClose(fd));
    if (written != 3 || closed < 0 || ps2_save_icon_install(0, 0, "OOT_PS2_TEST") != 1 || check_icons()) return -1;
    if (wait_result(mcDelete(0, 0, "/OOT_PS2_TEST/icon.sys")) < 0 ||
        ps2_save_icon_install(0, 0, "OOT_PS2_TEST") != 1 || check_icons()) return -1;
    /* A metadata failure must not invalidate the already committed SRAM. */
    if (ps2_save_icon_install(0, 0, "DIRECTORY_THAT_DOES_NOT_EXIST") != -1 ||
        ps2_save_read(recovered, sizeof(recovered)) || memcmp(expected, recovered, sizeof(expected))) return -1;
    printf("[PS2 CARD TEST] Corrupt model and missing icon.sys repaired; SRAM unchanged after icon failure\n");
    return 0;
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
    wait_result(mcGetInfo(0, 0, &type, &space, &formatted));
    if (type != MC_TYPE_PS2) { finish(1, "PS2 card missing"); return 1; }
    if (!formatted) {
        char token[40] = {0};
        FILE* permission = fopen("host:ps2-memorycard-format.allow", "rb");
        if (permission) { fread(token, 1, sizeof(token) - 1, permission); fclose(permission); }
        if (strcmp(token, "OOT_PS2_NEW_TEST_CARD\n") || wait_result(mcFormat(0, 0)) < 0) {
            finish(1, "format requires new-test-card token"); return 1;
        }
        printf("[PS2 CARD TEST] Formatted dedicated test card\n");
    }
    for (u32 i = 0; i < sizeof(expected); i++) expected[i] = (i * 17u + (i >> 8)) & 255;
    ps2_save_init();
    if (check_icons()) { finish(1, "browser icon/title readback"); return 1; }
    if (ps2_save_read(recovered, sizeof(recovered)) == 0) {
        if (memcmp(expected, recovered, sizeof(expected))) {
            finish(1, "cold restart image differs"); return 1;
        }
        if (check_icon_repair()) { finish(1, "icon repair must preserve saved image"); return 1; }
        finish(0, "cold restart: 32768 bytes and 3D browser icon persisted and verified");
        return 0;
    }
    if (ps2_save_write(expected, sizeof(expected))) { finish(1, "first image write"); return 1; }
    expected[123] ^= 0x55;
    if (ps2_save_write(expected, sizeof(expected))) { finish(1, "alternate image write"); return 1; }
    ps2_save_init();
    if (ps2_save_read(recovered, sizeof(recovered)) || memcmp(expected, recovered, sizeof(expected))) {
        finish(1, "latest revision reload"); return 1;
    }
    /* Corrupt the newer copy in this diagnostic-only directory. */
    int fd = wait_result(mcOpen(0, 0, "/OOT_PS2_TEST/save.bak", FIO_O_WRONLY | FIO_O_TRUNC));
    if (fd < 0) { finish(1, "open test copy"); return 1; }
    int bytes = wait_result(mcWrite(fd, "bad", 3));
    int closed = wait_result(mcClose(fd));
    expected[123] ^= 0x55;
    ps2_save_init();
    if (bytes != 3 || closed < 0 || ps2_save_read(recovered, sizeof(recovered)) ||
        memcmp(expected, recovered, sizeof(expected))) { finish(1, "corrupt-copy recovery"); return 1; }
    if (check_icon_repair()) { finish(1, "icon repair must preserve saved image"); return 1; }
    finish(0, "real card: alternating writes, latest reload and corrupt-copy recovery; restart to verify persistence");
    return 0;
}
