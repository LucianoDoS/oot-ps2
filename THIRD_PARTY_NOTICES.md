# Source provenance and licenses

This repository combines the port supplied by the user, platform changes,
OoT reconstruction sources, and third-party components. No single license
changes the rights in all of these files.

- The reconstruction base comes from [zeldaret/oot](https://github.com/zeldaret/oot),
  revision `1bef952ff61a6dd1945c7887c1babd94efe95f72`. See `UPSTREAM.md`.
  No new license is assigned to the original game code.
- `src/platform/ps2/audio/rsp_hle` contains Mupen64Plus RSP HLE code.
  Its headers specify GPL version 2 or later. The license text and author
  list are preserved in `LICENSES` inside that directory.
- The graphics capture adaptation preserves PS2SDK's AFL 2.0 notice in
  `src/platform/ps2/gfx/ps2_frame_capture.c` and the license text in
  `src/platform/ps2/gfx/LICENSE.ps2sdk`.
- The IOP audsrv driver comes from PS2SDK revision
  `c318b136294ed225863b580e115988b05751cf59`. Its [original headers](https://github.com/ps2dev/ps2sdk/blob/c318b136294ed225863b580e115988b05751cf59/iop/sound/audsrv/src/audsrv.c)
  specify GNU Library General Public License version 2. Those notices,
  `src/platform/ps2/iop/audsrv/UPSTREAM.md`, and the text in
  `src/platform/ps2/iop/audsrv/LGPL-2.0.txt` are preserved. The `LICENSE`
  file in that directory also preserves the SDK's general AFL 2.0 license;
  it does not replace the individual source files' specific notices.
- The external toolchain and libraries are not included. The license text
  published by [PS2SDK](https://github.com/ps2dev/ps2sdk/blob/master/LICENSE)
  identifies AFL 2.0. The [GNU compatibility list](https://www.gnu.org/licenses/license-list.html#AcademicFreeLicense)
  classifies AFL as incompatible with GPL. Distribution of the combined
  binary requires resolution of those requirements; renaming the port's
  license is insufficient.

SPDX identifiers in new tools apply only to those files. They do not grant
rights to game ROMs, graphics, models, text, music, microcode, or trademarks.
Extracted data remain local.
Imported reconstruction sources also contain some inline data, such as debug
fonts and a shadow mask. Their provenance is the same as the game code's:
publishing the sources does not assign them a new license or claim that
the entire tree is free of original graphics data.
