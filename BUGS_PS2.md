# Issues and limitations found by testing

## Language and logo frame rate

The file select screen supports English/Spanish through `ESP.DAT` alongside OOT.Z64.
Actual selection, preserving saves when writing the option, reloading it from SRAM
and opening translated dialogue pass with emulated native USB.
Not all text written in code or the credits is translated.
Language persistence on a physical memory card still needs verification.

The scheduler's minimum of two vblanks kept the logo at 30 FPS despite
requesting one. Only that screen now allows one vblank: the actual presentation
test measures 59.9 FPS in PCSX2. The menu and game retain 30/20.
Hardware verification of the logo remains pending.

## Black screen at startup on real PS2

Status: startup fixed and confirmed by the user. The first
OOTPS2.ELF showed a black screen on an SCPH-90010 with an AV-to-HDMI converter.
The USB variant includes messages before starting the GS and a BOOT.LOG next
to the data; BOOTTEST displays the end of the log after a restart.

The second attempt reaches all modules, GS, controller, saving, audio,
vblank and data opening. It completes `Setup_Init` (`0x003a7708` in that ELF),
but does not complete `ConsoleLogo_Init` (`0x00486b60`, instance of 488 bytes).
The physical file is preserved in
`build/PS2/hardware-boot/scph90010-before-cache/BOOT.LOG`.

A separate cache bug is found and fixed: `osInvalDCache` ignored the range
and invalidated the entire data cache, potentially discarding pending writes
to stacks, queues and CPU-produced payloads. It now synchronizes the range's
lines through SyncDCache. A CPU/RAM model test detects neighboring data loss
with the previous implementation and accepts the new one. BOOTTEST also
tests neighbors, the physical alias and interrupt state.
The test and logo through the first frame pass in PCSX2. The user confirms
that the corrected version starts and allows gameplay. The new physical
BOOT.LOG is preserved in `build/PS2/hardware-boot/scph90010-first-gameplay`.

## Pauses, floor and audio on the console

Status: floor waves are corrected by centered UVs in the second layer,
confirmed by the user on the SCPH-90010 with FLOOR v3. The normal game
applies the correction automatically. Loads recover on their own after
waiting, and improving audio during low FPS remains pending.
DIAG.ELF adds DIAG.LOG with loads, USB timings, video and audio queue data.
The USB cache is expanded and separates sample reads from game loads;
bounded read-ahead is added and redundant consecutive seeks are avoided.
The USB audio reserve increases to 60 ms. DIAG224.ELF allows comparison
with a 640x224 render with duplicated lines, retaining the converter's
interlaced NTSC signal. Normal ELFs retain 640x448. The physical comparison
at 224 lines still shows the same waves. A separate precision loss caused
by truncating interpolated UVs at each clipping plane is fixed.
256 cameras and the floor tests pass, but the user confirms that this
correction alone did not change the waves.
The physical FLOOR v1 comparison retains waves with forced perspective;
omitting the second layer eliminates them but loses part of the floor.
The log confirms active perspective and the modes in Kokiri, without USB
errors or losses. FLOOR v2 also does not change the waves with read-only
depth and a minimal offset. Its 130 samples contain no USB errors or lost
records. A coordinate in the second layer with 32 pixels is observed
exceeding the GS's ±2047 range. FLOOR v3 restores the original depth and
centers second-layer UVs through whole repeats per triangle. Up selects
that test; Right restores the previous UVs. Instructions and limitations
are in `PS2_FLOOR_DIAGNOSTIC.md`. The user confirms that Up eliminates the waves.
The capture `build/PS2/hardware-runtime/floor-v3-confirmed-20261005-184050`
records 58 out-of-range coordinates before adjustment and zero after in
24 sampled frames, without USB errors or lost records. Other areas and
conditions remain pending; this is not a validation of the entire game.
General usage and limitations are in `PS2_HARDWARE.md`.

The user confirms that the latest revision works well and identifies FPS
drops mainly in large areas. The GS packet revision retains the SDK
registers with fewer bytes per vertex and reuses TEX0/PRIM/TEX1 per triangle
group. The USB animation cache keeps 256 KiB of game data separate from
samples. Equivalence tests and interleaved reads pass; improvement still
needs to be measured on real PS2. This does not guarantee 20 FPS in every
view or eliminate initial loads.

## Black screen after the logo with DIAG224

Status: startup fixed and confirmed in the latest physical comparison.
The first DIAG224 reached the first logo frame, but DIAG.LOG preserves
only its 346-byte header; there are no periodic samples. Both logs are
preserved in `build/PS2/hardware-runtime/scph90010-diag224-20261005-140823`.
They do not establish whether video output, the game or the new logging
thread failed.

The logger's unchecked usleep wait is removed: a semaphore now wakes it
from the vblank thread. Logger startup is saved immediately, its entry is
logged, and logging stops if the wait fails. A lack of Timer 0 progress
allows samples with approximate retrace timing, marked with clockFallback=1.
DIAG.ELF was first checked with normal 448-line video; DIAG224 startup and
its Kokiri load were then confirmed as well.

The second physical attempt, DIAG.ELF with 448 lines, also shows a black screen.
Its BOOT.LOG ends with `REGISTRO: iniciando hilo por vblank`; DIAG.LOG
preserves only the v2 header of 433 bytes. Private logs were verified in
`build/PS2/hardware-runtime/scph90010-diag448-20261005-143535`.
The lockup is inside logger startup; it does not demonstrate a 224-mode failure.

The new test with a FAT32 image and the emulated USB driver, without HostFs,
reproduces text corruption and USBHDFSD errors during flushes, although the
game continues. BOOT.LOG v2 and DIAG.LOG v3 write 512-byte sectors from
aligned buffers and keep the file aligned; retries after partial writes
also preserve buffer alignment. BOOTTEST removes padding to display the
last ten stages. Separate milestones for the first batch are added.
The user confirms that this revision passes the intro, opens the menu
and enters their save on the SCPH-90010.
The native USB driver test in PCSX2 now passes in
`build/PS2/usb-stack-checks/20261005-151055-633/report.json`: both logs
intact, data unchanged, scene loads and frames after the logo.

## Black screen when leaving the house with DIAG.ELF

Status: fixed in the automated test and physically confirmed by the user.
The latest physical log preserves samples through 205 seconds: after
leaving the house, frames stop while audio and log flushes continue,
without reads or USB errors. Private evidence with verified hashes is in
`build/PS2/hardware-runtime/scph90010-house-exit-20261005-153103`.

The USB driver test reproduces Graph ending after destroying the house.
The Play_Init wrapper changed its identity in Graph_GetNextGameState's
external comparison, while Play_Update's internal transition assigned
the original Play_Init. The diagnostic now wraps GameState_Init;
it preserves callback identities and can continue to the next scene.
The house → Kokiri → house test completes 120 updates in each scene,
without USB errors or lost records, in
`build/PS2/usb-stack-checks/20261005-155909-872/report.json`.
The user now confirms leaving the house and gameplay through obtaining
the sword on the SCPH-90010. Logs are preserved in
`build/PS2/hardware-runtime/scph90010-first-sword-20261005-162143`.
This diagnostic fix does not itself correct the floor waves.

## Nintendo 64 logo cadence

Status: corrected and measured in PCSX2 with native USB. The scheduler's
minimum of two vblanks prevented R_UPDATE_RATE=1 from taking effect.
It is removed only during the logo; the presentation test measures
59.9 FPS after warmup. The file select screen retains 30 FPS and the game 20.
Physical cadence still needs measurement.

## Gerudo Training Ground, room 2

Status: pending. The new room traversal finds a read exception in the
first frame. It is also reproduced when starting only this case,
without loading previous rooms:

```powershell
./tools/run_ps2_room_smoke.ps1 -Start 201 -Count 1
```

The log locates the read in `EnGoroiwa_TeleportToWaypoint` and
`EnGoroiwa_FaceNextWaypoint`, the moving boulder actor. The scene bank
describes one path, but two room actors request indices 0 and 1.
The second descriptor remains in a block not described by the converter,
with its pointer in the original byte order. This is a concrete conversion
lead; the full path and its movement still need verification before the
issue can be considered fixed.

Private logs: `build/PS2/room-regression/20261004-220503-633/runtime.log`
and `build/PS2/room-regression/20261004-223054-332/runtime.log`.

## Dark captures or no visible geometry

Status: camera review required. The gallery retains captures of individual
rooms from synthetic positions; some are outside the geometry, in front
of walls or in rooms used only for a cutscene. Cases with more than
65 % nearly black pixels or fewer than 32 colors are flagged for review.
A successful load does not certify that the image is faithful to the original.

## Geometry hidden by a demonstration

In `hiral_demo`, `DemoKankyo_Init` sets `curRoom.segment` to NULL to hide
the background. The first test marked this as FAIL despite complete actors,
events and capture. The diagnostic allows this hidden geometry only in
`SCENE_CUTSCENE_MAP`, with the load buffer present and all other conditions
correct. This changes only the test criterion, not the game or scene.

## Remaining coverage

The traversals initialize rooms with an age chosen per scene and test state
functions. They do not solve every puzzle, combat, dialogue, natural transition,
age change or ending. On real PS2, initial data validation and startup into
a save are confirmed; loading issues, video/audio and physical saving verification
remain. The Deku Tree and audio in PCSX2 remain user-confirmed cases.
Current work returns to performance and USB loading, especially in large areas.
