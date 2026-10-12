# First test on real PS2

Private package for booting from a FAT32 USB drive through Free McBoot and
wLaunchELF. The user already confirms startup and entering a save on a real
console. PCSX2 results do not certify every case on hardware.

User's first test: on an SCPH-90010 connected through an AV-to-HDMI converter,
BOOTTEST displays `DATOS OK`. The first game showed a black screen;
after fixing global cache invalidation, it starts and allows gameplay.
Recoverable loading pauses and audio distortion when FPS drops remain.
Floor waves when moving the camera are now corrected by centering the second
layer's coordinates; the user confirms the solution on this console with
FLOOR v3. OOTPS2.ELF applies this calculation automatically, without selecting
modes. Physical saving remains pending.

The latest log review now allows loading the save on hardware.
A DIAG.ELF-specific bug that ended gameplay when leaving the house was also
fixed: logging changed an initialization function's identity.
The automated house → Kokiri → house test passes with PCSX2's native USB
driver. The user confirms leaving the house on the console and continues
through obtaining the sword. Logs from that session are preserved in
`build/PS2/hardware-runtime/scph90010-first-sword-20261005-162143`, without
USB errors or lost records.

1. Copy the entire `OOTPS2` folder to the USB drive's root.
2. In wLaunchELF, open `mass:/OOTPS2/BOOTTEST.ELF`.
3. Wait for the result. It should display `DATOS OK`, `MEMORIA OK` and the
   status of the memory card in port 1. The diagnostic only queries the card;
   it does not save or format. If `ERROR MEMORIA` appears, report the test
   number and do not continue.
4. Restart the console, return to wLaunchELF and open
   `mass:/OOTPS2/OOTPS2.ELF`.
   This version displays the boot stage before activating the game's video.
   If it stops there, note the last visible stage.
5. Check the logo, title, controller, music and loading a save.
   Then test Kokiri and the Deku Tree interior.
6. To check saving, use a test save on a PS2 card with free space in port 1.
   Save from the game, restart and verify that you can recover that progress.
   Also check its icon in the BIOS.

The ELF embeds the SDK's USBD and USBHDFSD and reloads them after resetting
the IOP. It does not need external IRX files. If wLaunchELF supplies its
boot path, it looks for `OOT.Z64` in the same folder; the default path is
`mass:/OOTPS2/OOT.Z64`. Package filenames must be preserved.

`OOT.Z64` contains data **converted for this port**. An original ROM with
the same name will not work. Startup checks its size and twelve distributed
blocks using CRC32; this detects incorrect files and some corruption,
but does not replace the full SHA256 in `HASHES.sha256`.

On a data error, the game displays an explanation and does not start
gameplay. Restart to return to the launcher.

If the screen goes black after activating video, restart and open BOOTTEST.ELF
again. It will display the last ten stages of `mass:/OOTPS2/BOOT.LOG` without
modifying that file. Report the last line or photograph the screen.
The file can also be read from the PC. Each new game startup replaces
BOOT.LOG after validating the data; BOOTTEST preserves it.
If no message appears from the beginning and BOOTTEST reports no log,
report that case: the game may not have reached its entry point.

Logging is limited to boot stages, game states and the first frame.
It disables diagnostic text before gsKit takes control of the screen.
It does not write a log every frame. If the USB drive cannot create the
file, it displays that condition while the text screen is still active.

The first black screen is located before completing Nintendo 64 logo
initialization. This version fixes global cache invalidation and logs the
logo allocation, segment load, matrices, view and SRAM buffer.
The user confirms that it now starts. The scheduler's minimum of two vblanks
is removed only during the logo. An actual presentation test with emulated
native USB measures 59.9 FPS after the first 30 frames; file select retains
30 and gameplay 20. The SCPH-90010's physical automatic log confirms three
logo intervals at 59.9–60 FPS.

## Spanish and performance

The package adds `ESP.DAT`. In file select, open Options/Opciones, move down
to LANGUAGE/IDIOMA and choose Español with left or right. Press B to save
the option and return. It includes dialogue, the font and menu textures;
some text written in code and the credits remain in English. Full data
and credits: [PS2_LANGUAGE.md](PS2_LANGUAGE.md).

The game reuses floor coordinates and texture preparation within each
triangle group. It retains the physical wave correction and passes the
comparison of points sent to the GS. This reduces repeated calculations,
but the FPS improvement still needs measurement on the console and does
not certify stable 20 FPS in every view. DIAG.ELF samples render components
every 60 frames to locate remaining costs. Use OOTPS2.ELF to compare normal
performance: diagnostic logging adds work and USB writes.

The SCPH-90010's second automatic session, preserved in
`build/PS2/hardware-runtime/auto-play-physical-20261006-194851`, confirms
4 KiB batches and a maximum write pause of 128 ms versus 949 ms in the
previous session. The last minute without reads or overlapping writes
records a median of 13.45 FPS and 60.55 ms of translation per frame;
the camera is not logged, so this does not certify a comparable FPS gain.
There is a sector of 512 null bytes around second 445, without a reported
write error. The analyzer flags that damage and the sample jump from
443 to 446, and discards its lines; the last minute used does not contain
that sector. The cause of the incomplete write remains pending.
The current revision prepares wrapping, bounds and filtering once per
layer/material. It retains logging of each cache use to avoid omitting
a subsequent GS wait.

## Session logging and USB optimizations

The game data cache retains 256 KiB and also reuses short animation reads;
it keeps a separate audio sample cache. Each read-ahead remains limited to
16 KiB. This reduces repeated requests while walking and reusing resources,
but a new area still needs to be read from USB. Triangle packets also omit
repeated register addresses and reuse their preparation per material.
Equivalence passes on the emulated EE; FPS for this revision still needs
measurement on the console, especially in large areas.

The normal USB game `mass:/OOTPS2/OOTPS2.ELF` now automatically logs your
session in `DIAG.LOG`, without opening another program. It includes FPS
every second, scene and room identifiers, load/USB timings, drawing and audio
observations. The `AUTO-PLAY` marker appears after the header date.
It uses a RAM buffer of 256 records, writes batches every 15 seconds and
records overall timings without enabling timers per triangle or vertex.
It does not display a counter.
To collect a drop, play in that area for a minute, save your progress and
wait at least 20 seconds before turning off. Then connect the USB drive
to the PC. The file is replaced when starting another session; preserve it first.

To separate drawing costs without DIAG's indicator or frequent sampling,
open `mass:/OOTPS2/PERF.ELF`. It includes the same optimizations and automatic
logging, with detailed measurement only every 300 frames. The other 299 do
not read the per-vertex/triangle timers. The marker
`[PS2 PERF SAMPLING] every=300` identifies this session; sampled frame
timings include timer overhead and other interruptions, excluding recorded
DSP synthesis. It helps locate costs, not attribute every drop to a phase
without a controlled comparison. Use a slow outdoor view for about
30 seconds, walk for a minute, save and wait 20 seconds before turning off.
It also replaces BOOT.LOG and DIAG.LOG at startup.
The normal OOTPS2.ELF game keeps detailed profiling disabled.
The current revision adds `[PS2 COMBINER ANALYSIS PROFILE]`: it counts
rebuilds and reused analyses when changing materials. Its time is included
in the other phases and must not be added again. The cache keeps 32 entries
and uses 2,716 bytes; it retains key validation and dynamic colors.
Its FPS gain must be checked with the same camera.
`[PS2 DRAW DETAIL PROFILE]` separates key lookup, GS packets and floor
rebasing; its timings are already included in the other phases.
The previous session contains one damaged line and two duplicate samples.
Originals are preserved and the analyzer excludes duplicate intervals;
the cause of that write remains pending. The recent session, preserved in
`build/PS2/hardware-runtime/setup-physical-20261007-205251`, contains
285 intact samples and 13 profiles, without certifying that the intermittent
issue is fixed. Between 111 and 126 seconds, 14 intervals without overlapping
writes collect 169 frames at a median of 11.9 FPS: translation 68.02 ms,
final submission 1.96 and presentation 13.11 per frame. There are no reads
or observations of an empty audio queue; the renderer remains the sustained
cost. Overall timings include execution of other threads.
`[PS2 TEXTURE CONTENT]` distinguishes `smallBypass`, `largeBypass`, `mutations`,
`newSnapshots`, `descriptorEvictions` and `overlapEvictions`: small or large
limits, renewed changes, new copies and evictions due to descriptors or ring
space. `[PS2 DRAW SETUP PROFILE]` measures initial preparation, tile application
and metadata before lookup. Its timings are included in submission/lookup.
The counters describe the sampled frame, not the entire session.
In five large profiles, 358 of 372 evictions (96.2 %) are descriptor evictions.
The candidate increases their count to 128, with 3,840 additional bytes of
metadata; it retains the 128 KiB ring, 256 bytes/16 KiB limits and comparison
of every byte from both sources. It also retains 17 palette fingerprints in
72 bytes, plus 12 for counters: it only invalidates the bank and full palette
when a decoded entry changes. All TLUT loads still read RAM. Its portion of
the physical metadata cost, 3.3–4.9 ms in those five profiles, has not yet
been isolated. `[PS2 PALETTE HASH PROFILE]` measures fingerprints within
metadata, excluding TLUT updates. `[PS2 MATERIAL RUN PROFILE]` counts commands
that delimit groups; not all correspond to flushing a batch with primitives.
The isolated EE suite, eleven analyzer tests and both traversals with
emulated native USB pass: logo at 59.9 FPS, options/dialogue in Spanish and
house → Kokiri → house. Captures preserve the menu; the dialogue changes
only 35 pixels from eye blinking. The player's logs, data and card are
verified intact. Tests use disabled cards. Their emulated cycles do not
establish a physical FPS gain or a full-frame gain.
Details: [CHANGELOG_PS2_PORT.md](CHANGELOG_PS2_PORT.md). Console improvement
and stable 20 FPS remain pending. The later session of 255 samples confirms
zero descriptor evictions in large profiles; its no-read interval records
13 FPS and 60.83 ms of translation. Identical cameras between tests have
not been verified. The new candidate retains the group while traversing
parts and loading vertices, invalidating only overwritten slots.
Its 24 point/vertex comparisons and both traversals with emulated native
USB pass; it preserves captures, the logo at 59.9 FPS and transitions.
It needs a new physical measurement in the same view. PERF measures the
boundaries that actually close groups.

For a test with a visible counter, open `mass:/OOTPS2/DIAG.ELF`.
It runs the same game with your controller and normal save; it creates
`DIAG.LOG` next to the game files. Since the FPS-HUD revision it also displays
`FPS 20.0` in a corner, updated approximately every second: it measures
presented frames, not the 60 video retraces. At startup it displays
`FPS --.-` until the first sample. An asterisk indicates the estimate from
retraces if the EE clock stops advancing. The counter continues even if
the log reaches its limit or a write fails. DIAG224.ELF includes the same counter.

To measure a large area, look at a slow view for 20 seconds, walk for another
20 and wait ten before turning off. Note the minimum and typical counter
values. When reconnecting the USB drive to the PC, the `runtime log v3`
header with the `FPS-HUD` marker after the date identifies this test versus
previous logs, while preserving reader compatibility.
The counter draws solid sprites after the game: it does not load fonts or
textures and does not enter the inventory portrait. OOTPS2.ELF logs with
less frequent writes and without adding that indicator to the image.

The first automatic console session confirms a median of 12 FPS in the
final Kokiri interval, even during intervals without reads or logger writes.
Graphics translation takes 66.82 ms per frame, including interruptions by
other threads; it does not yet identify which graphics phase dominates.
The new texture check also speeds up sources not aligned to sixteen bytes,
preserving all their contents and the floor correction.
This revision's result still needs comparison in the same view.

BOOTTEST remains only the initial check.

DIAG.LOG records EE stages and errors, state changes, room requests and
completion, slow or failed reads, and performance samples approximately
every second. It includes presented frames, translation/GS/presentation
timings, synthesis cycles, read bytes and waits, cache hits and audio queue
observations. `fps10=180` means 18 FPS. `zeroPolls` is not an exact counter
of audio underruns in the IOP.

If two samples pass without completed frames, it records STALL_SNAPSHOT:
initialization/update/destruction phase, scene and transition, graphics
queue, current command, EE thread state and waits, and GS/DMA registers.
It then repeats the snapshot at intervals 5, 10, 15… without progress.
This can appear during a normal long load; alone it does not prove an exception.
Logging does not change scene callbacks.

Producers log in RAM; a semaphore wakes the logging thread from vblank,
without using usleep or writing files from vblank. The initial state is
saved immediately, then blocks are written and closed every five seconds
in DIAG and every fifteen in OOTPS2. BOOT.LOG v2 writes 512-byte sectors;
DIAG.LOG v3 groups up to 4 KiB per call, with aligned buffers and
sizes/offsets that are multiples of 512. Spaces at the end of batches are normal.
Logging startup stages distinguish the clock, semaphore, thread and first
batch opening, writing and closing. If the port's clock fails, clockFallback=1
marks an approximate interval based on retraces; it does not certify
graphics timings. The file is limited to 2 MiB; when full, it logs
`LOG_LIMIT` and the game continues without further logging.
A write that returns an error stops only logging; a USB driver blocked
inside a call can also stop loads. It does not log every instruction,
triangle or audio sample. An exception that stops all threads may prevent
the final flush. The logger can introduce a pause: `FLUSH durationMs`
measures its own write. The first physical automatic session records a
maximum of 949 ms. Grouping sectors reduces calls; the second session
with 4 KiB records a maximum of 128 ms. Its incomplete sectors are flagged
during analysis and are not interpreted as valid measurements.

Test about 20 seconds standing still, another interval walking, and enter/leave
a house. After observing an issue, wait at least twenty seconds before turning
off or restarting so the next block can close. Save your progress from the game
if you want to retain it. Reconnect the USB drive to the PC and preserve DIAG.LOG
and BOOT.LOG before starting another session: DIAG.ELF replaces its log at startup.
The normal ELF also creates and replaces DIAG.LOG at startup.

To check the USB driver in PCSX2 without using HostFs, there is
`tools/run_ps2_usb_stack_check.ps1`. It uses a private FAT32 image of 128 MiB,
the emulator's USB storage device and the IRXs embedded in the ELF.
It does not format the physical USB drive. It disables the player's cards
and checks logs, driver errors and the data's SHA256 after closing the test.
The image and results are stored in `build/PS2/usb-stack-checks`.
It requires the local Python environment specified by `-Python`, with
`pyfatfs` and `setuptools<81`, plus the existing test BIOS/configuration.
Emulating that driver does not certify real USB timing.

The transition regression is built separately with
`bash tools/build_ps2_usb_transition_smoke.sh` and run with
`tools/run_ps2_usb_stack_check.ps1 -Transition -Seconds 55`.
It loads a synthetic save, completes 120 updates in the house, Kokiri Forest
and house, and uses normal transitions. It does not test contact with the
door or real-save progress, and is not part of the USB package.

The physical session `traversal-physical-20261008-164351` maintains a view
at about 13 FPS without USB reads. 224 complete samples are used: the final
512-byte sector matches the previous log and is excluded along with spliced
lines. The cause of that write remains unresolved.
The profile still points to translation and drawing preparation; nested
timings must not be added together. PERF now breaks down its boundaries by
command type in two complete lines per sampled frame, compatible with the
log buffer. The analyzer combines them only if they belong to the same
frame and are not duplicated; it does not itself certify every form of
file damage.

The candidate compares textures in complete 256-byte blocks and retains
the previous path for tails, without skipping content. Single-layer
textured points are written directly into the free part of the batch;
the two floor layers retain their order. The four RDP SYNC commands,
already ignored by the interpreter, no longer split groups; loads, materials,
captures and the end of the root list remain boundaries. Point, packet and
content comparisons pass on the emulated EE. Isolated savings do not yet
certify physical FPS improvement or stable 20 FPS; the next test uses PERF.ELF.
In both scenes of the automated traversal, SYNC boundaries disappear,
but the next material change keeps the total group count unchanged.
That change alone does not demonstrate batch savings in those scenes.

## Comparing floor shaking

`mass:/OOTPS2/DIAG224.ELF` runs the same diagnostic, but renders at
640x224 and duplicates lines up to 448. It retains the interlaced NTSC signal
to test with the same AV-to-HDMI converter. It has less vertical detail;
it is a comparison to identify the source of shaking, not a confirmed
texture correction. OOTPS2.ELF and DIAG.ELF retain rendering at 640x448.

The first DIAG224 showed a black screen after the logo due to the already
fixed diagnostic bugs. The user now compares the revised version and the
waves remain unchanged. Its log confirms 640x224 rendering, 448-line output
and Kokiri Forest loading, without USB errors or losses. Private evidence in
`build/PS2/hardware-runtime/scph90010-224-waves-20261005-163739`.

Test the same floor with Link and the camera still, then while walking.
Compare it with the normal version. Report whether the shaking disappears,
decreases or remains unchanged and whether the image changes. This test
helps compare flicker between lines; it does not certify correct texture
or depth conversion. DIAG.LOG includes render and video output dimensions.

DIAG224.ELF also replaces DIAG.LOG at startup. Preserve that file on the PC
before starting DIAG.ELF if you want to keep the results of both sessions.

The next revision retains fractional texture coordinates for clipped vertices.
It previously truncated them to an integer at each clipping plane, changing
the mapping with the camera. An independent perspective interpolation test
passes 256 cameras and 1323 corners, alongside the two-layer floor tests.
This demonstrates the calculation's correctness, but the user now confirms
that waves remain unchanged after this physical revision.

The first additional comparison `mass:/OOTPS2/FLOOR.ELF` retained waves
with forced perspective; removing the second layer eliminated them along
with part of the texture. The log confirms modes in Kokiri and active
perspective, without USB errors or losses. It is preserved in
`build/PS2/hardware-runtime/floor-lab-result-20261005-175141`.

The physical FLOOR v2 comparison also does not improve waves when changing
second-pass depth. Log preserved in
`build/PS2/hardware-runtime/floor-v2-result-20261005-181656`.
FLOOR v3 allows switching with the D-pad between centered second-layer UVs
(Up), one layer (Left) and previous UVs (Right), without restarting.
It restores the original depth; it shifts each triangle by whole repeats
to reduce STQ magnitude and avoid the GS texel limit.
It logs the mode, maximum UVs per sampled frame and UV/W/STQ/XYZ samples,
including second-layer dimensions and shifts. Instructions are in
`PS2_FLOOR_DIAGNOSTIC.md` and `SUELO.txt` in the additional package.
The normal ELF now automatically incorporates the same user-confirmed calculation.
The physical FLOOR v3 capture is preserved with hashes in
`build/PS2/hardware-runtime/floor-v3-confirmed-20261005-184050`.
Its 24 sampled frames reduce maximum UVs from 2222.5 to 411.188 texels,
with 58 coordinates originally outside ±2047 and none after adjustment.
The 105 intervals contain no USB errors, losses or NUL.
This documents the tested view; it does not certify the entire game on hardware.

To build a comparison without periodic writes, use
`PS2_USB_AUTO_LOG=0 bash tools/build_ps2_usb.sh`; by default the USB package
includes the requested automatic logging. DIAG.ELF offers more render
samples and the visible counter. Writes from both variants can introduce
additional pauses: in the two earlier physical sessions a flush took up
to 184 and 221 ms, respectively.

## Log analysis

Analysis from the PC:
```powershell
python tools/analyze_ps2_runtime_log.py D:\OOTPS2\DIAG.LOG --output build/PS2/hardware-runtime/report.json
```

The three USB game ELFs include a 256 KiB sample cache and a 256 KiB load cache.
8 KiB load requests can reuse 16 KiB reads; large loads do not evict audio
sectors. Seeks are omitted when the next read starts where the previous one
ended. Read-ahead is bounded and respects the end of the file; initial loads
can still take time. Audio retains a 60 ms reserve in the USB variant,
versus the previous 20 ms, without changing synthesis frequency or repeating
samples. These changes need comparison on the console; they do not themselves
fix all lag or floor shaking. The normal PCSX2 ELF retains its configuration.

`MEMORIA OK` checks neighboring buffer and stack preservation, publishing
CPU bytes to RAM, reading new bytes from the physical alias and interrupt
state. It does not check every game allocation or all physical RAM.
Execution in PCSX2 does not certify the real EE cache.

The `package.json` report checks the ELF's static addresses within the EE's
32 MiB. Remaining space alone does not guarantee that all dynamic allocations,
stacks, IOP and VRAM work on hardware.

Controller, audio, video and saving are checked through gameplay: BOOTTEST
does not certify them. No tool automatically copies a PCSX2 save to the real
card. The private folder includes game resources and must not be uploaded
to GitHub; only code and tools will be published when that phase is prepared.

Driver technical reference: [ps2sdk USBHDFSD](https://github.com/ps2dev/ps2sdk/tree/master/iop/usb/usbhdfsd).
Batching avoids repeating the RPC for each sector; the SDK's POSIX interface
is retained. Reference for the underlying call:
[ps2sdk fioWrite](https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/src/fileio.c).
