# Floor comparison on PS2

The user confirms that the Up mode in FLOOR v3 eliminates the waves on their
real PS2. OOTPS2.ELF, DIAG.ELF and DIAG224.ELF already apply that calculation
automatically. Use OOTPS2.ELF to play; FLOOR.ELF remains an optional comparison
with the previous coordinates.

Run `mass:/OOTPS2/FLOOR.ELF` from wLaunchELF and load your save.
Find the floor where waves appear when rotating the camera.

With the inventory closed and no dialogue or transition active, press a
D-pad direction to choose the mode. No restart is needed:

- Up: centered UV coordinates for the second layer, retaining both textures.
  This is the initial mode in this version.
- Left: only the first layer. The floor's appearance changes intentionally.
- Right: the previous second pass, for comparison with the original waves.

Rotate the camera in the same location for about ten seconds in each mode.
Record which mode makes the waves disappear, decrease or remain unchanged.
Use the joystick to move Link; the D-pad selects these modes during the test.
The change takes effect on the next complete frame. It does not change items,
enemies, saves or cutscenes. Inventory and dialogue controls retain their behavior.

This ELF uses the game's normal save system. If you make new progress,
save it from the menu before turning off the console.

FLOOR.ELF replaces BOOT.LOG and DIAG.LOG at startup. Copying the diagnostic
preserves the previous logs on the PC. After the comparison, reconnect the
USB drive to the PC to collect DIAG.LOG before running another diagnostic.

The log identifies this version with `FLOOR_LAB v3` and records `FLOOR_MODE`,
the scene and the frame for each selection.
Every sixty frames with floor material, it records a triangle: RDP modes,
scale, UV coordinates, W and the STQ/XYZ values sent to the GS. It includes
the second pass's depth, dimensions, masks and offsets.
`PS2 FLOOR UV` adds the maximum values and out-of-range coordinates for all
floor triangles in one frame every sixty. These are bounded samples, not
a capture of every polygon. Logger writes can introduce pauses; this
variant is intended for image comparison.

The user confirms that the waves persist with both DIAG224 and the clipping
precision fix. The first FLOOR.ELF comparison retains the waves in normal
and forced-perspective modes; omitting the second layer removes them along
with part of the texture. The log confirms enabled perspective, selections
in Kokiri and no USB errors or losses.

The second version compared read-only depth with a minimal offset against
the previous policy. The user confirms the same waves with both.
Its physical log is preserved in
`build/PS2/hardware-runtime/floor-v2-result-20261005-181656`.

The third version restores the original depth in both modes and changes
only the second layer's coordinates. It subtracts the same integer number
of repeats from all three vertices before multiplying by Q. It retains
scale, perspective, blending, fog and position. It does not apply modulo
to each vertex: that would distort the texture between corners. It respects
already expanded mirrored repeats and leaves clamped texture axes unchanged.

The GS limits texel coordinates to ±2047 and discards eight low bits of the
STQ mantissa. Large coordinates can lose precision before wrapping
([GS manual, section 3.4.4](https://raw.githubusercontent.com/ninjadynamics/PS2Docs/main/GS_Users_Manual.pdf)).
The physical v2 sample reaches about 2058 units in the second layer with
32 pixels; this motivates the comparison, but does not yet prove it is the
only cause of the waves. Centering cannot reduce the span of a triangle
that already covers more than 4094 texels: the log also records those cases.
Right restores the previous UVs exactly. The user now confirms that Up
corrects the image on the SCPH-90010. The physical v3 capture is preserved
in `build/PS2/hardware-runtime/floor-v3-confirmed-20261005-184050`:
24 sampled frames with maximum UVs of 2222.5 before and 411.188 after,
58 out-of-range coordinates before and zero after. It does not certify other areas.
The normal game includes the calculation without this variant's controls or
measurements, and retains depth, blending, fog and both textures.

To return to the normal game, run `mass:/OOTPS2/OOTPS2.ELF`.

## Preparation from source

```bash
bash tools/build_ps2_floor_diagnostic.sh
python3 tools/package_ps2_floor_diagnostic.py
```

In PowerShell, with the FAT32 USB drive connected as D:

```powershell
.\tools\copy_ps2_floor_diagnostic.ps1 -DriveLetter D
```

The additional package contains FLOOR.ELF, SUELO.txt, its hashes and a manifest.
It uses the same verified OOT.Z64 already present in OOTPS2. The script
checks that data and the files it copies; it preserves the normal ELFs.
