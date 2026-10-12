# OoT PS2

Experimental native **The Legend of Zelda: Ocarina of Time** port for
PlayStation 2, based on [zeldaret/oot](https://github.com/zeldaret/oot).
The game code is compiled for the Emotion Engine. N64 display lists are
translated to the Graphics Synthesizer, and audio is synthesized with RSP HLE.

## Status — October 12, 2026

The full asset profile converts **101 scenes, 388 rooms, and 378 object
banks**. This allows the map's resources to load, but does not establish
that the entire story, combat, and cutscenes work correctly.

- **PCSX2:** the user has tested Kokiri Forest, houses, the shop, and the
  Deku Tree. Automated tests cover 191 scene entrances; 387 of the 388 rooms
  complete loading, update, and capture tests. The known Gerudo Training
  Ground room hang is documented in [BUGS_PS2.md](BUGS_PS2.md).
- **Real PS2:** tested by the user on an SCPH-90010 using USB and wLaunchELF:
  intro, menus, save loading, leaving Link's house, and obtaining the sword.
  The ground distortion when moving the camera is fixed and confirmed on
  hardware.
- **Graphics and audio:** lighting, fog, transparency, the animated title,
  equipment portrait, and received items work. The fix for the sun appearing
  in three separate parts passes a repeatable castle capture and 49,152
  channel comparisons; this change still needs confirmation on real hardware.
- **Performance:** targets are 20 FPS in gameplay, 30 in file selection, and
  60 for the initial logo. Some views reach 20 FPS; large views still drop.
  The three fixed castle cameras measure 18.84 / 14.70 / 19.98 FPS in PCSX2.
  Stable 20 FPS and unmeasured optimization gains are not claimed.
- **Saves and input:** memory card title and animated 3D icon, save recovery,
  and DualShock 2 support. Virtual memory card tests pass. Rumble is
  implemented but still needs physical verification.
- **Spanish:** an English/Español selector uses a private `ESP.DAT` archive;
  some text remains in English. See [PS2_LANGUAGE.md](PS2_LANGUAGE.md).

The [changelog](CHANGELOG_PS2_PORT.md) records changes and measurements.
[TESTING_PS2.md](TESTING_PS2.md) describes the tests and their limits.

## Game data and distribution

Assets extracted from your own **US NTSC 1.2** copy are required. This
repository does not include ROMs, BIOS files, generated data packages,
memory cards, or save files. Source provenance is recorded in
[UPSTREAM.md](UPSTREAM.md), and component licenses are documented in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The original game and
third-party notices are not replaced with a single license for the port.

The regular USB package is private. An executable with external native
initializers and a local `PS2DATA.DAT` generator is being prepared.
Reconstruction across two different link layouts and startup/rejection tests
for damaged data pass. **A binary release is still pending** validation
and resolution of the linked components' distribution requirements.
See [the procedure and remaining work](PUBLISHING_PS2.md).

## Building with local assets

Requires Linux/WSL, Python 3.10+, PS2SDK/gsKit, and a zeldaret/oot checkout
at revision `1bef952ff61a6dd1945c7887c1babd94efe95f72`. Follow its instructions
to install dependencies and run `make setup VERSION=ntsc-1.2` with your
ROM in `baseroms/ntsc-1.2/`. Its normalized SHA1 must be
`41b3bdc48d98c48529219919015a1af22f5057c2`.

Once extraction is complete, run from this repository:

```sh
export PS2DEV="$HOME/oot-ps2-tools/ps2dev"
export OOT_UPSTREAM="$HOME/oot-ps2-tools/oot-upstream"
source tools/ps2_env.sh
python3 tools/prepare_ps2_includes.py --output build/PS2-ntsc12/generated
bash tools/build_ps2.sh OOT_VERSION=NTSC_1_2 BUILD_DIR=build/PS2-ntsc12 \
  OOT_UPSTREAM="$OOT_UPSTREAM" TARGET=oot_ps2.elf -j2
"$OOT_UPSTREAM/.venv/bin/python" tools/prepare_ps2_rom.py \
  --upstream "$OOT_UPSTREAM" --profile full --output build/PS2-ntsc12/oot.z64
```

The ELF and converted assets are written to `build/PS2-ntsc12/`. For real
hardware, see [PS2_HARDWARE.md](PS2_HARDWARE.md); the package needs additional
preparation and a FAT32 USB drive. Do not run converters on the only copy of
a save file or memory card.

## Development tests

```sh
PS2_SMOKE_USB_CACHE=1 bash tools/build_ps2_smoke.sh
PS2_PERF_SCENE_MODE=castle_gate bash tools/build_ps2_perf_scene.sh
```

The PowerShell scripts in `tools/` use isolated PCSX2 configurations with
a local BIOS. Scene and performance tests disable the player's memory cards
and use synthetic saves. Optimization candidates remain disabled until
correctness and a performance gain have been established.

## Controls

| N64 | DualShock 2 |
|---|---|
| A | Cross |
| B | Square |
| Z | L2 |
| Start | Start |
| D-Pad | D-Pad |
| L / R | L1 / R1 |
| C buttons | Right stick |
| C-Up (backup) | Triangle |
| C-Down (backup) | Circle |
| C-Right (backup) | R2 |
| Joystick | Left stick |
