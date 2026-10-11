#!/usr/bin/env python3
"""Exercise the real PS2 controller/motor stubs with an asynchronous SDK mock.

No game, emulator, USB, or controller is accessed. Run with a host C compiler:
    python3 tools/test_ps2_pad_rumble.py
The target build separately checks the 32-bit OSPfs ABI; this host test checks
discovery, commands, error paths and reconnection using the host pointer size.
"""
from pathlib import Path
import os
import subprocess
import tempfile


LIBPAD = r'''
#ifndef TEST_LIBPAD_H
#define TEST_LIBPAD_H
#define PAD_LEFT 0x0080
#define PAD_DOWN 0x0040
#define PAD_RIGHT 0x0020
#define PAD_UP 0x0010
#define PAD_START 0x0008
#define PAD_R3 0x0004
#define PAD_L3 0x0002
#define PAD_SELECT 0x0001
#define PAD_SQUARE 0x8000
#define PAD_CROSS 0x4000
#define PAD_CIRCLE 0x2000
#define PAD_TRIANGLE 0x1000
#define PAD_R1 0x0800
#define PAD_L1 0x0400
#define PAD_R2 0x0200
#define PAD_L2 0x0100
#define PAD_STATE_DISCONN 0
#define PAD_STATE_FINDPAD 1
#define PAD_STATE_FINDCTP1 2
#define PAD_STATE_EXECCMD 5
#define PAD_STATE_STABLE 6
#define PAD_STATE_ERROR 7
#define PAD_RSTAT_COMPLETE 0
#define PAD_RSTAT_FAILED 1
#define PAD_RSTAT_BUSY 2
#define PAD_TYPE_DUALSHOCK 7
#define PAD_MODECUREXID 2
#define PAD_MODETABLE 4
#define PAD_MMODE_DUALSHOCK 1
#define PAD_MMODE_LOCK 3
struct padButtonStatus {
    unsigned char ok, mode;
    unsigned short btns;
    unsigned char rjoy_h, rjoy_v, ljoy_h, ljoy_v;
    unsigned char pressure[24];
} __attribute__((packed));
int padInit(int);
int padEnd(void);
int padPortOpen(int, int, void*);
int padPortClose(int, int);
unsigned char padRead(int, int, struct padButtonStatus*);
int padGetState(int, int);
unsigned char padGetReqState(int, int);
int padInfoMode(int, int, int, int);
int padSetMainMode(int, int, int, int);
unsigned char padInfoAct(int, int, int, int);
int padSetActAlign(int, int, const char[6]);
int padSetActDirect(int, int, const char[6]);
#endif
'''

KERNEL = r'''
#ifndef TEST_KERNEL_H
#define TEST_KERNEL_H
int DIntr(void);
void EIntr(void);
int SleepThread(void);
#endif
'''

ULTRA64 = r'''
#ifndef TEST_ULTRA64_H
#define TEST_ULTRA64_H
typedef struct OSMesgQueue OSMesgQueue;
#define MAXCONTROLLERS 4
#define OS_MESG_BLOCK 1
s32 osRecvMesg(OSMesgQueue*, OSMesg*, s32);
#endif
'''

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ps2_types.h"
/* Some host CRTs expose errno through stdlib.h; libultra uses that name
 * for the controller response byte, not the host errno macro. */
#undef errno
/* Only scheduling infrastructure and the target pointer-size assertion are
 * excluded: both production controller and motor implementations are used. */
#define PS2_OS_INTERNAL_H
#define PS2_OS_EVENT_SI 5
void ps2_os_fire_event(int);
#include "PAD_SOURCE"
#define _Static_assert(expression, message)
#include "MISC_SOURCE"
#undef _Static_assert
#include "PADSETUP_SOURCE"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static int states[2] = {PAD_STATE_STABLE, PAD_STATE_EXECCMD};
static int requests[2], dualshock[2] = {1, 1}, acts[2] = {2, 2};
static int modeCalls[2], alignCalls[2], directCalls[2], initCalls, openCalls, closeCalls;
static int modeResult = 1, alignResult = 1, directResult = 1, initResult = 1;
static unsigned char motors[2][6];
static int pending[2], readMode = 0x73, readResult = 1, buttons = 0xffff, siEvents;
static int stateCalls, requestCalls, readCalls[2];
int DIntr(void) { return 1; }
void EIntr(void) {}
int SleepThread(void) { abort(); }
void ps2_os_fire_event(int event) { if (event != 5) abort(); siEvents++; }
s32 osRecvMesg(OSMesgQueue* queue, OSMesg* message, s32 flag) {
    (void)queue; (void)message; (void)flag; abort();
}
int padInit(int mode) { if (mode) abort(); initCalls++; return initResult; }
int padEnd(void) { return 1; }
int padPortOpen(int port, int slot, void* memory) {
    (void)port; if (slot || (uintptr_t)memory % 64) abort(); openCalls++; return 1;
}
int padPortClose(int port, int slot) {
    if (slot || motors[port][0] || motors[port][1]) abort(); closeCalls++; return 1;
}
int padGetState(int port, int slot) {
    if (slot) abort(); stateCalls++;
    /* libpad converts STABLE to EXECCMD while a request is pending. */
    if (states[port] == PAD_STATE_STABLE && requests[port] == PAD_RSTAT_BUSY)
        return PAD_STATE_EXECCMD;
    return states[port];
}
unsigned char padGetReqState(int port, int slot) { if (slot) abort(); requestCalls++; return requests[port]; }
int padInfoMode(int port, int slot, int mode, int index) {
    if (slot) abort();
    if (mode == PAD_MODECUREXID) return dualshock[port];
    if (mode != PAD_MODETABLE) abort();
    if (index < 0) return dualshock[port] ? 2 : 0;
    return index ? PAD_TYPE_DUALSHOCK : 4;
}
int padSetMainMode(int port, int slot, int mode, int lock) {
    if (slot || mode != 1 || lock != 3 || requests[port] == PAD_RSTAT_BUSY) abort();
    modeCalls[port]++;
    if (modeResult == 1) { requests[port] = PAD_RSTAT_BUSY; pending[port] = 1; }
    return modeResult;
}
unsigned char padInfoAct(int port, int slot, int index, int command) {
    if (slot || index != -1 || command) abort(); return acts[port];
}
int padSetActAlign(int port, int slot, const char data[6]) {
    if (slot || requests[port] == PAD_RSTAT_BUSY || data[0] || data[1] != 1) abort();
    for (int i = 2; i < 6; i++) if ((unsigned char)data[i] != 255) abort();
    alignCalls[port]++;
    if (alignResult == 1) { requests[port] = PAD_RSTAT_BUSY; pending[port] = 2; }
    return alignResult;
}
int padSetActDirect(int port, int slot, const char data[6]) {
    if (slot) abort(); directCalls[port]++;
    if (directResult == 1) memcpy(motors[port], data, 6);
    return directResult;
}
unsigned char padRead(int port, int slot, struct padButtonStatus* status) {
    if (slot || requests[port] == PAD_RSTAT_BUSY) abort();
    readCalls[port]++; memset(status, 0, sizeof(*status));
    status->mode = readMode; status->btns = buttons;
    if (readMode != 0x41) status->rjoy_h = status->rjoy_v = status->ljoy_h = status->ljoy_v = 128;
    return readResult;
}
static void complete(int port, int success) {
    if (!pending[port]) abort(); pending[port] = 0;
    requests[port] = success ? PAD_RSTAT_COMPLETE : PAD_RSTAT_FAILED;
}
static void settle(OSContStatus status[4]) {
    for (int i = 0; i < 160; ++i) {
        for (int port = 0; port < 2; ++port) if (pending[port]) complete(port, 1);
        osContGetQuery(status);
    }
}
int main(void) {
    OSContStatus status[4]; OSContPad pads[4]; OSPfs pfs; u8 mask;
    memset(&pfs, 0, sizeof(pfs));
    CHECK(PadSetup_Init(NULL, &mask, status) == 0);
    CHECK(mask == 1 && initCalls == 1 && openCalls == 2);
    CHECK(status[0].type == 5 && !status[0].status && !status[0].errno);
    CHECK(status[1].errno == 8 && status[2].errno == 8 && status[3].errno == 8);
    states[1] = PAD_STATE_DISCONN;
    CHECK(modeCalls[0] == 1 && !alignCalls[0] && !directCalls[0]);
    CHECK(osMotorInit(NULL, NULL, 0) == 5);
    CHECK(osMotorInit(NULL, &pfs, 0) != 0 && !pfs.status);
    CHECK(__osMotorAccess(&pfs, 1) != 0 && __osMotorAccess(NULL, 1) != 0);
    int previous = stateCalls;
    osContGetReadData(pads); osContGetQuery(status);
    CHECK(stateCalls - previous == 4 && pads[0].errno == 4 &&
          !status[0].errno && status[0].type == 5 && !status[0].status &&
          !alignCalls[0] && !readCalls[0]);
    /* Real PadSetup_Init followed by PadMgr's query during a pending
     * mode command must keep the mask used by ConsoleLogo_Init. That
     * state latches fileNum=0xFEDC and disables Start if port 0 vanishes. */
    CHECK(PadSetup_Init(NULL, &mask, status) == 0 && mask == 1);
    osContGetQuery(status);
    u32 titleMask = 0;
    for (int i = 0; i < MAX_CONTROLLERS; ++i)
        if (!status[i].errno && status[i].type == CONT_TYPE_NORMAL) titleMask |= 1u << i;
    u16 titleFileNum = (titleMask & 1) ? 0xFF : 0xFEDC;
    CHECK(titleFileNum == 0xFF && !status[0].status);
    complete(0, 1); osContGetQuery(status); osContGetQuery(status);
    CHECK(alignCalls[0] == 1 && !status[0].status && !directCalls[0]);
    osContGetReadData(pads); osContGetQuery(status);
    CHECK(pads[0].errno == 4 && !status[0].errno && !status[0].status && !directCalls[0]);
    complete(0, 1); osContGetQuery(status); osContGetQuery(status);
    CHECK(status[0].status == 1 && directCalls[0] == 1 && !motors[0][0] && !motors[0][1]);
    CHECK(osMotorInit((struct OSMesgQueue*)0x1234, &pfs, 0) == 0 && pfs.status == 8);
    CHECK(pfs.queue == (struct OSMesgQueue*)0x1234 && pfs.channel == 0);
    CHECK(__osMotorAccess(&pfs, 1) == 0 && motors[0][0] == 1 && motors[0][1] == 255);
    int writes = directCalls[0]; CHECK(__osMotorAccess(&pfs, 1) == 0 && directCalls[0] == writes);
    CHECK(__osMotorAccess(&pfs, 0) == 0 && !motors[0][0] && !motors[0][1]);
    CHECK(__osMotorAccess(&pfs, 17) == 5);
    for (int i = 0; i < 6; ++i) { CHECK(__osMotorAccess(&pfs, 1) == 0); CHECK(__osMotorAccess(&pfs, 0) == 0); }
    CHECK(osMotorInit(NULL, &pfs, -1) != 0 && osMotorInit(NULL, &pfs, 2) != 0);
    CHECK(osMotorInit(NULL, &pfs, 0) == 0);

    /* A rejected stop removes rumble capability until an off write succeeds. */
    CHECK(__osMotorAccess(&pfs, 1) == 0); directResult = -1;
    CHECK(__osMotorAccess(&pfs, 0) == 4); osContGetQuery(status);
    CHECK(!status[0].status && motors[0][0]);
    directResult = 1; osContGetQuery(status);
    CHECK(status[0].status == 1 && !motors[0][0] && !motors[0][1]);

    /* Losing valid input also clears an active motor, even if PADMAN's
     * cached connection state has not reported disconnect yet. */
    CHECK(__osMotorAccess(&pfs, 1) == 0); readResult = 0;
    osContGetReadData(pads);
    CHECK(pads[0].errno == 8 && !motors[0][0] && !motors[0][1]);
    readResult = 1;

    /* Disconnect an active motor; reconnect must negotiate and write off. */
    CHECK(__osMotorAccess(&pfs, 1) == 0);
    states[0] = PAD_STATE_DISCONN; osContGetReadData(pads); osContGetQuery(status);
    CHECK(pads[0].errno == 8 && status[0].errno == 8 && !status[0].status);
    CHECK(!motors[0][0] && !motors[0][1]);
    CHECK(__osMotorAccess(&pfs, 1) != 0);
    states[0] = PAD_STATE_FINDPAD; osContGetQuery(status); CHECK(status[0].errno == 8);
    states[0] = PAD_STATE_EXECCMD; osContGetQuery(status); CHECK(status[0].errno == 8);
    states[0] = PAD_STATE_STABLE; osContGetQuery(status);
    CHECK(modeCalls[0] == 2 && !status[0].status);
    complete(0, 0); osContGetQuery(status); CHECK(!status[0].status);
    settle(status); CHECK(status[0].status == 1 && !motors[0][0] && !motors[0][1]);
    CHECK(modeCalls[0] == 3 && alignCalls[0] == 2);

    /* A re-discovery state must clear stale capability too. This may be
     * exposed by libpad instead of ERROR during bus retries. */
    CHECK(__osMotorAccess(&pfs, 1) == 0);
    states[0] = PAD_STATE_FINDPAD; osContGetQuery(status);
    CHECK(status[0].errno == 8 && !status[0].status && !motors[0][0]);
    states[0] = PAD_STATE_STABLE; settle(status);
    CHECK(status[0].status == 1 && modeCalls[0] == 4 && alignCalls[0] == 3);

    /* Plain digital pads remain usable without pretending they have motors. */
    states[1] = PAD_STATE_FINDCTP1; dualshock[1] = 0; readMode = 0x41; buttons = 0xffff ^ PAD_CROSS;
    osContGetQuery(status); osContGetReadData(pads);
    CHECK(!status[1].errno && !status[1].status && !modeCalls[1] && !alignCalls[1]);
    CHECK(pads[1].button == 0x8000 && !pads[1].stick_x && !pads[1].stick_y);
    CHECK(osMotorInit(NULL, &pfs, 1) != 0);
    osContStartReadData(NULL); osContStartQuery(NULL); CHECK(siEvents == 2);

    /* Rejected mode requests back off instead of flooding RPC calls. */
    states[1] = PAD_STATE_DISCONN; osContGetQuery(status);
    dualshock[1] = 1; modeResult = 0; states[1] = PAD_STATE_STABLE;
    osContGetQuery(status); CHECK(modeCalls[1] == 1 && !status[1].status);
    for (int i = 0; i < 59; ++i) osContGetQuery(status);
    CHECK(modeCalls[1] == 1 && !alignCalls[1]);
    modeResult = 1; settle(status); CHECK(status[1].status == 1);

    /* An accepted alignment request can still fail asynchronously. */
    states[1] = PAD_STATE_DISCONN; osContGetQuery(status);
    states[1] = PAD_STATE_STABLE; osContGetQuery(status);
    complete(1, 1); osContGetQuery(status); osContGetQuery(status);
    int previousAlign = alignCalls[1]; CHECK(pending[1] == 2);
    complete(1, 0); osContGetQuery(status);
    CHECK(!status[1].status);
    settle(status); CHECK(status[1].status == 1 && alignCalls[1] == previousAlign + 1);

    /* Rejected alignment also backs off and cannot expose a motor. */
    states[1] = PAD_STATE_DISCONN; osContGetQuery(status);
    states[1] = PAD_STATE_STABLE; osContGetQuery(status);
    complete(1, 1); osContGetQuery(status); alignResult = 0;
    osContGetQuery(status); previousAlign = alignCalls[1];
    for (int i = 0; i < 59; ++i) osContGetQuery(status);
    CHECK(!status[1].status && alignCalls[1] == previousAlign);
    alignResult = 1; settle(status); CHECK(status[1].status == 1);

    /* A failed off write while the bus is lost is bounded; reconnect
     * must clear the retained command before advertising the motor. */
    CHECK(osMotorInit(NULL, &pfs, 1) == 0 && __osMotorAccess(&pfs, 1) == 0);
    directResult = -1; writes = directCalls[1]; states[1] = PAD_STATE_ERROR;
    osContGetQuery(status);
    CHECK(status[1].errno == 8 && !status[1].status && directCalls[1] == writes + 1);
    for (int i = 0; i < 8; ++i) osContGetQuery(status);
    CHECK(directCalls[1] == writes + 1);
    directResult = 1; states[1] = PAD_STATE_STABLE; settle(status);
    CHECK(status[1].status == 1 && !motors[1][0] && !motors[1][1]);

    /* Duplicate init never reopens a port; shutdown clears motors first. */
    osContInit(NULL, &mask, status); CHECK(mask == 3 && openCalls == 2 && initCalls == 1);
    CHECK(osMotorInit(NULL, &pfs, 0) == 0 && __osMotorAccess(&pfs, 1) == 0);
    ps2_pad_shutdown(); CHECK(closeCalls == 2 && !motors[0][0] && !motors[0][1]);
    initResult = -1; osContInit(NULL, &mask, status); CHECK(mask == 0 && openCalls == 2);
    puts("PS2 pad/rumble: PASS asynchronous negotiation, title connection mask, capability reporting, game motor API, failure recovery, reconnect, digital input and shutdown");
    return 0;
}
'''


def main():
    repository = Path(__file__).resolve().parent.parent
    source = repository / 'src/platform/ps2/libultra_stub'
    driver = DRIVER.replace('PAD_SOURCE', (source / 'osContPad.c').as_posix())
    driver = driver.replace('MISC_SOURCE', (source / 'osMisc.c').as_posix())
    driver = driver.replace('PADSETUP_SOURCE', (repository / 'src/libu64/padsetup.c').as_posix())
    with tempfile.TemporaryDirectory(prefix='ps2-pad-test-') as folder:
        root = Path(folder)
        (root / 'libpad.h').write_text(LIBPAD)
        (root / 'kernel.h').write_text(KERNEL)
        (root / 'ultra64.h').write_text(ULTRA64)
        (root / 'libu64').mkdir()
        (root / 'libu64/padsetup.h').write_text((repository / 'include/libu64/padsetup.h').read_text())
        (root / 'driver.c').write_text(driver)
        executable = root / ('test.exe' if os.name == 'nt' else 'test')
        subprocess.run([os.environ.get('CC', 'gcc'), '-std=gnu11', '-O2', '-Wall', '-Wextra',
                        '-Wno-misleading-indentation', '-I', str(root),
                        '-I', str(repository / 'include/ps2'), str(root / 'driver.c'),
                        '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    main()
