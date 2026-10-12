# Publishing the port

Repository: [LucianoDoS/oot-ps2](https://github.com/LucianoDoS/oot-ps2).
Experimental sources are published with their authors' notices. This does not
assign a new license to the original game code or third-party components.

## Executables and game data

The package created by `tools/package_ps2_usb.py` is private: it contains
assets generated from a local copy of the game. Do not upload it as a release.
The regular ELF also contains native game initializers; separating only the
converted ROM is insufficient to prepare a public executable.

`PS2_EXTERNAL_ASSETS=1` uses a mathematically generated geometric icon and a
16 KiB catalog inside the ELF. `tools/prepare_ps2_external_assets.py` extracts
the exact ranges into `PS2DATA.DAT`, zeroes those bytes in the ELF, and
produces `native-assets.json` with identities, addresses, sizes, relocations,
and CRCs, without resource contents.

The destination catalog belongs to the executable. The DAT supplies no
addresses. The loader validates the header, size, EOF, global CRC, and every
block before copying anything, then synchronizes the cache. Loading occurs
before the actors' initial data are snapshotted.

The tested reference contains 223 resources and 260,948 bytes. To generate
data compatible with a published ELF, the generator reads relocations from
the local build's MIPS objects and modifies only marked words. It does not
interpret integer values as potential pointers.
Identities, sizes, offsets, final contents, and release catalog CRCs are
validated. A different version or ABI must fail clearly.

## Generating private data for a future release

This procedure requires the same source revision as the release, PS2SDK,
a local **US NTSC 1.2** extraction, and the upstream specified in `UPSTREAM.md`.
The normalized ROM SHA1 is `41b3bdc48d98c48529219919015a1af22f5057c2`.
It does not download game data. `release/OOTPS2.ELF` and
`release/native-assets.json` must come from the same public package.

```sh
export OOT_UPSTREAM="$HOME/oot-ps2-tools/oot-upstream"
python3 tools/prepare_ps2_includes.py --output build/PS2-ntsc12/generated
bash tools/build_ps2.sh OOT_VERSION=NTSC_1_2 BUILD_DIR=build/PS2-ntsc12 \
  OOT_UPSTREAM="$OOT_UPSTREAM" TARGET=oot_ps2.elf -j2
python3 tools/ps2_release_relocations.py \
  --metadata release/native-assets.json --release-elf release/OOTPS2.ELF \
  --reference build/PS2-ntsc12/oot_ps2.elf \
  --generated build/PS2-ntsc12/generated --output private/PS2DATA.DAT
"$OOT_UPSTREAM/.venv/bin/python" tools/prepare_ps2_rom.py \
  --upstream "$OOT_UPSTREAM" --profile full --output private/OOT.Z64
```

For gameplay, both private files must be beside the USB ELF.
`ESP.DAT` is optional and generated separately as described in `PS2_LANGUAGE.md`.
None of these three files is distributed.

## Completed checks

- 32 cases using the actual C loader compiled on the host: valid data,
  corruption, truncation, invalid destinations, overlaps, and allocation
  failure. No failure partially writes the resources.
- Reconstruction from two ELFs with different layouts: 165 relocations,
  156 changed destinations, and a byte-identical resulting DAT.
  Three altered metadata cases are rejected.
- PCSX2 with HostFs: valid data reach the initial logo; missing, corrupt,
  and truncated data are rejected before drawing the game.
- The same four cases pass with the emulated native USB driver and a private
  FAT32 image. The image's data remain unchanged and the player's memory
  card retains its hash.

These are startup and rejection tests. They do not yet verify transitions
through a gameplay session or the new loader on a real PS2.

To repeat startup tests after preparing a private reference:

```powershell
./tools/run_ps2_external_assets_check.ps1 -Prepared build/PS2/external-prepared
# Only for a reference compiled without PS2_USB_HOST_TEST:
./tools/run_ps2_external_assets_check.ps1 -Prepared build/PS2/external-native-prepared -NativeUSB
```

## Remaining work before a binary release

Remaining work includes transitions and physical testing of the external
loader, a public package with hashes and corresponding sources, and review
of the linked objects' licenses. Audio HLE uses GPL2 or later; the adapted
SDK capture code and several libraries use AFL2; audsrv has LGPL2 notices.
Preserving the texts or renaming the license does not by itself resolve
the combined executable's distribution requirements.
See [provenance and notices](THIRD_PARTY_NOTICES.md).

Until this is resolved, the regular ELF and private DAT are not uploaded.
The initializer audit does not certify every right in the game's sources.
ROMs, BIOS files, extracted assets, memory cards, save files, and logs are excluded.
