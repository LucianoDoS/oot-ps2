# Port checks

From PowerShell 7, in the project folder:

```powershell
./tools/run_ps2_regression.ps1
```

The run builds and tests graphics, audio, SRAM, memory cards and Skulltula
contact. It uses the SDK in Ubuntu WSL, the NTSC 1.2 extraction and the
already prepared local PCSX2 configurations. Resources for the `full`
profile must exist in `build/PS2-ntsc12/oot-full-next.z64`.
It does not download ROMs or BIOS files.

Each run generates `build/PS2/regression/<fecha>/report.json`, with executable
hashes, timings and logs from that run. Each emulator has a fresh log;
a previous PASS cannot serve as evidence for a new test. A failure,
EE exception, premature exit or timeout produces a FAIL report.
At completion, only diagnostic processes identified by their PID and
executable are closed. The gameplay configuration remains open.

The card test creates a new image under that same directory. It verifies
two alternating writes, recovery from a damaged copy, persistence after
closing the emulator, reading the 3D icon, repairing its files and retaining
SRAM after an icon failure. Only that new card receives the formatting
token. Graphics and enemy tests disable both memory cards.

To add the automated entrance sweep across all scenes:

```powershell
./tools/run_ps2_regression.ps1 -IncludeSceneSweep
```

These are 191 entrances covering 101 scenes and their age/time variants.
Each case loads the scene, runs 60 updates and destroys it. The additional
`scenes.json` report checks the log and overlay restoration. It does not
traverse every room or solve combat and puzzles. `-SkipBuild` allows repeating
a run with existing ELFs and records their hashes.

## Specific cases

### Title cutscene performance

```powershell
./tools/run_ps2_perf_scene.ps1 -Mode title_cinematic
```

This mode builds a separate ELF and runs the original title initializer.
It measures 480 frames after 30 warmup frames in Hyrule Field, with the
original moving camera, adult Link and Epona. Their positions, animations
and clock follow the original behavior. Seed `0x12345678` is also applied
when `Play_Init` reseeds the generator, before initializing actors.
The verifier requires cutscene progress, camera movement and records of
Link riding the loaded horse.

It uses a synthetic save, HostFs and a private configuration with both
memory cards and all eight controllers disabled. It checks that the player's
card retains its SHA256. Each run saves the ELF, data image, configuration,
log, GS capture, `fixture.json`, `report.json` and `summary.json` in
`build/PS2/perf-scenes/<fecha>/`.

Frame timing and movement rows are stored in memory and written after
the measured window. The capture is also obtained outside that window.
Detailed profiles are sampled every 60 frames in the case; their frames
are excluded from normal timings, and cadence also excludes the next
interval. The summary shows graphics translation, submission, presentation,
cadence and cutscene blocks to locate drops.

To compare a new version against a previous run:

```powershell
./tools/run_ps2_perf_scene.ps1 -Mode title_cinematic -Baseline 'build\PS2\perf-scenes\<fecha-base>\summary.json'
```

This command rebuilds. Add `-SkipBuild` to repeat the already prepared ELF;
pending code changes are not incorporated in that case.
`-FixtureRoot 'build\PS2\perf-scenes\<fecha>'` allows repeating a preserved
ELF and its data, together with `-SkipBuild`. Their hashes are checked and
a mode different from the requested one is rejected. The default `house_path`
mode retains the three fixed views outside the house; its results are not
compared with cutscene results.

A comparison sets `comparable=true` only if case data, ROM, emulator,
configuration, sample indices and complete geometry and movement sequences
match. `capture_equal` separately compares pixels in the final capture.
If conditions do not match, it does not calculate the translation improvement
percentage. The capture needs visual review: it does not test every frame
or complete fidelity. These are emulated EE timings with data through
HostFs; they do not establish USB or physical PS2 performance, all scenes,
walking stability or a guaranteed FPS rate.

### Sun and castle entrance performance

```sh
PS2_PERF_SCENE_MODE=castle_gate bash tools/build_ps2_perf_scene.sh
```

```powershell
./tools/run_ps2_perf_scene.ps1 -Mode castle_gate -SkipBuild -TimeoutSeconds 150
```

The fixture measures three cameras with original entrance 0x047e, scene 95,
fixed time 16:00 and a synthetic save. It disables controllers and both cards.
It saves timings, the geometry sequence, environment per task and a GS capture.
`-Baseline <summary.json>` requires identical identity and sampling for comparison.
The original environment advances ten ticks before drawing; that value is validated.

The baseline with the corrected sun obtains 18.84 / 14.70 / 19.98 FPS in
PCSX2. It does not demonstrate stable 20 or a hardware result. The sun oracle
compares 49,152 channels; the VTX loop oracle retains 192 exact states.
VU0 remains disabled: `PS2_SMOKE_VU0=1` runs its separate diagnostic,
which has failed exact comparison in 208 of 512 cases. A SKIP for that
candidate in the normal suite does not mean VU0 passed.

### Triangle packets and USB animation cache

```powershell
wsl -d Ubuntu -- bash -c 'PS2_SMOKE_LOCALE=1 PS2_SMOKE_USB_CACHE=1 bash tools/build_ps2_smoke.sh'
```

The graphics test includes `PS2 TRIANGLE PACKET TEST`: it decodes SDK and
compact packets and compares the complete GS register sequence. It tests
padding, fog/Q, both contexts, filters/palettes, odd sizes, alignment and
DMA block changes. Its benchmark measures packet construction, not FPS.
`PS2 USB CACHE TEST` interleaves small animation frames with samples:
it requires 16 reads for 64 sectors, none on repetition, and retains
20 sample sectors. It uses a generated file without game data.
Floor layer, clipping, audio tests and the diagnostic's 120 frames must
also pass. `PS2_SMOKE_USB_CACHE=1` is required to check the USB configuration,
which differs from the one used with HostFs.

### Language and logo cadence over native USB

With private `ESP.DAT` already generated alongside the converted image:

```powershell
wsl -d Ubuntu -- bash tools/build_ps2_locale_game_smoke.sh
./tools/run_ps2_locale_game_check.ps1 -Python 'C:\ruta\python.exe'
```

It requires the same Python environment as the USB test, plus Pillow.
It runs the original Nintendo 64 logo and measures actual presentations
after 30 warmup frames. It then changes the options to Spanish, checks that
saves do not change when saving and reloading the SRAM header, captures
the language row and a dialogue, and completes house → Kokiri → house.
Both player cards and HostFs are disabled. The logger completes its writes
before closing the emulator; logs and SHA256 values for both data files
are checked. It saves `language-game-check.json` alongside the USB report.
It does not demonstrate language persistence on a physical card.

To check every message, texture, glyph and partial read on EE, add
`PS2_SMOKE_LOCALE=1` when building `tools/build_ps2_smoke.sh`; without that
flag, the graphics test does not need translation data. The packager also
rejects an incorrect ESP.DAT version, size, range or CRC.

To check USB executables and their logs:

```powershell
./tools/run_ps2_usb_checks.ps1
./tools/run_ps2_usb_stack_check.ps1
```

The first script tests valid/corrupt data, startup and reading old and
sector-padded logs through HostFs. The second runs the native USB ELF
with a private FAT32 image and PCSX2's USB storage device; HostFs and both
cards are disabled. It compares saved stages against EE output, rejects
corruption/USB errors and checks that data retains its SHA256.
It requires the Python environment specified by `-Python`, with `pyfatfs`
and `setuptools<81`. Physical usage and limitations are in `PS2_HARDWARE.md`.
These do not replace a console test or format the user's USB drive.

To check leaving the house and returning with the USB diagnostic:

```powershell
wsl -d Ubuntu -- bash tools/build_ps2_usb_transition_smoke.sh
./tools/run_ps2_usb_stack_check.ps1 -Transition -Seconds 55
```

The test uses a separate ELF and a synthetic save, without cards or controller.
It completes 120 updates in house → Kokiri Forest → house and requests the
original fades and scene changes. The report requires all three stops,
completion of the traversal, intact logs and preserved data SHA256.
It detects the game thread ending incorrectly during a state change.
It does not physically traverse the door or validate the player's save.

To traverse all referenced rooms and review their captures:

```powershell
./tools/run_ps2_room_smoke.ps1
```

This separate diagnostic selects all 388 rooms from the converted image's
room lists. It uses an age chosen per scene, test saves and synthetic
positions obtained from spawn points, doors or actors.
It keeps Link still so he does not leave the selected room. The test
executable blocks automatic requests for another room caused by those
synthetic positions and logs their count; it does not modify the normal ELF.
It loads, updates and draws each room for at least 45 frames; it checks
actor lists and loaded object banks. It runs the actual sword/shield/rupee
receipt functions and switch/chest flags, immediately restoring all state
it modified.

It generates `build/PS2/room-regression/<fecha>/report.json`, `rooms.json`
and an `index.html` gallery, with room search, PNGs and contact sheets.
Captures come from the GS framebuffer during rendering, not the PCSX2
window. The validator requires a result and a complete image per case;
it detects exceptions, exits, missing cases and lack of progress.
The player's card is excluded: both ports are disabled.

`-Start 12 -Count 2` isolates a selection; `-SkipBuild` only accepts the
executable and fixtures that match the requested parameters.
Reports and captures are stored in a new directory per run.
Nearly black images are flagged for review: they may result from the
synthetic camera, lighting or a graphics bug. They are not automatically
considered port errors or proof of visual fidelity. Flags do not simulate
opening every chest, crossing every door, solving puzzles or defeating bosses.

If a room fails, the run preserves FAIL status and generates a partial
gallery with previously verified captures. It can continue from the next
case using `-Start`, and reports can be merged with
`tools/merge_ps2_room_reports.py`. The merge preserves provenance and
unfinished cases; it does not convert a failure to PASS. `DemoKankyo`
hides the geometry of `hiral_demo` in the original game; the diagnostic
allows that case if it retains the loaded buffer and all other checks pass.
Known lockups are recorded in [BUGS_PS2.md](BUGS_PS2.md).

Initial result from October 4: 388 cases attempted in separate runs,
387 complete rooms across the 101 scenes and one reproducible lockup in
`men_room_2` (case 201). The merged gallery retains FAIL and that pending case.
The false failure of `hiral_demo_room_0` is resolved in an isolated run
after recognizing the geometry hidden by `DemoKankyo`.

| Case | Automated check | Manual verification pending |
| --- | --- | --- |
| Deku Tree Skulltulas | Seven original positions: lowering, landing, OC contact with Link's cylinder, pushback and return to the ceiling. Retreat over an elevation change at heights 799/820. Poses compared with the original extraction. | The user confirms the spider in room 10 is fixed; the other rooms and physical PS2 still need testing. |
| Memory card | Reading every byte of `icon.sys` and `triforce.icn`, persistence, repair and save data. The installer compares both complete SRAM files before and after. | Name, position, eight-pose rotation and final speed/lighting adjustment confirmed in PCSX2's BIOS; repeat on physical PS2. |
| Materials | Texels, alpha masks, scrolling, cache and framebuffer capture with controlled inputs. | New cases with material combinations not yet covered. |
| Progression | Entrance load/destruction and restoration of actor states. | Key items, doors, combat, puzzles, age changes, ending and reloading an advanced save. |

The gameplay log in room 10 places the spider's floor at Y=720 and the
chest platform at Y=820. The lowering/return alternation is also reproduced
with original `EnSt`. The user confirms that it stays too low both before
and after crossing. The port adds a complete retreat to the ceiling when
the next downward step would bury its body in the highest surface supporting
Link. This is a PS2-only behavior adjustment, not a correction to model or
floor data. It retains normal descent at ground level, damage and contact
cylinders; it does not create an invisible barrier.
The user confirms that this correction now resolves the observed issue.

Reports distinguish verified behavior from full progression and physical
console testing, which remain pending. The USB package checks data and
static memory ranges; USB loading and memory during gameplay still need
console verification. See [PS2_HARDWARE.md](PS2_HARDWARE.md).

## USB package and startup checks

```powershell
wsl -d Ubuntu -- bash tools/build_ps2_usb.sh --host-test
./tools/run_ps2_usb_checks.ps1
python tools/package_ps2_usb.py
./tools/copy_ps2_usb.ps1 -DriveLetter D
```

Private output is stored in `build/PS2/usb-ready/OOTPS2`. The USB variant
uses separate entry and read objects; it does not replace the normal PCSX2 ELF.
The diagnostic queries the card and data without writing saves.
Local tests use a fresh configuration with both ports disabled: valid data,
incorrect size, altered block and game startup through the checked path.
The test variant uses `host:` and states that in its log; it does not
validate reading from a physical USB device. Two other checks load the actual
USB modules in PCSX2's IOP and require the bounded missing-file error:
in BOOTTEST and in the game with controller, card and sound modules already
loaded. They check driver startup without a device.
Valid game startup also requires a BOOT.LOG with module, video, controller,
saving, audio, vblank, data opening, first state initialization and first
frame stages. BOOTTEST must display the end of a log larger than its buffer
and preserve its SHA256; data errors must not create one.
The file is startup evidence, not certification of the complete game.
BOOTTEST also requires preservation of neighboring buffers, stack, physical
alias, CPU payloads and interrupt state. In PCSX2, this check does not certify
the hardware cache. The CPU/RAM model is run with:

```powershell
wsl -d Ubuntu -- python3 tools/test_ps2_cache_ranges.py
```

It tests unaligned ranges and exact line boundaries, untouched neighbors,
requested writeback, zero/negative sizes and explicit global writeback.
A negative variant with the previous global invalidation must fail through
neighboring data loss. The range functions use
[the SDK's SyncDCache](https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/src/glue.c),
which rounds boundaries to EE lines and preserves interrupt state.
Copying checks that the chosen drive is removable FAT32 and verifies all
files through SHA256. It copies only the OOTPS2 folder; it preserves other
files and backs up previous package files if they will change.
It does not format the USB drive.

## 3D icon

`tools/generate_ps2_save_icon.py` converts the local Triforce model into an
icon with 96 triangles, volume, bevels and a central opening. It writes the
title in Shift-JIS: «Legend of Zelda» / «Ocarina of Time».
The build embeds it in the ELF; the game installs or repairs only
`icon.sys` and `triforce.icn` in `OOT_PS2`, with readback verification.
It skips writes if the files already match. An icon failure does not disable
the save backend. SRAM data remains in its two alternating files and retains
its format.

The format is compared against the [PS2SDK example](https://github.com/ps2dev/ps2sdk/blob/master/ee/rpc/memorycard/samples/mc_example.c),
the [ps2iconsys converter](https://github.com/ticky/ps2iconsys) and the
[ps2mc-browser reader](https://github.com/caol64/ps2mc-browser).
`tools/check_ps2_save_icon.py` decodes the structure, title, geometry,
normals, animation and texture; it can generate a preview with Pillow.
That image is not a capture of the BIOS browser. Generated icons, vertices
and C files are stored in `build/`, outside Git.

The current presentation centers the model at Y=-1.5, uses a gold gradient
and animates eight poses around the vertical axis. The cycle covers
160 animation frames, with speed 0.35. The model takes 56208 bytes.
The reader checks that curves sum to one between keys, that the loop is
continuous and that rotation retains height. A 16-pose variant passed
byte checks and card readback, but the BIOS displayed it as corrupted data.
The eight-pose variant was accepted and confirmed by the user.
This does not establish a universal BIOS limit or replace testing on a real console.

To update only the presentation of an existing save:

```powershell
wsl -d Ubuntu -- bash tools/build_ps2_save_icon_install.sh
```

The resulting executable, `build/PS2/oot_ps2_save_icon_install.elf`, requires
an already formatted PS2 card and a save in `OOT_PS2`. It does not format
or write SRAM: it installs the model and `icon.sys`, verifies both and
compares every byte of `save.dat` and `save.bak` with their previous reads,
including their absence. Before using it on the gameplay card, close the
emulator and copy the card image as a backup. Test first on a copy with an
isolated PCSX2 configuration. The message `PS2 ICON INSTALL: PASS` only
establishes installation and preservation of the files; appearance is
checked afterward by opening the BIOS. The installer does not modify other
card directories; an old diagnostic directory is independent of the save.
