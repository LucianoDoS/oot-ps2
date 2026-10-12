# Changelog — PS2 port review and fixes

This document records everything reviewed, fixed, and verified in the
Ocarina of Time PS2 port. It lets you resume work knowing exactly what
changed, why it changed, and what remains to be done.

## Castle sun, repeatable tests, and publication preparation — 2026-10-12

- `castle_gate` reproduces the sun split into three parts, using three
  original Hyrule entrance camera views, a fixed time, and a synthetic save.
  The test preserves the camera, environment, geometry, and captures for each
  graphics task; it does not touch the player's save. Time checks account for
  the environment's original advance of ten ticks.
- The original sun display list uses I8 descriptors for its packed I4 strips.
  Compatibility is restricted to its exact selectors, dimensions, masks,
  clamp settings, and two sources. It separately blends daytime and evening
  intensities, tints, and alpha. Generic fairy cancellation is unchanged.
  `20261011-192833-141/sun.png` shows a complete sun. All 49.152 channel,
  mutation, alpha, and cache checks pass.
- Post-fix baseline: 18,8420 / 14,6955 / 19,9798 FPS;
  translation takes 40,58 / 56,62 / 25,43 ms. The 4096-entry RGB cache,
  deferred projection, and LTO brought no useful improvement and remain
  disabled. The split VTX loop is opt-in: 18,8429 / 14,7528 / 19,9793 FPS,
  with identical images and geometry; this small difference does not
  establish a performance gain.
- A VU0 candidate using separate multiplications/additions fails exact
  equivalence in 208 of 512 cases. The first mismatch comes from different
  ADD truncation between the FPU and VU in PCSX2, with a one-ULP difference.
  It remains disabled, and its diagnostic runs only when explicitly enabled.
  The oracle's requirements are not relaxed to present this candidate as passing.
- The experimental ELF/DAT split inventories 223 private initializers,
  260.948 bytes, and 165 32-bit MIPS relocations. The generator reconstructs
  the same DAT from two links with 156 different destination addresses:
  it uses object relocations instead of guessing pointers from their values.
  All 32 host C-loader cases and three altered-metadata rejection cases pass.
- All four startup cases pass in PCSX2 with HostFs: valid, missing, corrupt,
  and truncated data. The test validates the logo, not a complete playthrough
  or real hardware. The trusted catalog is inside the ELF; the DAT does not
  control addresses and is checked in full before any writes.
- The same four cases pass through emulated native USB in
  `external-checks/20261012-141801-338`, preserving the image's files and the
  player's card. `floor-production-smoke-20261012-142716` passes with the
  renderer defaults: 120 submitted frames, sun, 192 VTX states, floor, alpha,
  audio, and SRAM. VU0 is explicitly marked SKIP.
- The public repository is `LucianoDoS/oot-ps2`. ROMs, private DATs, BIOS
  files, cards, saves, and logs are not published. The binary release remains
  pending; [PUBLISHING_PS2.md](PUBLISHING_PS2.md) distinguishes technical
  requirements from component license review.

## Title cinematic and controller connection — 2026-10-11

- `tools/run_ps2_perf_scene.ps1 -Mode title_cinematic` measures 480 frames
  after 30 warm-up frames, using the original title initializer, Link riding
  Epona, and a moving camera. It uses a synthetic save, fixed seed, and
  disabled controllers and cards. Motion is recorded with its graphics task,
  and rows/captures are written after the measured segment. `-Baseline`
  requires matching configuration, samples, motion, and geometry.
- The title lighting comparison (`20261011-150455-022`, 256;
  `20261011-150919-846`, 1024) preserves images and motion, but reduces
  translation by only 0,82 % and barely changes FPS (18,39 to 18,42).
  Profiling identifies a larger cost: the nine aura masks filter the same
  32 × 32 texel noise again.
- `PS2_FLAME_NOISE_REUSE=1` retains a filtered 64 × 64 grid and a copy of
  the 1024 loaded bytes. Each conversion compares all those bytes; keys
  include source, stride, and scroll. Mask, tint, and LOD are then applied
  through the original calculation. The unmasked S neighbor, which continues
  into the next TMEM row, is preserved. Reuse does not depend solely on
  addresses or frame numbers. It uses 5184 cache bytes and 16 statistics
  bytes; game counters advance only while profiling.
- `20261011-153323-009` passes against `20261011-150919-846`, with matching
  capture, motion, and geometry sequence. Mean translation: 34,96 →
  27,05 ms (−22,61 %); mean FPS: 18,42 → 19,91. The final 60-frame block
  improves from 14,61 to 19,65 FPS. Profiling records one build and eight
  reuses per aura frame, while preserving its GS uploads. This does not yet
  establish stable 20 FPS in every area or on real PS2 hardware.
- `floor-production-smoke-20261011-152438` passes. The new oracle checks
  327680 channels against an independent reference: two masks, tint/LOD,
  mutation within the same frame, padding, stride 32/40, and fractional/wrapped
  scrolling. Existing flame, Skulltula, floor, transparency, audio, and SRAM
  tests still pass. The player's memory card retains its previous SHA-256.
- A single `[PS2 TITLE PAD]` log entry is added after initial file selection
  to check the controller mask during normal startup and ensure a pending
  asynchronous request does not leave `fileNum=0xFEDC` set. The normal game
  logs `mask=03 fileNum=00ff`, with both ports ready for rumble; the user
  confirms Start works and the save can be entered. Opened executable SHA-256:
  `8d25cbebe57dd96e2ae032eae423759a12e9e121b218fcabcfce630426b2b57d`.

## Lighting cache and rumble-enabled build — 2026-10-11

- Instrumentation is corrected so the 256-entry cache records hits and
  rebuilds at the same cost as the 512/1024 candidates. Counters advance only
  during profiling; no clock reads are added to the normal game.
- New baseline: `build/PS2/perf-scenes/20261011-134417-449`, 256 entries.
  Candidates `20261011-140014-951` (512) and `20261011-135150-107` (1024)
  pass with identical ROM, configuration, seed, samples, geometry sequence,
  and captures per view. The player's cards stay disabled during the test,
  and their file is unchanged.

  | RGB cache | RAM | Translation per view (ms) | FPS per view |
  | --- | --- | --- | --- |
  | 256 | 3 KiB | 35,06 / 32,40 / 39,06 | 19,46 / 19,76 / 19,10 |
  | 512 | 6 KiB | 34,71 / 32,09 / 38,74 | 19,46 / 19,77 / 19,16 |
  | 1024 | 12 KiB | 34,48 / 31,97 / 38,65 | 19,48 / 19,82 / 19,06 |

- The 1024-entry capacity is retained: it reduces translation by 1,1–1,6 %
  and RGB rebuilds from 1279/1306/1386 to 772/821/904 per profiled frame,
  without changing alpha or the original calculations. It uses an additional
  9 KiB. The small timing improvement does not demonstrate a sustained FPS
  gain or stable 20 FPS. These views near the house do not replace the heavier
  physical view or a test walking around the map.
- The previous session's independent normal-vector cache passes its oracle,
  but `20261010-144111-786` gets no hits and increases translation by about
  0,9 %. `PS2_NORMAL_VECTOR_REUSE` remains disabled, as does the color-based
  invalidation candidate that provided no gain.
- `floor-production-smoke-20261011-140625` passes with 1024 entries and
  checks collisions/replacement, complete keys, lighting changes, alpha,
  floor, transparency, audio, and SRAM. The smoke script uses the production
  header's capacity by default and allows an explicit override for comparison.
- `tools/test_ps2_pad_rumble.py` is rerun: PASS. Review confirms that
  `PadMgr_UpdateRumble` discovers the device through `CONT_CARD_ON` and uses
  the integrated motor API. Rumble still needs to be felt on a physical controller.
- The first interactive test detects “NO CONTROLLER”: `EXECCMD` during a
  mode change temporarily cleared the mask, and the N64 logo set
  `fileNum=0xFEDC`. A previously verified connection is retained during the
  request; a pending read returns overrun 4, which PadMgr handles by keeping
  the previous sample. Actual disconnections/errors still return 8. The host
  test now includes real `PadSetup_Init`, the mask used by the title, and a busy
  port whose presence was never verified: PASS.

## Later physical session and repeatable tests — 2026-10-10

- `build/PS2/hardware-runtime/compare-physical-20261008-203055` is preserved
  before changing USB files. Its 195 samples contain all three complete
  SAMPLE/USB/AUDIO rows, continuous and ordered; no old sectors are detected
  at this session's tail. This does not prove that the intermittent write
  failure in earlier sessions is fixed.
- Heavy views remain near 12–13 FPS. Profiles 1200/1500 transform 4439
  vertices and record 1816 triangle commands; translation takes about
  57,50–61,45 ms. There is no clear FPS gain over the previous session. Reduced
  texture uploads explain more than the overall timing reduction: that
  difference is not attributed to direct point writes. Final 18–20 FPS
  segments draw lighter views. The inventory and its transitions are excluded
  from active-gameplay analysis.
- The next optimization cycle uses PCSX2 and fixed cameras, with a ROM,
  configuration, and captures dedicated to the test. The physical PS2 is
  reserved for checking relevant changes; emulator numbers do not replace
  hardware validation.
- The ocarina is checked against the local original code: free-play mode
  deliberately hides the staff and notes while keeping Link and the scene
  visible. Learning/recognition modes do draw the notes. The user confirms
  the confusion concerned free play; this game behavior is unchanged.
- `tools/run_ps2_perf_scene.ps1` runs three Kokiri Forest views with a
  synthetic save, disabled controllers and cards, 30 warm-up frames, and
  120 measured frames per view. Link's position is supported by loaded
  collision. `Play_Init` changed the seed using the clock: the test ELF also
  intercepts that call and checks the effective seed. Profiling is limited to
  frames 60/120 and excluded from normal timings. Captures and rows are written
  after the measured segment.
- The valid baseline `build/PS2/perf-scenes/20261010-141140-235` passes,
  measuring 19,46 / 19,76 / 19,06 FPS with translation of
  34,98 / 32,36 / 38,96 ms. Repeat run `20261010-141648-933` preserves each
  frame's geometry, captures, and timings. Initial test versions are kept for
  diagnostics but are not valid baselines: Link's position first floated,
  and the seed later still depended on the clock.
- The color-based invalidation candidate passes 25 cases/150 points and the
  complete `floor-production-smoke-20261010-140500`. Comparison
  `20261010-141505-782` preserves geometry and captures exactly, but retains
  no textures in these views and adds approximately 0,2–0,3 % to translation
  time. `PS2_COLOR_INVALIDATION_REUSE` stays disabled; it is not presented as
  a performance improvement.
- Rumble: `osContGetQuery` reports `CONT_CARD_ON` only after confirming
  analog mode, actuator alignment, and stopped motors. Previously, the game
  did not discover the Rumble Pak. Negotiation advances through queries,
  without busy waiting; `osMotorInit` validates the port and `OSPfs` state,
  and original pulses are sent to both motors. Disconnection, errors, and
  shutdown attempt to stop the motors; reconnection starts with them off.
  `tools/test_ps2_pad_rumble.py` passes detection, failed asynchronous requests,
  digital controller, pulse, reconnection, and shutdown tests. Real EE
  structures compile; rumble still needs to be felt on hardware.

## Texture comparison, direct writes, and empty synchronizations — 2026-10-08

- Session `build/PS2/hardware-runtime/traversal-physical-20261008-164351`
  matches the delivered package: PERF SHA256
  `3BBC47BF012FEB61A533DCE12BC306175CCC46EBF0C235461EA6B7EBA314A09E`.
  It has 224 reliable complete samples and 12 profiles. The final sector,
  offset 163.840 of 164.352 bytes, matches the previous log byte for byte;
  it joins AUDIO 225 and contains old samples 234/235. The original file is
  preserved, and that tail is excluded. Intermittent writing remains
  unresolved; a zero loss counter does not rule it out.
- Between 111 and 126 seconds, without overlapping reads or writes, there
  are 13 intervals/174 frames: median 13 FPS, translation 60,26 ms, final
  submission 2,43, and presentation 12,37 per frame. No empty audio queues
  are observed in those intervals. The camera has not been verified identical;
  the result does not establish a sustained improvement over the previous
  session. Nested VTX/DL/END no longer close groups; root END flushes them.
  There are still 381/382 boundaries from other states.
- Large profiles 1800/2100 spend 34,50–39,01 ms on triangles, including
  29,11–33,55 ms on preparation/submission. They compare 376–379 kB in
  3,42–4,68 ms, with no new copies, mutations, or evictions. Remaining hashes
  belong only to small ineligible textures. The palette takes 0,163–0,173 ms;
  uploads of 5/7 textures cost 4,54–6,13 ms. These are nested categories,
  not independent timings that can be added together.
- The EE comparator now processes complete 256-byte blocks and retains the
  64/16-byte and scalar tail paths. It skips no content validation, excluded
  padding, either source, or RAM changes. It does not enlarge the 128 KiB ring
  or its 128 descriptors. The 128-byte comparator and reference selector exist
  only in SMOKE. Larger blocks take longer to detect an early mutation; the
  measured heavy views record no mutations, but correctness is still checked.
- Single-layer textured triangles write their points directly into the free
  queue, avoiding a 144-byte copy. When a batch fills, they retain the temporary
  buffer and original flush timing. Two-layer floor rendering, captures, and
  failed draws keep their order; the queue is committed only when the texture
  loads. Floor layers are not grouped together.
- The breakdown before this fix, fixture
  `usb-stack-checks/20261008-172657-381`, finds 248 of 302 boundaries in the
  four RDP SYNC commands of frame 600. Their handlers were already empty;
  now they no longer interrupt the group either. LOAD/TLUT, material changes,
  captures, targets, and root ENDDL still flush the group before execution.
  No effective GS synchronization is removed or deferred.
- `build/PS2/floor-production-smoke-20261008-174515` passes: 338.880
  comparisons and 14.256 additional hash checks for all three blocks,
  alignments, every byte, repeated differences, tails, pairs, and padded rows;
  the previous 233.216/42.430 suite also passes. Disassembly confirms block/
  alignment guards, final offsets, and return to the tail path. Test canaries
  establish results, but do not by themselves establish which reads were omitted.
- In 20 isolated groups, the 256-byte block beats the 64-byte block:
  approximately 17–29 % fewer cycles on individual sources and 18–25 % on a
  traversal of 96 copies/48 KiB. The direct queue preserves 480 points and
  every packet byte in four cases involving overflow, clipping, partial loads,
  and texture failure. With all four SYNC commands, 864 triangles per path
  take 929.009 cycles previously and 863.926 directly. These are emulated tests
  of isolated operations, not game FPS gains or physical proof of stable 20 FPS.
- All 24 display-list traversal cases pass with SYNC, branches, matrices, and
  partial, complete, and final-slot loads. The separate ENV/PRIM differential
  preserves 12 ordered points for both textured and color materials; it requires
  three groups where the previous path emitted four.
- PERF breaks remaining boundaries into 19 categories across two log entries
  that fit within the logger's 320 bytes. It counts only in the sampled frame;
  the categories sum to `otherFences`. The analyzer combines complete, unique
  parts from the same frame; its 14 tests pass. Per-operation timers remain
  disabled in the regular game.


- Final emulated native USB tests pass in
  `build/PS2/usb-stack-checks/20261008-201143-015` and
  `build/PS2/usb-stack-checks/20261008-201419-773`: logo at 59,9 FPS, Spanish
  option and dialogue, house–Kokiri–house with 120 updates per phase. All four
  captures match the baseline byte for byte. The player's cards remain
  disabled throughout these routes.
- The final profile confirms zero SYNC boundaries, but totals 135/302 do
  not fall in frames 300/600: the next actual state change closes the group.
  This case therefore demonstrates neither fewer batches nor higher FPS.
  The new classification allows these materials to be studied on hardware.
- `build/PS2/compare-release-verification.json` links the smoke ELF, both
  route ELFs, and their reports through SHA256. It independently recalculates
  the physical window from the log without using its summary, checks static
  ELF bounds, converted data, language, and preservation of the log and card.
  It does not establish total dynamic memory use or this candidate's real FPS.
- OOTPS2 SHA256: `886825B975700707D3BCE234FD59DDBB584787BD75A12B951D55919BF2E0FD80`.
  PERF SHA256: `7F3B97CE20E43041F42A7D62ABCA14C3ADF31F35027278D1268B6FD45266E540`.

- All 12 files are copied and verified in `D:/OOTPS2`; backup:
  `D:/OOTPS2-backup-20261008-201937`. The subsequent check passes with
  `copied=true`, physical logs and the player's card intact. This candidate
  still needs testing in the same heavy view on the SCPH-90010.

## Reuse during model traversal and physical cache verification — 2026-10-08

- The session is preserved in
  `build/PS2/hardware-runtime/retention-physical-20261007-214734`: 255 usable
  samples and 13 profiles, without damage, duplicates, losses, or USB errors.
  PERF matches the previous package, SHA256
  `8DA83A36DC6A5F724EEFC8687163D54F88463B7E25C43F4DA4E4D7E25573319B`.
  Between 111 and 126 seconds, excluding an overlapping write, there are
  14 intervals and 186 frames: median 13 FPS, translation 60,83 ms, final
  submission 2,85, and presentation 12,01 per frame. That segment has no
  reads, empty audio queues, or increase in its delay counter. The camera
  has not been verified identical: no exact gain over the previous
  11,9 FPS/68,02 ms, or stable 20 FPS, is claimed.
- Large profiles 1500/1800/2100 no longer show new copies, descriptor
  evictions, or overlaps. The remaining 46/45/45 hashes belong solely to the
  small admission threshold: 3.456/3.392/3.392 bytes, versus approximately
  117 KiB previously. Full validation now compares 380–403 KiB and takes
  3,66/3,82/4,92 ms; increasing capacity further does not remove that work.
  The palette takes 166–168 µs, within metadata; TLUT reads and decoding
  fall outside that measurement. Triangles remain the largest cost:
  31,37–33,31 ms, including submission of 25,84–27,80 ms.
- All three large profiles have 45 VTX boundaries, three DL boundaries,
  178 ENDDL boundaries, and 201 from other states. These are reuse
  opportunities, not a prediction of batches saved. The candidate retains
  a group across calls/jumps, matrices, and vertex loads; it invalidates
  exactly the overwritten color slots, points from both layers, and UVs.
  Loads of 64 slots avoid shifting a mask by 64 bits. Pending packets contain
  copied points. Material, viewport, fog, render targets, display-list clipping,
  and root end still close the group; closure when aborting at the guard is
  also preserved. Both floor layers still draw per triangle in their original order.
- The EE suite passes in `build/PS2/floor-production-smoke-20261008-142415`.
  Twenty-four cases compare the previous and new interpreters: nested calls,
  jumps, matrices, partial/full/slot 63 loads, colors, textures, floor, fog,
  depth, decals, clipping, and material changes. Points, final vertices,
  order, and draw counters match. The reference takes 1.778.485 cycles and
  reuse takes 1.258.149: this is an isolated emulated test, not a physical FPS
  increase. The console gameplay improvement remains to be measured.
- Both emulated native USB routes pass:
  `build/PS2/usb-stack-checks/20261008-143259-045` (regular) and
  `build/PS2/usb-stack-checks/20261008-143554-364` (PERF). They preserve the
  logo at 59,9 FPS, Spanish options/dialogue, and house → Kokiri → house.
  Both captures match the previous delivered build; only 35 blinking-eye
  pixels differ from the old baseline. Logs are intact, and the new profiles
  appear in full only in PERF. Data, language, and the player's card retain
  their hashes; cards remain disabled during tests. OOTPS2's static range ends
  at 0x01223C70: this does not establish dynamic memory use. Verification is
  recorded in `build/PS2/traversal-release-verification.json`. Production
  executables do not contain the helper that forces the previous barriers.
- The 12-file package is copied and verified in `D:\OOTPS2`, with replaced
  files backed up in `D:\OOTPS2-backup-20261008-143944`. BOOT.LOG and DIAG.LOG
  retain the archived session's hashes. OOTPS2 SHA256:
  `1DBDE26C6570175E38D4CE0C4A56632D8F6C452CF465D7D17C9BC9C1DAD10894`.
  The next physical test must use PERF.ELF, standing and walking in the same
  slow view; sustained 20 FPS remain unconfirmed.

## Descriptor limit and palette hash reuse — 2026-10-07

- The physical session is preserved in
  `build/PS2/hardware-runtime/setup-physical-20261007-205251`: 285 usable
  samples and 13 profiled frames, without damage, duplicates, gaps, reported
  losses, USB errors, or scratchpad failures. Between 111 and 126 seconds,
  excluding the interval overlapping a log write, there are 14 intervals and
  169 frames: median 11,9 FPS, translation 68,02 ms, final submission 1,96,
  and presentation 13,11 per frame. There are no reads or observed empty audio
  queues in that segment. Sustained cost remains concentrated in the renderer;
  these overall timings include interrupts and other threads' execution.
- New reasons separate the descriptor limit from copy storage. In five
  large-view frames, 358 of 372 evictions (96,2 %) involve descriptors;
  only 14 involve ring storage.

  | Frame | New copies | Descriptor evictions | Ring overlaps |
  | --- | ---: | ---: | ---: |
  | 1200 | 75 | 73 | 2 |
  | 1500 | 74 | 71 | 3 |
  | 1800 | 75 | 71 | 4 |
  | 2100 | 74 | 72 | 2 |
  | 2400 | 74 | 71 | 3 |

- The candidate retains 128 descriptors instead of 64: it adds 3.840 bytes
  of metadata while keeping the 128 KiB ring, 16 KiB maximum, and 256-byte
  minimum per copy. All loaded bytes, including both blend sources, are still
  compared before a hash is reused. The test using 96 sources of 512 bytes
  retains 384 hits after warm-up, then validates 96 content changes.
- 17 palette FNV hashes are retained: 16 CI4 banks and the complete
  CI8 palette. Their validity changes only when a decoded RGBA entry changes;
  every TLUT load still reads its RAM bytes. This uses 72 cache bytes and
  12 counter bytes. Metadata for the five cited profiles takes 3,3–4,9 ms,
  but the palette's share has not yet been isolated on the console.
- The EE suite passes in `build/PS2/floor-production-smoke-20261007-211045`,
  with 233.216 content comparisons and 42.430 hash checks. In 9.600 warmed
  queries to 96 sources, 64 descriptors record 41.295.030 cycles, zero hits,
  and 9.600 hashes; 128 record 6.546.806 cycles, 9.600 hits, and zero hashes,
  with the identical checksum `a9acb698`. The palette passes 4.096 entry/bit
  changes, 21 partial loads, and seven invalid loads. For 8.192 unchanged
  full hashes, the reference records 17.268.078 cycles and reuse records
  303.277, preserving the checksum. These are isolated operations in PCSX2,
  not whole-frame or physical-console measurements.
- PERF adds `[PS2 PALETTE HASH PROFILE]`, included in metadata, and
  `[PS2 MATERIAL RUN PROFILE]`, which counts group boundaries caused by
  vertex, list, end, and other state commands. These boundaries do not
  necessarily correspond to batch flushes with pending primitives. Eleven
  analyzer tests pass. Measuring this candidate's physical gain and reaching
  stable 20 FPS in large views remain pending.
- Both emulated native USB routes pass:
  `build/PS2/usb-stack-checks/20261007-212243-879` (regular) and
  `build/PS2/usb-stack-checks/20261007-212625-070` (PERF). They verify the logo
  at 59,9 FPS, Spanish menu/dialogue, house → Kokiri → house, and intact logs.
  The menu retains every pixel; the dialogue's 35 differences are limited to
  blinking eyes and match the already inspected capture of the previous
  build. Both variants produce the same captures. New profiles are complete
  in PERF and absent from the regular log. Data and the player's card retain
  their hashes, with the card disabled during tests. Production symbols
  confirm 7.680 descriptor bytes, 131.072 copy bytes, and 84 palette/counter
  bytes; they contain no test helpers. OOTPS2's static range ends at
  0x01223AF0; this does not establish dynamic memory use. Verification is kept
  in `build/PS2/retention-palette-release-verification.json`.
- The 12-file package is copied and verified in `D:\OOTPS2`, with replaced
  files backed up in `D:\OOTPS2-backup-20261007-213036`. BOOT.LOG and DIAG.LOG
  retain their original hashes; OOT.Z64, ESP.DAT, and the player's card remain
  intact. OOTPS2 SHA256:
  `D2A3D384E40A08D04C17308F3AD0C80D8BF50846B10BE45A86F77B1E56DAB965`.
  The new physical check must use PERF.ELF in the same large view, standing
  and walking, to compare descriptor pressure and translation time without
  attributing camera changes to an optimization.

## Physical cost of large views and copy renewal — 2026-10-07

- BOOT.LOG, DIAG.LOG, the manifest, and executable identities are preserved
  in `build/PS2/hardware-runtime/draw-detail-physical-20261007-200623`.
  PERF matches the delivered package, SHA256
  `691F99E5CEAF5475BDE26F2DED272C8FEFB19A73634C13FD805C8525DC3D7D86`.
  This session has 225 usable samples and 11 detailed profiles, without
  damaged lines, duplicates, USB errors, or reported losses. It does not
  prove that the previous session's intermittent corruption is fixed.
- Between 75 and 103 seconds, median FPS is 12. Excluding intervals with
  overlapping reads and log writes leaves 24 intervals and 285 frames:
  translation 68,01 ms, final submission 2,43, and presentation 13,47 per
  frame. There are only two reads, totaling 15 ms throughout that segment,
  with no empty audio queue or increase in its delay counter. The game waits
  76–78 ms for graphics; its update takes 6–7 ms. Sustained drops in that
  view are not explained by USB loading.
- In profiles 1200 and 1500, translation takes 58,96/59,30 ms after
  subtracting the measured DSP time. Other threads and interrupts remain
  included. Triangle preparation/submission takes 30,26/28,54 ms, including
  texture lookup and points. Profile 1500 draws 1.720 primitives, prepares
  3.240 points, and converts or uploads no textures. Keys, packets, and floor
  adjustment take 1,41/1,61/0,16 ms; they are not the main remaining cost.
  These categories are nested and must not be added as independent phases.
- Content validation still takes 5,66/6,11 ms and computes 121/122 hashes
  despite reusing almost every texture in VRAM. Previous counters do not
  distinguish limits, evictions, and changes; the entire cost is not assigned
  to one reason without measurement. The final segment with less geometry
  returns to 19,9 FPS; it is not treated as the same camera as the slow view.
- A texture copy with unchanged bounds is now renewed in its existing
  storage when its bytes change. Previously, each change advanced the ring
  and could evict stable copies, even when the live set used only 8 KiB. All
  bytes of both sources are still compared, and the original hash is computed
  with identical row and seed order. Capacity stays at 128 KiB and 64
  descriptors; no additional memory is reserved.
- Validation/eviction reasons are added to `[PS2 TEXTURE CONTENT]`, and
  `[PS2 DRAW SETUP PROFILE]` separates initial triangle preparation, tile
  application, and metadata before lookup. PERF retains one sample every
  300 frames; the regular game does not read those timers.
- Conversion of each eight-bit channel to the GS scale uses an exact
  identity, preserving the endpoint 255 → 128. All 256 values and renderer
  point comparisons pass. For 8.192 isolated conversions, PCSX2 measures
  133.178 reference cycles and 100.537 helper cycles. This does not represent
  a whole-frame gain or a physical measurement.
- The EE suite passes with 233.216 content comparisons, 41.694 hashes,
  repeated changes/rows/palettes/bounds/evictions, floor, depth, fog, clipping,
  and 120 graphics/audio frames in
  `build/PS2/floor-production-smoke-20261007-201638`. Nine analyzer tests
  pass, including partial capture of the new profiles. This build's console
  improvement and stable 20 FPS remain pending.
- Both emulated native USB routes pass:
  `build/PS2/usb-stack-checks/20261007-202701-703` (regular) and
  `build/PS2/usb-stack-checks/20261007-203004-217` (PERF). They verify the
  logo at 59,9 FPS, Spanish menu/dialogue, house → Kokiri → house, and intact
  logs. The menu retains every pixel. The dialogue's 35 differences are
  limited to blinking eyes and were inspected; both variants produce the
  same captures. PERF includes the new measurements and reasons; profiling
  remains disabled in the regular game. The player's cards are disabled
  during tests. OOTPS2's static range ends at 0x012226F0; this does not
  establish dynamic memory use. Package verification is recorded in
  `build/PS2/mutation-reuse-release-verification.json`.
- All twelve package files are copied and verified in `D:\OOTPS2`, with
  a backup in `D:\OOTPS2-backup-20261007-203554`. OOTPS2 SHA256:
  `82CD14F27DC614964AE9E6D3000B280842F5E596D8864EFD1C1ACCB3E09E83E3`.
  Physical BOOT.LOG and DIAG.LOG, converted data, and the player's card are
  verified unchanged. Joint verification passes with the copy checked;
  this build has not yet been tested on the physical console.

## Second PERF measurement and shared key lookup — 2026-10-07

- Before USB changes, logs, the package, and executable identities are
  preserved in
  `build/PS2/hardware-runtime/analysis-cache-physical-20261007-000954`.
  PERF matches the delivered build, SHA256
  `1980B60778A3862F5E0DE48B6A6F60E1CCD8B24810F510C367A01222C76126E9`.
  There are 283 complete samples and 14 profiled frames.
- Slow views remain around 11–12 FPS. Frames 1200/1500/1800 reuse 131
  material analyses and rebuild only eight, taking 64–115 microseconds to
  rebuild. The saving exists, but does not confirm a sustained FPS gain.
  Triangles still cost 38–42 ms, including 11–12 ms of texture lookup.
  Translation excluding measured DSP time takes 63–68 ms; other threads
  remain included.
- The final minute free of overlapping reads and writes contains 580 frames
  and median 19,9 FPS. Less geometry is visible in those final profiles;
  this is not presented as a same-camera comparison. Stable 20 FPS in large
  views remain the pending goal.
- The file contains two samples with sequence 80, an incomplete line, and
  sequence gaps. There are no reported USB/logger errors or reported losses,
  which does not establish integrity. Out-of-order fragments appear near
  32 KiB boundaries; the cause remains unresolved. The driver is not declared
  fixed, and the original log is not rewritten.
- The analyzer retains all complete samples for auditing, but excludes all
  copies of repeated sequences and non-advancing sample times from FPS
  statistics and clean windows. This session has 281 usable intervals.
  Eight tests pass, including the observed corruption and partial profiles.
- Texture descriptor lookup compares the material's common fields once.
  Only a complete hit uses previous VRAM: content, palette, LOD, window
  origins, and colors must also match. On a miss, existing candidate selection
  and reuse criteria are preserved. All bytes of both sources are still read
  and validated.
- The EE test compares both classifications in 5.640 cases, changing every
  descriptor bit, including auxiliary data and padding outside the key.
  The isolated operation of 8.192 hits drops from 1.983.902 to 1.374.749
  emulated cycles. This does not measure a complete frame. The renderer suite
  passes with 120 graphics/audio frames in
  `build/PS2/floor-production-smoke-20261007-002545`.
- PERF adds `[PS2 DRAW DETAIL PROFILE]`: key lookup, GS packet construction,
  and floor coordinate readjustment. These timings are included in lookup/
  submission and must not be added as separate categories. Sampling remains
  one frame every 300; the regular game does not enable these timers.
- Both emulated native USB routes pass:
  `build/PS2/usb-stack-checks/20261007-004040-963` (regular) and
  `build/PS2/usb-stack-checks/20261007-004401-082` (PERF). They check the logo
  at 59,9 FPS, Spanish options, dialogue, and house → Kokiri → house. The menu
  retains every pixel. Dialogue differences are limited to the eyes during
  blinking and were visually inspected. Both emulated logs are intact;
  the regular build enables no profiles, while PERF includes all three new
  measurements in its sampled frames. The player's cards are disabled
  during these tests.
- All four game executables and BOOTTEST are built; twelve files are
  verified in `D:\OOTPS2`, with a backup in
  `D:\OOTPS2-backup-20261007-004727`. OOTPS2 SHA256:
  `653A2C494E4E1CDDD845102A1DC4ADD42ED36F19D007037AE4026BE6C88797B6`.
  Its static range ends at 0x01222170; this does not establish dynamic memory
  use. Preservation of original physical logs, data, and the player's card
  is verified. Joint verification is recorded in
  `build/PS2/shared-key-release-verification.json`. The physical improvement
  from this lookup and timings of the three new phases remain pending.

## Sparse physical profiling and material analysis cache — 2026-10-06

- Both logs and the package identity are preserved in
  `build/PS2/hardware-runtime/sparse-physical-20261006-232358`, before USB
  changes. PERF matches the delivered executable and records a detailed
  sample every 300 frames. There are 270 intervals and 14 profiles, without
  USB/logger errors, null bytes, damaged lines, gaps, losses, or scratchpad
  guard failures.
- The session includes a view around 12 FPS and ends with views near 20 FPS.
  The final minute without overlapping reads or log writes includes 740
  frames and median 19,9 FPS. This is not a same-camera comparison: final
  profiles draw less geometry. Stable 20 FPS in all views are not yet confirmed.
- Slow profiles 1200/1500/1800 take 60–63 ms for translation after subtracting
  measured audio synthesis: 37–38 ms for triangles and 10–12 ms for vertices.
  Texture lookup, included in submission, takes 11–13 ms; those categories
  must not be added together. Other threads remain included. USB requests
  alone do not explain this sustained cost.
- The renderer retains 32 selector analyses in eight groups of four,
  maintaining the last-material fast path and checking the same four keys
  as before. Primitive/environment colors and LOD are not cached as results:
  they are still evaluated on every use. Eviction or collision reconstructs
  the original analysis. Fixed storage is 2.716 bytes.
- The EE test compares 480 cases against the previous single-analysis
  cache, including pressure from 80 materials, color/LOD changes, cycles,
  blends, and restores. Alternating eight materials reduces rebuilds from
  256 to 8; the isolated operation takes 719.741 versus 3.440.332 emulated
  cycles. This is not equivalent to a whole-frame gain.
- The renderer suite passes in
  `build/PS2/floor-production-smoke-20261006-233803`, with 120 graphics/audio
  frames, layers, mutable textures, points, clipping, and transparency.
  All six analyzer tests pass. PERF adds rebuild time and counters for the
  last material, reused analyses, and cache misses; these remain measurements
  included in other phases.
- Regular (`build/PS2/usb-stack-checks/20261006-235133-095`) and PERF
  (`build/PS2/usb-stack-checks/20261006-235523-706`) emulated native USB
  routes pass: logo at 59,9 FPS, Spanish options, dialogue, and
  house → Kokiri → house. The menu retains every pixel; dialogue differs only
  in 35 eye pixels during Link's random blink. There are no errors, losses,
  or damaged guards, and the player's cards are disabled.
- Emulated profile 600 reuses 105 analyses across material changes and
  rebuilds only 10; 4.037 uses retain the last material. All 4.152 uses are
  accounted for, and rebuild cost is 35 microseconds, included in other
  phases. Frames 300 and 900 rebuild no analyses. The regular game does not
  enable these profiles.
- All four game executables and BOOTTEST are rebuilt; twelve files are
  verified in `D:\OOTPS2`, with a backup in
  `D:\OOTPS2-backup-20261006-235954`. OOTPS2 SHA256:
  `7EA6E92ED44D9A4CFA77A7C6BA51AF5E368C9138464B1993F33160BB41205272`.
  The ELF's static range ends at 0x01221d70; this does not establish dynamic
  memory use. Results are recorded in
  `build/PS2/analysis-cache-release-verification.json`. Physical logs, data,
  and the player's card are preserved. The new cache's FPS improvement
  remains to be compared in the same physical view.

## Fourth physical session and triangle preparation — 2026-10-06

- BOOT.LOG, DIAG.LOG, and the package identity are preserved before copying
  anything to the USB, in
  `build/PS2/hardware-runtime/auto-play-physical-20261006-223349`.
  The played ELF matches the previous revision: SHA256
  `91044823E7D56C5BFD7333A8BE4D297366498617BE6E7FA359570724B8ED29CD`.
  There are 225 samples across about 226 seconds, covering title, house,
  and Kokiri. Texture copies and scratchpad are active; there are no
  USB/logger errors, lost records, null bytes, sequence gaps, or damaged guards.
- The final minute contains 41 intervals without read requests or overlapping
  logger writes: 484 frames, median 12 FPS, translation 70,74 ms, submission
  1,82 ms, and presentation 12,36 ms per frame. Maximum write pause is 54 ms,
  and writes total 532 ms. The session does not confirm a sustained FPS gain.
  Camera and gameplay load vary across sessions, and timings include other
  threads; they also cannot establish a regression from the previous session.
- TRI already validates its three vertex indices. They are passed directly
  to submission instead of recovered through ranges, remainder, and division
  by vertex size. Clipped polygons pass -1 and retain full preparation for
  their temporary vertices. Material reuse, color, fog, and ordering of both
  floor layers are unchanged.
- Aligned GS points use six 64-bit writes with identical RGBAQ, ST, and XYZ2
  bits. Floating-point operation order, Q, SDK XY conversion and bounds,
  depth, and GS registers are preserved. Unaligned destinations keep the
  original path.
- The EE suite passes in
  `build/PS2/floor-production-smoke-20261006-225328`: 27.648 point cases,
  256 destinations/alignments and XY/depth bounds, 612 points compared
  against indices calculated by the previous path, descriptor waits, mutable
  textures, layers, fog, transparency, and 120 graphics/audio frames.
  In isolated emulated operations, aligned points take 6.478.808 versus
  7.000.818 cycles for the prepared unaligned path; direct-index submission
  takes 397.750 versus 426.591 cycles. These results are not equivalent to
  whole-frame or physical-console improvements.
- PERF.ELF retains the game and automatic logging with 15-second batches,
  4 KiB transfers, and no HUD. Only one frame in every 300 enables detailed
  timers. OOTPS2.ELF keeps those timers disabled. The analyzer separates
  drawing, geometry, texture content, state, color, points/material, and
  interrupts; it rejects incomplete records. All six tests pass. Phases
  overlap and must not be added as independent costs. This profile still
  needs collecting in the same slow physical view.
- Both emulated native USB routes pass:
  `build/PS2/usb-stack-checks/20261006-230240-257` (regular automatic) and
  `build/PS2/usb-stack-checks/20261006-230538-291` (PERF). They check the
  logo at 59,9 FPS, Spanish options, dialogue, and house → Kokiri → house.
  Regular mode records no detailed profiles; PERF records frames 300 and
  600, with all expected categories. Regular-mode menu and dialogue match
  the baseline byte for byte. In PERF, only 35 pixels in Link's eyes change
  during his random blink. The player's cards are disabled, and both sessions
  finish their writes without errors, losses, or damaged data.
- All four game executables and BOOTTEST are rebuilt, and twelve verified
  files are copied to `D:\OOTPS2`, with a backup in
  `D:\OOTPS2-backup-20261006-230834`. OOTPS2.ELF SHA256:
  `BC968B2353F2D674C824C4FC5945D38DF10AFB0480E75F822FAFBFDE23A01EBE`.
  Joint results are recorded in
  `build/PS2/index-aligned-release-verification.json`. Physical logs, data,
  language, and the player's card are preserved. The new optimizations and
  sparse profile await physical testing; stable 20 FPS are not declared achieved.

## Texture validation and EE internal memory — 2026-10-06

- The previous physical log still identifies drawing as the sustained cost,
  even without USB reads. This revision introduces two renderer optimizations;
  it does not change the game's 20 FPS limit or reduce visual detail.
- A fixed 128 KiB buffer retains up to 64 texel windows. Every use compares
  all loaded bytes, excluding row padding; only a complete match reuses the
  previous XXH32 hash. Changes, large windows, and evictions run the original
  hash. Seed/row order, both blend sources, palettes, and GS retirement waits
  are preserved. The EE comparator processes 64-byte blocks, with aligned
  and unaligned paths and a scalar tail.
- In the isolated EE test of 1.000 unchanged uses, sources of 2–8 KiB take
  approximately 53–68 % fewer cycles than recomputing the hash. This measures
  the emulated operation, not a complete frame or physical PS2. Separate game
  profiling reduces texture lookup, but does not by itself demonstrate a
  comparable FPS gain.
- RSP state, its vertex cache, and reused GS points for both layers move
  into a 10.704-byte block in the EE's 16 KiB scratchpad. A single graphics
  thread uses it; packets are copied into gsKit's normal queue before DMA.
  Calculations, geometry, floor centering, and layer order are unchanged.
  Size/alignment assertions and guards checked each frame are provided.
  `PS2_GFX_SCRATCHPAD=0` retains the RAM path.
- The EE suite passes in `build/PS2/floor-production-smoke-20261006-220856`:
  233.216 comparisons, 40.816 hash/window cases, and 120 graphics/audio frames,
  without failures or damaged guards. It checks mutations in any byte,
  differences that could cancel out, rows, dual sources, alignments, bounds,
  RAM reuse, and buffer evictions.
- The automatic game test passes in
  `build/PS2/usb-stack-checks/20261006-221607-826`: logo at 59,9 FPS, language
  selection, dialogues, and house → Kokiri → house. It enables neither
  triangle profiling nor the HUD; it retains 15-second batches and 4 KiB
  transfers. The menu matches the baseline byte for byte. The dialogue
  capture differs in 35 pixels in Link's eyes during blinking; the scene,
  text, and rest of the player match. Play_Init's random seed depends on the
  clock, so that blink is not a deterministic reference. There are no
  USB/logger errors, null bytes, sequence gaps, or scratchpad errors.
  The player's cards are disabled during tests.
- Periodic logging includes exact uses, hashes, and internal block size.
  The analyzer separates them from detailed profiling and rejects damaged
  lines; all five tests pass. Comparison counters express requested ranges,
  not exact bus reads when exiting early. Both optimizations still need
  measurement in the same real-console view to check how much of the
  50 ms per-frame budget remains.
- All three game executables are rebuilt, and the package is copied with a
  backup to `D:\OOTPS2`. OOTPS2.ELF SHA256:
  `91044823E7D56C5BFD7333A8BE4D297366498617BE6E7FA359570724B8ED29CD`.
  BOOT.LOG, DIAG.LOG, data, language, and the player's card retain their hashes.
  Joint results are recorded in
  `build/PS2/texture-scratchpad-release-verification.json`; physical FPS
  improvement awaits another session with OOTPS2.ELF.

## Third physical session: sustained drawing cost — 2026-10-06

- The new log is preserved before modifying the USB, in
  `build/PS2/hardware-runtime/auto-play-physical-20261006-212132`.
  OOTPS2.ELF matches revision `ecedd06`: SHA256
  `4DE2BCAB437C53A9FE7B99B92603CAD9900F124D704055AD61D1F4A31F90BC35`.
  There are 270 samples over about 271 seconds, moving from title to house
  and Kokiri Forest. There are no USB or logger errors, lost records,
  null bytes, damaged lines, sequence gaps, or clock failures.
- The final minute contains 51 intervals without read requests or overlapping
  logger writes: 660 frames, median 12,9 FPS, translation 62,91 ms, submission
  1,82 ms, and presentation 12,67 ms per frame. The slowest log write takes
  112 ms; writes total 885 ms in the session. Sustained slowdown persists
  outside those writes and loads. The camera is not recorded, and these
  timings include interrupts and other threads: they establish neither a
  regression nor a comparable improvement over the previous session's 13,45 FPS.
- The `clipped` counter is reviewed: it counts triangles rejected for being
  entirely outside the same face of the visible volume, plus those that
  disappear after clipping. It does not count clipper calls or measure their
  cost. The final summary's 880 rejections therefore do not justify assigning
  slowdown to clipping. Automatic logging identifies the graphics path as
  the main cost, but does not yet separate vertex, material, and texture
  preparation in that physical view.
- The previous session's null sector does not reappear; its cause remains
  pending, and this check does not prove it fixed. Audio queues show no empty
  polls, which also does not replace a listening test. USB executables and
  the player's card are preserved; this log review introduces no new game build.

## Second physical session and preparation of texture bounds — 2026-10-06

- The USB is preserved before any changes in
  `build/PS2/hardware-runtime/auto-play-physical-20261006-194851`. OOTPS2.ELF
  matches the previous package: SHA256
  `BC63FC32E85E4B76A9BC117F7043ECCA87949E744BD47A5CF9BF653C36F6C155`.
  About 511 seconds are recorded, covering the title, house, and Kokiri Forest. There are no reported
  USB errors, buffer losses, stopped clock, or file limit reached.
- The 4 KiB transfers reduce the maximum observed write time from 949
  to 128 ms. The final minute contains 56 intervals with no overlapping reads or writes:
  753 frames, median 13,45 FPS, translation 60,55 ms, submission
  1,83 ms, and presentation 11,93 ms per frame. These times include other
  threads; the camera is not recorded, so a comparable
  FPS gain over the previous session cannot be claimed.
- There are 512 null bytes at offset 229376, within the batch ending around
  second 451, with no error returned by the driver. The analyzer flags
  that line and the sequence jump 443 → 446, retains 508 valid samples,
  and discards the damaged fragments. The measured final minute is outside the
  affected sector. The cause of that incomplete write remains unresolved.
  The read, write, failed clock, partial record,
  and sector damage cases pass; FPS are never reconstructed from fragments.
- The isolated game test separates vertices, triangles, and preparation in
  `build/PS2/usb-stack-checks/20261006-195335-041`. It retains both reference
  screenshots and passes house → Kokiri → house. The times from that emulated EE
  profile are not equivalent to the user's physical run.
- Bounds, wrapping, mirroring, and filtering are prepared once per layer during
  consecutive triangle commands. Every state change or upload for
  that layer invalidates the preparation. Each application still records cache
  use to preserve GS waits after an earlier retirement.
  The base → second layer order for each triangle and the centering
  that eliminates the waves on the console are preserved.
- The first EE test of this revision passes in
  `build/PS2/floor-production-smoke-20261006-195955`: contexts, filters,
  wrapping, mirroring, clipping, alternation, and 4.096 applications record
  1.127.846 immediate cycles versus 519.187 prepared cycles. Layers,
  points, Q, fog, transparency, clipping, and 120 graphics/audio frames pass.
  Measuring one operation does not certify console FPS.
- The final revision passes in
  `build/PS2/floor-production-smoke-20261006-200916/report.json`, including
  a texture reused after an earlier GS retirement.
  The 4.096 applications record 1.127.826 versus 519.153 cycles.
  The three USB executables are rebuilt. The automatic native USB
  run passes in `build/PS2/usb-stack-checks/20261006-201033-831`: house → Kokiri → house,
  logo at 59,9 FPS, Spanish text, log closure, 15-second batches, and
  4 KiB transfers. Both screenshots match the
  previous revision byte for byte, and the player's memory cards are disabled.
- The package is copied and verified in `D:\OOTPS2`, with a backup of the
  previous version. BOOT.LOG, DIAG.LOG, and the player's memory card retain their
  SHA256 hashes. The combined test is in
  `build/PS2/tile-binding-release-verification.json`. The new
  material preparation still needs to be measured in the same slow view on the real PS2.

## First automatic console log and texture verification — 2026-10-06

- The physical play session is preserved in
  `build/PS2/hardware-runtime/auto-play-physical-20261006-114209`:
  255 samples, AUTO-PLAY header, no USB errors, losses, stopped clock,
  or file limit reached. The ELF matches the version copied previously.
- The final minute contains 46 intervals with no reads or overlap with
  logger writes: 549 frames, median 12 FPS, 66,82 ms of
  translation, 2,74 ms of submission, and 13,98 ms of presentation per frame.
  These times include interrupts and other threads; they identify the
  graphics path as the main sustained cost, without attributing it to a specific phase.
  The analyzer now separates that window from loads and log writes.
- The logger accumulates 2.885 ms of writes, with one reaching 949 ms. Up to
  eight sectors are grouped per call (4 KiB), preserving 64-byte alignment,
  offsets/sizes in multiples of 512, padding, batch closure, and the 2 MiB limit.
  Startup records `transferBytes=4096`. Partial writes,
  exactly two calls for a batch of about 7 KiB, failures, overflow, and
  batching pass in automatic and diagnostic modes. The latency
  of this revision still needs comparison on physical USB.
- XXH32 checking now also uses MMI for sources aligned to eight
  bytes and arbitrary byte alignments. It uses full LD or LDL/LDR and PCPYLD, preserves
  the four-byte scalar path, and does not round the end of a
  stripe upward. It checks every byte at each binding: it does not skip mutable data or
  retain hashes based only on address or frame.
- The physical log confirms three intervals of the Nintendo 64 logo at
  59,9–60 FPS. That check is no longer pending on the SCPH-90010.
- The final revision passes 120 graphics/audio frames and the cases for
  all 16 alignments, tails, fog, clipping, and two floor layers in
  `build/PS2/floor-production-smoke-20261006-120848/report.json`.
  For 1.000 checks of 4 KiB, the arbitrary-byte path records
  11.606.022 cycles versus 25.939.162 in the previous fixture; this measures
  that operation on the emulated EE, not game FPS or hardware.
- The game test with emulated native USB also passes in
  `build/PS2/usb-stack-checks/20261006-121059-666/language-game-check.json`:
  logo at 59,9 FPS, Spanish options and dialogue, house → Kokiri → house, intact
  data, log closure, 15-second batches, and `transferBytes=4096`.
  Both screenshots match those from the previous reference revision
  byte for byte. The player's memory cards are disabled, and no builds run
  simultaneously during these checks. The FPS improvement on the
  console still requires a comparable play session.
- The version is copied and verified in `D:\OOTPS2`, with a backup in
  `D:\OOTPS2-backup-20261006-121605`. The physical logs and the player's
  memory card retain their hashes. The combined check of the package,
  transfers, screenshots, and preserved files is in
  `build/PS2/hash-bulk-release-verification.json`.

## Automatic logging, FPS counter, and vertex submission — 2026-10-06

- The user confirms that large views still slow down. BOOT.LOG
  and DIAG.LOG on the USB have the same hashes as the previous FLOOR v3
  log; they do not measure the packets/cache of the new
  revision. Another private copy is preserved before updating the USB.
- DIAG.ELF and DIAG224.ELF display presented frames per second,
  using a window of approximately one second. The estimate based on retraces
  is marked with an asterisk if the clock fails. The count is independent
  of the logger: it continues after reaching the file limit or after a
  write fails. The `FPS-HUD` marker after the date in the v3 header identifies
  a new log while preserving the format compatible with existing readers.
- The indicator uses grouped solid sprites, without fonts, USB reads, or
  texture allocations. It is added to the main framebuffer after the game;
  it preserves alpha/depth states and the scissor, and does not draw in the
  inventory portrait. OOTPS2.ELF preserves the image without that counter.
- At the user's request, the regular USB game now creates DIAG.LOG at startup
  with the `AUTO-PLAY` marker, without opening diagnostics. It retains samples of FPS,
  scene/room, loading, audio, and drawing. It has 256 records in RAM and groups
  writes every 15 seconds, without enabling per-triangle timers. DIAG retains
  its five-second batches and profiling every 60 frames. The
  2 MiB limit and stopping the logger on errors do not stop the game; write
  costs are identified in FLUSH. Waiting 20 seconds before powering off
  allows the next batch to close. The previous history is already preserved.
  `PS2_USB_AUTO_LOG=0` allows a comparison build without periodic writes.
- Points constructed by the renderer use an internal entry with explicit
  alignment and uniform registers. This avoids checking the
  RGBAQ/ST/XYZ tags of each vertex a second time. General entries retain
  their validation and the SDK fallback for mixed or misaligned layouts.
  Submitted registers, floor layers, Q, fog, color, and geometry are unchanged.
- The EE comparison of 24 fixtures against the SDK and 120
  graphics/audio frames pass in
  `build/PS2/floor-production-smoke-20261006-000224/report.json`.
  For submission, the validated path records 655.700 cycles and the
  internal path 483.820; this is not an overall FPS measurement. The counter records
  12.250 cycles and 1.648 bytes in its fixture. Its screenshot checks visible text
  and restoration of alpha rejection. The 60/30/20/12/8 FPS cases, long pauses, and
  stopped clock with retrace wraparound pass. This revision still needs measurement on PS2.
- The final test with lightweight automatic logging passes through emulated
  native USB, without HostFs, the player's memory cards, or simultaneous builds:
  `build/PS2/usb-stack-checks/20261006-112018-371/language-game-check.json`.
  It checks the logo at 59,9 FPS, options, Spanish dialogue, and house → Kokiri → house,
  with 120 updates in each phase. It confirms 15-second batches and the absence
  of per-triangle profile records. Data/language are unchanged, the log
  ends without errors or losses, and the test save slots are preserved.
  The options screenshot matches the previous one byte for byte; in the dialogue,
  only 51 pixels of Link's eyes change because of the blink animation.
- When the USB was reconnected, BOOT.LOG had changed, but DIAG.LOG was still
  the old FLOOR v3 log: the previous regular play session did not record performance.
  The new reading is preserved in
  `build/PS2/hardware-runtime/before-auto-light-20261006-112309`.
  That evidence does not yet allow the drops to be attributed to USB, drawing, or audio;
  the next play session with OOTPS2.ELF will allow a comparison.
- The final package is copied and verified in `D:/OOTPS2` (11 files), with a copy
  of the previous version in `D:/OOTPS2-backup-20261006-113145`. The regular ELF
  contains `AUTO-PLAY`; the diagnostics contain `FPS-HUD`. Static addresses
  are within the EE's 32 MiB. Physical BOOT.LOG/DIAG.LOG and the SHA256
  of the player's memory card in PCSX2 are preserved. Private report:
  `build/PS2/automatic-light-release-verification.json`.

## Compact GS packets and USB animation cache — 2026-10-05

- Triangles use GS register lists instead of repeating the
  A_D address for every attribute. They preserve RGBAQ, ST, XYZ2/XYZF2 values and
  texture, depth, fog, blending, and context states. PRIM remains
  an explicit write: PRE does not apply in REGLIST. Entries with
  incompatible alignment or registers use the original SDK function.
- Within each triangle group, TEX0/PRIM are prepared once per layer,
  and TEX1 only when the filter changes. Each state command ends
  the group; a new texture upload invalidates its preparation. The two
  floor layers still alternate per triangle. Texture resolution,
  content, and quality are not reduced.
- The EE comparison against the installed SDK decodes both representations
  and checks every register, including Q and fog. It covers contexts,
  palettes, non-power-of-two sizes, filters, and lists of 3/6/60 vertices,
  as well as arbitrary alignment and DMA block changes. The set occupies
  16.000 versus 28.800 bytes. Two runs of 64 triangles per group
  show 636.876 versus 748.329 cycles and 628.292 versus 827.407 accumulated
  cycles. These measure this operation in PCSX2, with possible
  interrupts; they do not measure physical FPS or the whole render.
- USB retains sixteen 16 KiB game blocks (256 KiB of data), versus
  the previous four. Small animation reads also
  reuse them. The audio handler explicitly identifies its cache:
  music and animations share libultra's NORMAL priority, so
  that priority cannot distinguish them. Its 64 sectors of 4 KiB are preserved.
  The static increase is approximately 193 KiB, without increasing the maximum
  read-ahead size or removing EE cache synchronization.
- The interleaved test of 64 animation sectors and 20 sample sectors
  requires 16 file reads on the first pass and none on the
  second; the samples remain resident. It checks LRU eviction, boundary crossings,
  end of file, guards, and two concurrent readers. New loads
  of large scenes remain limited by the PS2's USB.
- The 120 graphics/audio frames and USB cache pass in
  `build/PS2/floor-production-smoke-20261005-232942/report.json`.
  The native USB house → Kokiri → house run passes in
  `build/PS2/usb-stack-checks/20261005-233611-477/language-game-check.json`:
  both screenshots have exactly the same SHA256 hashes as the previous revision.
  The logo remains at 59,9 FPS in that test, and data/language remain intact.
  The full profile does not yet establish an overall FPS gain.
  The three USB ELFs and the PCSX2 ELF are rebuilt; their static ranges
  fit within the EE's 32 MiB. This revision still needs checking on the console.

## Language, logo pacing, and floor reuse — 2026-10-05

- The minimum of two vblanks is removed only during the Nintendo
  64 logo state. The scheduler still limits menus to 30 and gameplay to 20. The
  presentation test of the actual logo, through emulated native USB, measures 59,9 FPS after
  30 warm-up frames. This does not confirm 60 FPS on the console.
- Private ESP.DAT is added with 2.114 messages and 380 texture resources from
  the eduardo_aj2 v2.2 translation (Patcher64Plus-Tool). They are relocated by ID and
  compatible descriptors, without importing N64 code or geometry. The file
  is loaded once outside the arena; reads of covered glyphs and resources
  use RAM. CRC32, bounds, sorted records, and widths are validated before
  use. If it is missing or invalid, English options and messages are retained.
- LANGUAGE/IDIOMA is selected with left/right in Options and saved with
  B in the SRAM header. Native tables retain their Western index,
  and saves retain their format. The row uses full sans-serif
  words, uniform size and outline, proportional spacing, and IA8 (file
  version 2); it avoids mixing the dialogue font with the options. The system TTF
  stays outside the repo, and its hash is recorded in the private report.
- Explicit segments are bound for the vertices and tags of this row:
  EE addresses above 16 MiB could be mistaken for segmented
  N64 addresses and leave the text blank. Text in code and credits
  is not fully translated; voices and sound retain the originals.
- The renderer prepares floor periods and coordinates once per
  shared vertex in each triangle group. It preserves translation by
  full periods confirmed on hardware, layer order, perspective,
  fog, depth, and clipping. The GS point comparison passes, along with
  the 120-frame audio and graphics tests. This is not claimed to
  guarantee 20 FPS in every view; the physical improvement still needs measurement.
- DIAG.ELF displays times for render components every 60 frames.
  OOTPS2.ELF omits that detailed profile. The native language test captures
  options and dialogue, checks the header without changing saves,
  and completes house → Kokiri → house with HostFs and the player's memory cards
  disabled. Its logger closes FAT writes before ending the
  test. Usage, credits, and limitations are in PS2_LANGUAGE.md and TESTING_PS2.md.
- The full EE check with language data passes:
  `build/PS2/floor-production-smoke-20261005-222748/report.json`.
  The USB run with the final typography and its screenshots passes in
  `build/PS2/usb-stack-checks/20261005-223443-071/language-game-check.json`.
  The three USB ELFs and the PCSX2 ELF are rebuilt. The package with ESP.DAT
  is copied and verified by SHA256 in D:\OOTPS2 (eleven files); the previous
  version is kept in D:\OOTPS2-backup-20261005-223751. OOT.Z64, the physical
  logs, and the player's memory card retain their hashes. Physical
  confirmation of the language, logo pacing, and FPS improvement remains pending.

## Floor confirmed on real PS2 and integrated into the game — 2026-10-05

- The user confirms that Up in FLOOR v3 eliminates the floor waves
  when moving the camera on their SCPH-90010. BOOT.LOG, DIAG.LOG,
  the tested ELF, the manifest, and their hashes are preserved in
  `build/PS2/hardware-runtime/floor-v3-confirmed-20261005-184050`.
  The log identifies the confirmed v3 ELF, SHA-256
  `49c72707b78e58c3153910d0eabc15379cf44957f4a59ce898da06bb482f2cec`.
- The 24 frames sampled with the adjustment active reduce the maximum UV
  from 2222.5 to 411.188 texels: 58 coordinates outside ±2047 before and
  none afterward. The 105 intervals contain no USB errors, losses, or
  NUL. The maximum of 248 ms per write belongs to diagnostics and does not
  demonstrate an FPS improvement or certify other areas.
- The same calculation is integrated into the second layer of the floor
  materials in the regular game. It is automatic in OOTPS2.ELF, DIAG.ELF,
  DIAG224.ELF, and the PCSX2 build; no D-pad press is required.
  It preserves scale, perspective, both textures, blending, depth, and
  fog. The comparison variant retains its modes.
- The calculation allows UV measurements to be omitted by passing NULL for the result.
  The regular game omits those measurements and FLOOR logging. Tests
  compare its GS points against those with measurements: they are identical.
  Repetition, mirroring, clamp, wide intervals, perspective,
  clipping, vertex reuse, blending, and state boundaries also pass, along with
  the full 120-frame diagnostic without enabling the laboratory:
  `build/PS2/floor-production-smoke-20261005-184649/report.json`.
- House → Kokiri → house passes with emulated native USB, 120 frames per phase,
  HostFs disabled, and the player's memory cards disabled:
  `build/PS2/usb-stack-checks/20261005-184942-622/report.json`.
  The three regular USB ELFs and the PCSX2 ELF are rebuilt, their
  static memory and data correspondence are verified, and the nine package
  files are copied and verified on D:, with a copy of the previous files
  in `D:\OOTPS2-backup-20261005-185331`. The confirmed comparison ELF
  is preserved, and its instructions are updated. Its copy script skips
  identical files and uses a neutral note when preserving previous logs.
  The physical capture, OOT.Z64, and the player's memory card are preserved.
  Final report: `build/PS2/floor-production-final-verification.json`.
  The floor confirmation
  applies to the comparison ELF; areas, audio, performance, and physical
  saving still need continued validation with the regular package.

## Repeated floor coordinates on hardware — 2026-10-05

- The user confirms that FLOOR v2 retains the same waves both with Up
  (read-only depth) and Right (previous pass). That
  hypothesis does not improve the image. BOOT.LOG, DIAG.LOG, the manifest,
  and hashes are preserved in `build/PS2/hardware-runtime/floor-v2-result-20261005-181656`.
  The analysis collects 130 intervals, zero USB errors, losses, or NUL, and a
  maximum of 175 ms per logger write. It does not represent a single-scene
  FPS benchmark.
- Samples of Kokiri's second layer reach about 2058 texel
  units for its 32-pixel images. The GS limits UV to ±2047 before
  wrapping and reduces the STQ mantissa. Centering the 25 sampled triangles
  by full repetitions would reduce their absolute maximum to about 399
  units; this does not yet show that it explains every wave. The size
  of the second layer is inferred from its resources in v2: v3 records it.
- FLOOR v3 restores the original depth and centers only the UVs of the
  second layer. It subtracts the same multiple of the period from the whole triangle
  before forming STQ, preserving perspective and scales. It uses drawing
  copies without modifying shared vertices, respects the expanded mirror
  period, and does not shift clamped axes. It does not apply per-vertex modulo or
  omit textures. Triangles that are too wide remain identified as
  out of range; translation is not claimed to reduce their interval.
- Up selects centered UVs, Left retains the one-layer
  control, and Right restores the previous UVs. The FLOOR_LAB v3 log adds
  dimensions, masks, shifts, and before/after maxima for every
  triangle in a frame sampled every sixty frames.
- Repetition, mirroring, clamp, wide intervals, interpolant equivalence,
  invariant XYZ/Q, immediate/prepared paths, clipping,
  fog, and state boundaries pass. In the bounded test cases that reproduce
  GS truncation, accumulated error drops from 0.0197754 to 0.0012207 texels.
  The full diagnostic presents 120 frames:
  `build/PS2/floor-v3-smoke-20261005-182820/report.json`.
- Startup through emulated native USB passes, with HostFs and the player's memory cards
  disabled: `build/PS2/usb-stack-checks/20261005-182927-438/report.json`.
  FLOOR_LAB v3 and the centered coordinates are checked in DIAG.LOG.
  The optional package is prepared for D: with verified hashes; the regular
  ELFs remain intact. This revision still needs checking on the real PS2.

## Physical layer comparison and read-only second pass — 2026-10-05

- The user compares FLOOR v1 on the SCPH-90010: Up and Right retain
  the same waves; Left eliminates them but loses the second texture.
  The physical log confirms selections in scene 85 and active perspective in
  the sampled materials. Its 160 samples contain no USB errors,
  losses, NUL, or approximate clock. Private evidence is preserved with hashes
  in `build/PS2/hardware-runtime/floor-lab-result-20261005-175141`.
- FLOOR v2 retains both textures and tests a second pass with read-only
  depth. For receivers that compare and write Z, it adds
  two units in Z24 (one in Z16), saturated at the maximum. It preserves GEQUAL,
  UV/XYZ coordinates, blending, and fog, and restores ZMSK when the layer ends.
  Up selects the revision; Right returns to the previous pass.
  This comparison remains limited to the optional ELF: hardware
  confirmation is still needed to determine whether it eliminates waves and preserves occlusion.
- The extended test verifies both layers in the immediate and
  prepared paths, clipped vertices, fog, margin saturation, decals without
  writes, and actual restoration of ZBUF/TEST commands. It passes along with
  the full 120-frame diagnostic:
  `build/PS2/floor-v2-smoke-20261005-180608/report.json`.
- Startup with emulated native USB passes, without HostFs or the player's
  memory cards: `build/PS2/usb-stack-checks/20261005-180608-550/report.json`.
  DIAG.LOG identifies FLOOR_LAB v2 and records the second layer with comparison
  active and writes disabled. The optional v2 package is prepared with
  the new controls; the regular game retains its executables.

## Floor layer and perspective comparison — 2026-10-05

- The user confirms that the waves remain unchanged with the clipping revision.
  That mathematical correction is retained, but it is not considered a solution
  to the defect observed on the SCPH-90010.
- FLOOR.ELF is a separate USB variant: Up returns to normal drawing,
  Left omits the second floor layer, and Right forces its perspective.
  The selection is applied at the start of the next frame. The controls
  remain available in menus, dialogue, and transitions; state callbacks are not replaced.
- The logger records selections with scene/frame and a bounded
  UV/W/STQ/XYZ sample every sixty frames that draw that material.
  It adds no traces to the regular game. It does not confirm a cause on its own.
- The renderer tests pass layer isolation, both vertex
  paths, frame boundaries, and unrelated materials, along with the
  120 frames of the full diagnostic. Private report:
  `build/PS2/floor-lab-smoke-20261005-173936/report.json`.
- Startup with the emulated native USB driver passes, without HostFs or
  the player's memory cards: `build/PS2/usb-stack-checks/20261005-173936-172/report.json`.
  DIAG.LOG retains the FLOOR_LAB marker and floor samples. The physical
  comparison of the modes is still pending; the waves are not claimed to disappear.
- Reproducible scripts are added for building, packaging, and copying the
  optional diagnostic. Copying verifies OOT.Z64 and the hashes, preserves the
  previous physical logs, and retains the regular ELFs. Instructions
  are in `PS2_FLOOR_DIAGNOSTIC.md`.

## Physical test through obtaining the sword and clipping precision — 2026-10-05

- The user confirms that the revised DIAG.ELF starts, loads the save, exits
  the house, and allows play through obtaining the sword on the SCPH-90010.
  BOOT.LOG and DIAG.LOG are preserved with verified hashes in
  `build/PS2/hardware-runtime/scph90010-first-sword-20261005-162143`.
  The 300 samples record no USB errors, losses, or approximate clock.
  There are FPS drops and loading pauses; a logger flush takes up to 184 ms.
- The floor description is refined: it appears to make waves when the camera moves,
  and none are observed with the camera still. Revised DIAG224 also loads Kokiri,
  but the user confirms that the waves remain unchanged. Its 65 samples are
  preserved in `build/PS2/hardware-runtime/scph90010-224-waves-20261005-163739`,
  without USB errors or losses. The 224-line mode is not adopted as a solution.
- An independent loss of precision is found in homogeneous clipping:
  RSPVertex stored interpolated UVs as s16, truncating them again at each
  plane. It now retains float values, including fractions; the original
  10.5 integers are loaded without loss, and reflection generation
  retains its previous quantization. It adds no subdivisions or graphics passes.
- The new test uses perspective interpolation over the original triangle
  as an independent reference. The previous version fails at 584 corners across
  128 cameras, with a maximum error of 1.874319 raw UV units. The extended
  revision passes 256 cameras and 1323 corners, with a maximum error of 0.004191.
  In addition, 20432 clipping cases, 27648 points, and the tests for both
  floor layers, transparency, and perspective pass. Private report:
  `build/PS2/clip-camera-after-20261005-164931/report.json`.
  The subsequent physical check retains the same waves; this change
  corrects the independent calculation, but does not resolve that observed defect.
- The nine startup cases with HostFs pass in
  `build/PS2/usb-host-checks/20261005-165704-330/report.json`, as does the
  house → Kokiri → house transition with the emulated USB driver, without HostFs, in
  `build/PS2/usb-stack-checks/20261005-165817-988/report.json`.
  The player's PCSX2 ELF, memory card, and converted data retain
  their hashes. The three USB ELFs are prepared with the precision correction.

## Leaving the house with USB diagnostics — 2026-10-05

- The user confirms the intro, selector, and save loading on the SCPH-90010;
  DIAG.ELF goes black when leaving Link's house. BOOT.LOG and
  DIAG.LOG are preserved, with verified hashes, in
  `build/PS2/hardware-runtime/scph90010-house-exit-20261005-153103`.
  Logging and audio remain active for over a minute without new
  frames, reads, or USB errors. It was more than just slow loading.
- An automatic test with the native USB driver reproduces the termination of the
  Graph thread after destroying the house. The Play_Init wrapper changed the
  address compared by Graph_GetNextGameState; Play_Update, defined in the
  same object as Play_Init, retained the original address. On exit,
  Graph could not find the next state and terminated. Previous evidence is in
  `build/PS2/usb-stack-checks/20261005-154512-304`.
- Diagnostics now record GameState_Init and preserve all original
  initialization addresses. They add transition,
  update, and destruction milestones, plus a bounded capture of EE threads and
  the graphics wait when no frames are presented for two samples.
  No files are written from game threads or interrupts.
- The corrected test completes 120 updates at each stop of
  house → Kokiri Forest → house, using normal fades, destruction, and loads.
  It passes without HostFs, with memory cards disabled, intact logs, no losses
  or USB errors, and data with the same SHA256. Private report:
  `build/PS2/usb-stack-checks/20261005-155909-872/report.json`.
  This exit still needs confirmation on the physical console.
- The independent test is built with
  `tools/build_ps2_usb_transition_smoke.sh` and run with
  `tools/run_ps2_usb_stack_check.ps1 -Transition -Seconds 55`.
  It uses a synthetic save and is not included in the player's package.
- The nine startup and log checks through HostFs also pass
  in `build/PS2/usb-host-checks/20261005-160634-489/report.json`, and the
  logger test preserves write limits and checks that wait captures
  activate, stay bounded, and reset when a frame returns.
  OOTPS2.ELF retains its previous SHA256; the correction applies to both
  logging ELFs. Converted data are unchanged.

## Diagnostic writes and USB driver test — 2026-10-05

- The second physical attempt also goes black, this time with DIAG.ELF and
  normal 448-line rendering. BOOT.LOG ends at stage 45,
  `REGISTRO: iniciando hilo por vblank`; DIAG.LOG v2 contains only its
  433-byte header. Both are preserved with verified hashes in
  `build/PS2/hardware-runtime/scph90010-diag448-20261005-143535`.
- A native ELF test is added with a FAT32 image and the PCSX2
  USB device, without HostFs. The previous version allows the game to keep
  running in that test, but produces 288 transfer errors, 675 null bytes
  in DIAG.LOG and 61 in BOOT.LOG, as well as overwriting the header.
  Private evidence is in `build/PS2/usb-stack-checks/20261005-144729-837`.
- DIAG.LOG v3 writes 512-byte sectors from a buffer aligned to
  64 bytes; it also keeps the next append aligned. Partial-write
  retries shift data within the buffer, preserving
  its aligned address. The 2 MiB limit counts the padding actually
  written and reserves one sector for LOG_LIMIT.
- Fixing DIAG.LOG alone eliminates transfer errors, but still
  allows BOOT.LOG to corrupt the neighboring header. BOOT.LOG v2 also uses
  one sector per line. BOOTTEST reads up to 8 KiB from the end and removes
  padding spaces to display ten stages, preserving v1 log support.
  The clock, semaphore, thread, and opening/writing/closing of the
  first batch are recorded separately to narrow down any remaining physical hang.
- The full test now passes in
  `build/PS2/usb-stack-checks/20261005-151055-633/report.json`: both logs
  intact, zero USB errors, 25 samples without losses or an approximate clock,
  Hyrule Field loading, and frames after the logo. The data SHA256
  remains intact. The largest flush takes 95 ms in that emulator;
  it does not certify hardware timing or yet confirm physical startup
  of this revision. The analyzer flags invalid headers and null bytes.
- The logger tests pass with aligned buffers, bounded
  transfers, 31-byte partial writes, and the exact file limit,
  as well as ring overflow, errors, and preservation of interrupts.
  The floor comparison with DIAG224 remains deferred until DIAG.ELF is confirmed
  on the SCPH-90010.
- The nine package cases pass in
  `build/PS2/usb-host-checks/20261005-151810-528/report.json`, including old
  startup logs and logs padded by sector. Test-harness reading
  shares files opened by Windows and retries transient locks.
  DIAG.ELF tested with the USB driver: SHA256
  `c6eb78f8578f43d6e808d297366325e2431d5cf93f6fd95c025b5220df68343a`.
- The nine files are copied and verified in `D:\OOTPS2`, with a backup in
  `D:\OOTPS2-backup-20261005-152225`. Both physical logs,
  the regular PCSX2 ELF, and the player's memory card are preserved and checked by SHA256.
  Static segments end at `0x011afc70` for OOTPS2.ELF and
  `0x011c2970` for DIAG.ELF, within the EE's 32 MiB.

## Diagnostics stopped after the logo — 2026-10-05

- The first physical DIAG224 test shows the logo and then goes black.
  BOOT.LOG and DIAG.LOG from the USB are preserved, with verified hashes, in
  `build/PS2/hardware-runtime/scph90010-diag224-20261005-140823`.
  BOOT.LOG reaches the first logo frame; DIAG.LOG contains only its
  346-byte header. There are no data to attribute this to the video mode.
- The logging thread stops using usleep with its return value ignored. A semaphore
  with capacity one receives a signal every 60 calls of the vblank thread,
  which performs no file I/O. The initial state is saved before starting
  the thread, and its entry is also recorded. A semaphore or wait failure
  stops logging. Periodic blocks are still closed every five
  seconds; retraces allow a flush even if Timer 0 does not advance.
- Samples include retraces, clockMs, and clockFallback. When Timer 0
  time is missing, the approximate interval is explicitly indicated; graphics
  times obtained with a stopped clock are not certified.
  The analyzer recognizes a header-only file, thread entry,
  logger errors, and use of that approximate time.
- The logger tests pass with semaphore saturation, a stopped clock,
  wait failure, and semaphore creation failure, as well as previous cases.
  The next physical test must use DIAG.ELF at 448 lines; the floor
  comparison with DIAG224 is deferred until normal diagnostics are confirmed.
- The eight startup cases pass in
  `build/PS2/usb-host-checks/20261005-142346-592/report.json`. Normal diagnostics
  record thread entry, ten samples, and Hyrule Field loading,
  without losses, errors, or use of the approximate clock. Flushes take
  at most 1 ms in PCSX2; this does not measure physical USB. The normal USB ELF,
  the regular PCSX2 ELF, and the player's memory card retain their hashes.
- The updated copy is verified in `D:\OOTPS2`, with a backup in
  `D:\OOTPS2-backup-20261005-142621`, preserving physical BOOT.LOG and DIAG.LOG.
  DIAG.ELF SHA256 `271cfe133d78b07c75f08706972df88265126c66a6162fa6dbb433545d18df31`;
  diagnostic static segments end at `0x011c22b0`.

## USB logging, loading, and floor comparison — 2026-10-05

- The user confirms that the game starts and can be played on their SCPH-90010
  after the cache correction. The physical BOOT.LOG is preserved in
  `build/PS2/hardware-boot/scph90010-first-gameplay`. Loading pauses
  recover, the floor shakes even when still, and audio fails at low FPS.
- DIAG.ELF records states, room loads, slow or failed reads,
  presented FPS, graphics times, synthesis, and the audio queue in DIAG.LOG.
  Game threads only add records in RAM; another thread writes and
  closes blocks every five seconds. It is limited to 2 MiB and records its own
  write cost and lost records. A write error or
  reaching the limit stops logging without stopping the game.
- USB ELFs expand the audio sector cache to 256 KiB and add an
  independent 64 KiB cache for 16 KiB read-ahead. They avoid
  redundant seeks and respect EOF. Buffers are aligned to 64 bytes to
  separate data and metadata during DMA. The test loading 64 KiB in
  8 KiB chunks requires four physical reads, none on repetition, and
  preserves audio sectors. USB audio reserves 60 ms in the IOP,
  versus the previous 20 ms. Its physical improvement remains pending.
- DIAG224.ELF compares a 640x224 render with duplicated lines, retaining
  448-line interlaced NTSC output for the AV-to-HDMI converter.
  OOTPS2.ELF and DIAG.ELF retain 640x448. It reduces vertical detail; whether
  it changes the shaking on hardware still needs checking. The floor is not considered fixed.
- The cache model, logger tests (including failures,
  partial writes, overflow, and interrupts), and the native suite with
  USB ROM/audio policies pass. The latter's log is in
  `build/PS2/usb-cache-smoke/runtime.log`. Eight startup cases pass in
  `build/PS2/usb-host-checks/20261005-005339-046/report.json`, including both
  diagnostics through scene loading. DIAG224 confirms render 224,
  output 448, and MagV=1; normal diagnostics confirm render/output 448 and MagV=0.
  No records are lost; flushes in PCSX2 take at most 1 ms.
- `tools/analyze_ps2_runtime_log.py` is added to summarize sessions without
  playing the whole game. PCSX2 USB times do not represent physical USB
  times. The new log reveals about 30 actual FPS in the N64 logo despite
  its R_UPDATE_RATE=1 setting; its 60 FPS target remains pending.
- The package includes the three game ELFs, BOOTTEST, and their data/documents.
  Copying avoids rewriting identical data, backs up replaced files,
  and preserves the user's additional logs. It does not modify the regular PCSX2
  ELF or the player's memory card.
- Nine files are copied and verified in `D:\OOTPS2`, with a backup in
  `D:\OOTPS2-backup-20261005-005608`. The physical BOOT.LOG retains its hash.
  Static segments end at `0x011afa70` for OOTPS2 and
  `0x011c1eb0` for diagnostics, within the EE's 32 MiB. Game SHA256
  `4d8f97f2ee171d4c6f9699bd637964f7e700521551ccb0f41fa7dedd01baa8d1`.
  Copy report: `build/PS2/usb-copy-report.json`.

## Console startup diagnostics — 2026-10-04

- The second attempt on the console ends at `[PS2 STATE] Init 0x486b60`.
  That symbol corresponds to ConsoleLogo_Init, not GS startup. BOOT.LOG
  from the USB is preserved in `build/PS2/hardware-boot/scph90010-before-cache`.
- Invalidation of the entire data cache is replaced with synchronization
  of the requested range. It preserves CPU writes and neighbors of partial
  lines. The functions are separated into ps2_cache.c; the previous version
  could lose stacks and messages from other threads while loading the logo segment.
  The log does not yet establish this as the sole cause of the hang.
- BOOTTEST adds a test of neighboring buffers/stack, physical alias, data
  produced by the CPU, and interrupts. The CPU/RAM cache model passes and
  explicitly rejects the previous global invalidation. The six startup
  cases also pass, including the logo and first frame, in
  `build/PS2/usb-host-checks/20261004-234222-172/report.json`. Console cache
  behavior awaits the new test.
- Steps of the arena and state constructor, and the logo's allocation,
  DMA, matrices, view, and SRAM are recorded only in the USB variant. The logo
  retains its 60 FPS configuration (R_UPDATE_RATE=1), as requested by the user.
- The corrected package is copied and verified in `D:\OOTPS2`, preserving the
  previous BOOT.LOG. Game SHA256
  `d4c7c98dfa1a41ec0c0c31ab49fb718b0692ae90555d52bc8f4dabf39f0022ab`,
  static segments through `0x0116e730`. Replaced files are backed up
  in `D:\OOTPS2-backup-20261004-234548`. The regular ELF and the player's
  memory card retain their hashes.

- The user confirms `DATOS OK` in BOOTTEST on an SCPH-90010 connected
  through an AV-to-HDMI converter. OOTPS2.ELF goes black. The initial
  hardware data read is confirmed, but game startup and the cause of the hang
  are not. This is recorded in `BUGS_PS2.md`.
- The USB variant displays the stage from main entry and records
  results for modules, reset, memory card, video, controller, audio, heap,
  vblank, and data opening. It preserves earlier stages in RAM and creates
  `BOOT.LOG` alongside OOT.Z64 after validating it. It closes each write
  to preserve the last stage on restart. Onscreen text is
  disabled before gsKit; logging is limited to 96 stages and only to the
  first frame, without recording every game frame.
- BOOTTEST displays the last ten lines of the previous log with
  bounded reading and without modifying it. It also reports if none exists yet.
  The boot and osSyncPrintf objects are independent in the USB
  variant; the regular PCSX2 ELF and the player's memory card are not replaced.
- Six checks pass in PCSX2: valid/incorrect data, game startup
  through the first frame with a complete BOOT.LOG, and both
  startups without a USB device. The BOOTTEST test preserves the hash
  of a log larger than its buffer and correctly displays its end.
  Report: `build/PS2/usb-host-checks/20261004-232202-361/report.json`.
- The new USB game's static segments end at `0x0116e430`,
  within EE RAM. Converted data are unchanged. Testing
  the new diagnostics on the real console remains pending.
- Seven files are copied and verified in `D:\OOTPS2`. The game has
  SHA256 `ca2d317e3b01b6cbd3691745c58168e0183f9bb4c2540d81d3c680c491122040`.
  Replaced files are preserved in
  `D:\OOTPS2-backup-20261004-232349`; report `build/PS2/usb-copy-report.json`.

## Room traversal and USB preparation — 2026-10-04

- The user considers the Deku Tree resolved and asks to expand testing and
  then prepare a real PS2 test through USB, Free McBoot, and
  wLaunchELF. They authorize copying the finished package to drive D:.
- An independent test ELF is added: it selects all 388 rooms from
  the original converted lists, with one age per scene. It loads and
  runs at least 45 frames, checks actors and objects, exercises
  equipment/rupee receipt and switch/chest flags, and immediately restores
  the test state. Both memory cards and the controller are
  disabled. This does not replace progression, puzzles, combat, or actual doors.
- It captures the GS in 320x240 PPM, verifies length/hash, and generates PNGs,
  contact sheets, and a searchable gallery. Dark images are flagged
  for camera review; visual fidelity is not automatically certified.
  Partial reports retain FAIL and can be combined with provenance.
  The validator rejects a missing result, an exception after PASS,
  a missing event, and an incorrect room.
- A synthetic anchor activates a portal in the Fire Temple. Requests
  for other rooms are blocked only in the test ELF. `DemoKankyo` deliberately
  hides the background of `hiral_demo`; that condition is recognized with
  the loaded buffer, without modifying the scene. The isolated test passes.
- All 388 rooms are attempted: 387 complete loading, events, and capture across
  101 scenes. `men_room_2`, case 201, produces an exception in the moving
  rocks. It is reproduced in a cold run; the second path is in
  a block not yet described by the converter. It remains pending and is noted
  in `BUGS_PS2.md`. The combined gallery is in
  `build/PS2/room-gallery-20261004/index.html`, with FAIL status.
- Current startup resets the IOP, losing the launcher's USB
  drivers. The new USB variant includes USBD and USBHDFSD, loads them
  after the reset, and looks for data alongside the ELF. It checks the size
  and twelve blocks with CRC32; it displays an error and stops startup if
  they are missing or do not match. It does not accept an original ROM as port data.
- BOOTTEST is added to check data and query the memory card in port 1
  without writing to or formatting it. The package includes ELF, converted data,
  SHA256, a report of the ELF's static addresses, and instructions. The game's
  static segments end at 0x01168570, within the EE's 32 MiB;
  this does not certify all dynamic allocations on hardware.
- Local checks pass for valid data, incorrect size,
  an altered block, game startup through the verified path, and actual loading
  of both modules in PCSX2's IOP with a bounded error for a missing USB
  file. Loading is checked in diagnostics and in the game with its
  sound, memory card, and controller modules already prepared. First report:
  `build/PS2/usb-host-checks/20261004-223424-326/report.json`.
  The final rerun passes all six cases, including the game's USB ELF:
  `build/PS2/usb-host-checks/20261004-225212-578/report.json`.
  No test validates a physical USB device or a real PS2.
- The private package is in `build/PS2/usb-ready/OOTPS2`. The game ELF
  has SHA256 `cd03ea959336f5d9ac9951823ed1afd35739302d7c450f4c4b27758e9d3c438b`;
  the data retain `034fcb6d0a95154d8f96b5b8e11711d5781e2e6950b3cc99b28c3de9a641a78f`.
  The regular PCSX2 ELF and the player's save are preserved. Console
  testing, further performance work, and publication on GitHub remain
  pending; the package with game resources stays outside Git.
- Seven files are copied to `D:\OOTPS2` on the LUCBYN FAT32 USB, and their
  SHA256 hashes are compared with the package. Report `build/PS2/usb-copy-report.json`.

## Icon position, background, and animation — 2026-10-04

- The user confirms that the spider above the drop is fixed. In the BIOS
  browser, they confirm the title and Triforce, but the model is too
  low. Y=-3 is tried first; it is too high in the save list.
  Y=-1.5 is set as an intermediate position for the list and the
  copy/delete panel. The background changes to a golden gradient.
- Poses around the vertical axis and blending curves summing to
  one are added. The 16-pose variant (75024 bytes) passes decoding and complete
  reading with libmc, but the BIOS displays it as corrupted data. It is reduced
  to eight poses (56208 bytes), which the user confirms are correctly visible
  and rotating. The cause of the larger variant's incompatibility is not
  isolated; it is not attributed to a specific size or pose limit.
- After that confirmation, the speed is reduced from 1.0 to 0.35, along with
  the light and ambient intensity, to better distinguish the relief and bevels.
  The local geometry of 96 triangles and the central hole are preserved.
- An EE installer is added that only updates `icon.sys` and `triforce.icn`
  on an already formatted memory card with an existing save. It does not write SRAM
  or format: it compares both complete files, `save.dat` and `save.bak`,
  before and after installation. It is first tested on a copy of
  the regular memory card with an isolated configuration. PASS confirms the
  browser files and that both saves remain identical.
- The independent reader verifies the eight poses, curves, blending between
  keys, cycle continuity, constant height, title, lighting,
  texture, and bounds. The report records the speed and retains
  `bios_display_validated=false`: that tool does not observe the BIOS.
- Tests pass with the final adjustment: reading every byte, two
  alternating writes, recovery from a damaged copy, repair of a truncated
  model and missing metadata, SRAM preservation on error, and reload
  after closing PCSX2. New logs:
  `build/PS2/pcsx2-icon-slow-card-write.log` and
  `build/PS2/pcsx2-icon-slow-card-cold.log`.
- Installation on the regular memory card passes and confirms both complete
  save files are identical, including their headers. Log:
  `build/PS2/pcsx2-icon-slow-install-player.log`. The backups
  `oot-port-before-icon-rotation.ps2` and `oot_ps2-before-icon-rotation.elf` are preserved.
  The normal ELF is updated after completing the link with 724 objects;
  SHA-256 `13AC8E1E6E51AEAEB12C05FA69558C79008B8B526996D140BC04DC269D1BCEAA`.
  The user also confirms that the final speed and lighting are good.
  Testing on physical PS2 remains pending. The old diagnostic directory
  `OOT_PS2_TEST` is detected separately and is not modified on the regular memory card.

## Memory card icon and automatic tests — 2026-10-04

- The user asks for the game's name and a 3D Triforce for the memory card
  browser. The build generates `icon.sys` and `triforce.icn` from
  `gTriforceVtx`/`gTriforceDL` in the local extraction: 96 triangles, bevels,
  volume, and a central hole, with a golden material. The title uses Shift-JIS on
  two lines: «Legend of Zelda» / «Ocarina of Time». The model occupies 39736
  bytes and the metadata 964; the normal ELF links 724 objects. The data
  generated and game vertices remain in `build/`, outside Git.
- The backend installs the model before the metadata, verifies a complete
  read, and skips rewriting if the bytes already match. It only operates on
  those two files; it preserves the names, version, checksums, and revisions
  of alternating saves. An icon error does not disable SRAM.
- The libmc test uses a new isolated memory card. Two writes,
  reloading the latest revision, recovery from a damaged copy, reading
  both browser files, and persistence after closing/reopening PCSX2 pass.
  Repair of a truncated model and missing metadata,
  save preservation after an icon failure, and installation without changes also pass.
  The independent reader checks geometry, normals, title, references,
  animation, texture, and bounds. The preview is a render of the model;
  its scale and lighting still need confirmation in the BIOS and on physical PS2.
- `tools/run_ps2_regression.ps1` combines graphics/audio/SRAM, memory card, and
  Skulltula tests, with a JSON report, hashes, and new logs for each run.
  It rejects old PASS results, detects failures/exceptions/timeouts, and closes
  only identified test processes. Each run creates its own memory
  card; the play-session configuration is excluded. The
  `-IncludeSceneSweep` parameter adds the 191 entry cases for all 101 scenes.
  Full progression and real PS2 are not declared verified.
- Regarding the spider in room 10: the play-session log retains floor
  Y=720, platform Y=820, original cylinders, and Link's normal mass.
  The observed path passes outside the contact cylinders. Seven
  original positions pass descent, landing, pushing, and return to the ceiling.
  The additional test reproduces alternating descent/return at
  platform height and confirms frontal contact with a response to the
  hit (`frontalHit=1`, `pushAndResponse=1`). That diagnostic was not considered
  a correction: the user confirms the problem before and after crossing.
  A PS2-only AI adjustment is added: when Link is standing on a higher
  surface, it anticipates the next descending step, cancels
  downward velocity, and completes the return to the ceiling before
  descending again. It does not change the floor, damage, or cylinders, and adds no barriers.
  Tests compare 9990 pose channels against the original extraction,
  heights 799/820, frontal contact, and return to the ceiling. Visual
  verification in the play session remains pending.
  The subsequent EE test passes: minimum Y=831.094 and Y=856.834, both
  above the surface plus 32 units, with four returns to the ceiling
  in each case; frontal contact still triggers pushing and a response.
  The seven positions at floor level still pass. Log:
  `build/PS2/pcsx2-skulltula-ledge-final.log`.
  Full command run with a build:
  `build/PS2/regression/20261004-184640/report.json`.
  Play-session log: `build/PS2/pcsx2-skulltula-live-final.log`.
- The normal game with the icon starts in PCSX2: it installs and verifies both
  files in `OOT_PS2`, loads the 32768 bytes of revision 41, and verifies
  a subsequent write as revision 42. The previous memory card is preserved
  in `oot-port-before-memorycard-icon.ps2`. Its browser presentation
  is not yet confirmed.
- The normal version with the retreat adjustment starts again and loads
  revision 42 without rewriting the icon files, which already match. Another
  memory card copy is preserved in `oot-port-before-skulltula-ledge-fix.ps2`.
  SHA-256 of the opened ELF:
  `1CE8CD742CB0D52DBF390BA664A888B4EC088ACF7530848863DC4080CE75E03B`.

## Fades, Gohma eggs, emerald, and owl — 2026-10-04

- The packed `RRGGBBAA` transition colors were assigned
  directly to a union of RGBA bytes. On the little-endian EE, the
  original white `160,160,160,255` became `255,160,160,160`;
  updating alpha left a pink fade. A helper explicitly converts
  between packed words and RGBA fields, without changing
  the structure layout. It is applied to Fade, Wipe, Circle, and Triforce,
  preserving their timing and original hue. The EE test uses the actual
  setters: 48 cases and 7008 Fade draw lists, with colors, durations,
  speeds, and both fade directions; it passes without differences.
- Gohma's eggs call `func_80094E78` to scroll a texture
  through a zero-size `SETTILESIZE`. Uploading interpreted this as
  an absence of texels. When a repeat mask exists, the period
  of the loaded image is preserved. Diagnostics reproduce the egg's
  commands and 12-bit wrap: 124 of 128 cases fail before; afterward,
  all pass, including 32768 RGBA texels. Logs:
  `build/PS2/pcsx2-egg-scroll-before.log` and
  `build/PS2/pcsx2-visual-material-fixes-pass.log`.
- The Kokiri Emerald combines an I4 intensity texture of 64x64 with I8 noise of
  32x32. Its alpha interpolates both samples; the generic path used the same
  texel for both. That combination is added with independent scrolling,
  shifts, green tint, LOD factor, and alpha. The complete noise image is
  preserved even though its scrolling window is 16x16. The cache checks both
  sources and origins, ignoring padding. 114688 channels pass against
  an independent evaluator, as do changes to tints, scrolling, and data.
- The owl's neck and body repeat a 16x16 image in a 640x192
  clamp window. The previous buffer could not expand that window and
  uploaded only 16x16 while retaining a 640x192 clamp: the GS could sample
  other textures in VRAM. Both temporary buffers now support that
  expansion; the total static RAM increase is 512 KiB. All
  122880 texels of the repeated pattern, clamp bounds, and reuse pass.
- Existing tests for audio, shadows, transparency,
  floor, inventory, title aura, and Skulltula also pass, with the 120 frames of the
  full diagnostic. These tests verify fields, commands, and texels;
  visual verification of all four cases during gameplay remains pending.
  The compiled Deku Tree cutscenes retain their 3448 verified bytes of
  mixed fields, in `build/PS2/check-visual-fixes-cutscenes.log`.
  After the user confirms saving, the memory card is preserved in
  `oot-port-before-visual-fixes.ps2` and the previous ELF in
  `oot_ps2-full-assets-native-actors-confirmed.elf`. The new visible version
  loads the save and Hyrule Field without exceptions. The changes do not
  modify game data or progression.

## Full map assets and actor reset — 2026-10-04

- The converter's `full` profile includes every data segment
  described by the local extraction: 101 scenes, 388 rooms, and 378 object
  banks, in 1013 segments. It adds 780 to the previous profile, preserving
  their VROM addresses. It excludes N64 code and its overlays: the game
  uses its functions compiled for the EE.
- `check_ps2_full_assets.py` compares 49560 resources of 67 types against the
  native structures and original bytes. It checks object dependencies,
  rooms, collisions, paths, alternate headers, skeletons, animations,
  JPEG backgrounds, and pointers. The 233 previous segments and the entire image
  outside the new ranges remain identical, including audio.
  The control with the previous image, a complete manifest, and valid checksum
  fails on an unconverted scene command. Logs:
  `build/PS2/check-full-assets.log` and `build/PS2/check-full-before-control.log`.
- Diagnostics link an independent ELF that calls `Play_Init`,
  `GameState_Update`, and `Play_Destroy`. It uses test saves and actual
  entrances; it disables the controller and both memory cards. It selects the base
  entrance and time appropriate to each scene variant and age; it does not
  treat nighttime or ruined-market entrances as base entrances.
- The first pass discovers camera TLB errors on the second load
  of Barinade. Its static camera variable still pointed to a camera
  from the previous state. The EE's compiled overlays remained resident
  and did not reset their data, unlike loading an N64 overlay.
- The linker groups `.data` and `.bss` of all 426 resident actor overlays.
  Before startup, 391600 bytes of initialized data are preserved; loading
  an overlay with no clients restores them and clears its BSS. It maintains
  shared state while actors remain alive and respects persistent
  overlays until the scene changes. Internal actors are not reset.
  Client decrement when destroying resident actors is also corrected.
- The EE test modifies the actual data and BSS of all 426 banks before
  creating actors. It verifies restoration of 390115 data bytes and 141375
  BSS bytes, preservation while clients exist, and preservation of
  persistent overlays until the scene changes. It passes without differences.
- Repeating Barinade's room, as child and adult, passes 60 frames
  per entrance without the camera exceptions. Logs:
  `build/PS2/pcsx2-scenes-before-actor-reset.log` and
  `build/PS2/pcsx2-barinade-reset-pass.log`.
- The subsequent full pass clears 191 cases covering all 101 scenes,
  using child/adult and day/night according to entrance variants. Each
  case completes 60 actual-state updates without EE exceptions;
  the expected scene and loaded room are checked. No texture failures
  or invalid vertices appear in the log's graphics samples.
  Logs: `build/PS2/pcsx2-all-scenes-native-pass.log`,
  `build/PS2/check-scene-sweep-results.log`, and `build/PS2/scene-sweep-results.json`.
  The normal game links 722 objects; diagnostics add their wrappers
  only to their own ELF. The four tree cutscenes still match
  their native fields in the final ELF:
  `build/PS2/check-full-game-actor-cutscenes.log`.
  Full progression and verification on a real PS2 remain pending.

## Skulltula token glow mask — 2026-10-04

- The token uses scrolling I8 noise of 32×32 as TEXEL0 and an
  I8 silhouette of 32×64 as TEXEL1. The generic path used the first source
  for both, turning the noise into a visible rectangle. The new path
  recognizes that combiner and its tiles, combines both sources, and preserves the
  silhouette's coordinates and clamp. The cache includes offsets,
  tints, LOD factor, and bytes from both textures.
- The EE test compares 40960 channels against an independent evaluator,
  five offsets and constants, 3785 transparent texels, bounds,
  sources with padding, immediate/prepared coordinates, and invalidation when
  the mask changes. Floor, inventory, title aura,
  audio, and the 120 frames of the full diagnostic also pass. Log:
  `build/PS2/pcsx2-token-flame-pass.log`.
- The user confirms that the token now appears cut out. The visible version
  includes the full data; the 233-segment image,
  the confirmed ELF for the tree's interior, and a memory card copy
  taken after saving, before this restart, are preserved.

## Deku Tree interior and vertex preparation — 2026-10-04

- After confirming the outside dialogue, the log shows entrance
  `0000`, scene 0. Its room list was read in N64 order and produced
  a load from VROM zero and TLB failures. The `play` profile now converts
  `ydan`, its twelve rooms, `ydan_boss`, and its two rooms. It also adds six
  object dependencies discovered in the original lists. The
  image grows from 211 to 233 segments; it preserves every VROM range.
- `check_ps2_deku_rooms.py` compares 852 new resources against fields
  in the C structures: commands, room and actor lists, collisions,
  geometry, skeletons, and animations. It checks bounds and dependencies;
  textures retain their bytes. The entire image outside the 22 new
  segments remains identical, including the 211 previous segments and audio
  banks. The control with previous data fails on vertices in the
  wrong order. The Kokiri room check also passes again.
- Graphics point preparation calculated GS dimensions,
  tile shifts and offsets per vertex, and queried the material's alpha again.
  It now prepares those values once after uploading the texture, with an
  independent context for each floor layer. It is invalidated by every
  state command. It preserves the order of floating-point operations,
  perspective, depth, fog, and pass order.
- On the EE, 27648 points match the immediate path in all 48 bytes:
  sixteen shifts, positive/negative/zero scales, fallback sizes,
  perspective around its threshold, and dynamic alpha. For 32768 points,
  preparation goes from 12089934 to 6663507 cycles. This measures that operation,
  not the percentage improvement in game FPS.
- The full diagnostic passes, including floor points and packets,
  clipping, decals, fades, audio, inventory, aura, and 120 frames. The
  ELF retains the four corrected cutscenes. Logs:
  `build/PS2/check-deku-rooms.log`, `build/PS2/pcsx2-point-setup-pass.log`,
  and `build/PS2/check-deku-and-point-linked.log`. The user confirms that
  the main room loads, they can move, and FPS are more stable.
  The log confirms scene 0, rooms 0 and 1, and over 3300 frames without
  the previous image's exceptions. The previous
  image is preserved in `oot-before-deku.z64`/`.json`, and the confirmed outside-dialogue
  ELF in `oot_ps2-deku-cinematics-confirmed.elf`. Full dungeon
  progression and stable 20 FPS remain pending. At the user's request,
  further optimization is deferred to prioritize
  conversion of the rest of the game.

## Compiled Deku Tree cutscenes — 2026-10-04

- The user reports a hang when entering the area outside the tree.
  The log stops producing frames, but audio keeps working.
  That Kokiri room was already converted. The interior conversion was
  added later, as detailed in the previous section.
- The tree actor's cutscenes are compiled from C. Command macros
  packed bytes and halfwords as on N64; on the EE,
  the parser read times, positions, and end markers from different fields. In
  particular, the `CS_CAM_STOP` byte ended up at the end of the word, and the camera
  search could run beyond the list.
- The PS2 macros place those members at their native offsets. Full
  words, floats, and pointers retain their values; the N64 path retains
  its order. Music start and stop use the two actual bytes of their
  header, instead of a halfword that reverses the ID on the EE.
- `check_ps2_compiled_cutscenes.py` compiles the four actual sequences and
  a sample with camera, actor, light, rumble, text, time, transition,
  destination, and music. Their 3448 bytes match the ROM converter's independent
  schema; thirteen camera lists terminate within their
  bounds. It also verifies the four arrays in the final ELF. The control
  with previous headers fails on field order.
- The full EE graphics and audio diagnostic passes, and 721
  objects link. Logs: `build/PS2/check-compiled-deku-linked.log` and
  `build/PS2/pcsx2-deku-native-commands-pass.log`. The user confirms that
  they can enter the outside area and dialogue continues. That session is
  preserved in `build/PS2/pcsx2-deku-cinematics-game-confirmed.log` and its ELF
  in `build/PS2-ntsc12/oot_ps2-deku-cinematics-confirmed.elf`.
  The previous ELF is preserved as `oot_ps2-ee-memcpy-confirmed.elf`, along with a
  copy of the memory card from before the restart. The resource image is not modified
  in that first correction.
- The Nintendo 64 logo retains the rate of its original code:
  60 FPS, versus 30 for the selector and 20 for gameplay. The user now confirms
  20 FPS while standing still in almost every view; drops remain while walking.

## Packet copying on the EE — 2026-10-04

- The executable linked the SDK copy routine, which traverses 32-bit words.
  gsKit uses it to copy each draw's packed points into
  the GIF queue. PS2 linking now redirects those calls through
  `--wrap=memcpy` to an implementation that copies full blocks with
  LQ/SQ if both ends are aligned to sixteen bytes, or LD/SD if
  aligned to eight. Tails and other alignments use the SDK.
- The return value, zero-length case, and read/write bounds are
  preserved. The original SDK remains linked for the fallback
  path and control comparison. Points, drawing order,
  and GS states retain their original data.
- 19200 copies pass with independent alignments 0–15, lengths
  0–65, and packet sizes up to 2880 bytes, compared with a bytewise
  copy and with guard regions checked. Audio,
  capture, transparency, floor, aura, and 120 frames also pass.
- Across 8192 repetitions, 144 aligned bytes go from 1678908 to 815892
  cycles; with the source offset by eight bytes, from 1682994 to 1055593.
  The 2880-byte block goes from 27395480 to 6707563. These are copy
  measurements, not FPS percentages. Log: `build/PS2/pcsx2-ee-memcpy-pass.log`.
- The game links 721 objects. The user confirms 18–20 FPS while standing still
  and approximately 16–18 while walking, with some drops depending on the view;
  image and audio are correct. Stabilizing 20 FPS remains pending.
  The previous version, confirmed at 16–18 FPS, is preserved in
  `build/PS2-ntsc12/oot_ps2-glow-dependency-confirmed.elf`.

## Fairy glow dependencies — 2026-10-04

- The outside profile shows up to twenty conversions and uploads of
  I8 textures of 32×64 per frame, with the same RGB colors and different
  ENV_ALPHA. These are the two halves of the fairy glow. In the current generic
  evaluator, TEXEL1 uses TEXEL0, so their difference in that
  material is exactly zero, and the ENV_ALPHA factor does not change its pixels.
- Dependency analysis excludes terms of a product with a
  zero difference, both from the texture key and from per-channel
  tables. The explicit two-source path for paths retains its
  dependencies; the title aura retains its full key. Color and alpha
  equations and vertex behavior are preserved.
- The EE test compares all 256 values of 26 materials, nine changes
  of constants, and sixteen partial masks (3744 combinations).
  An actual glow-material texture retains its 8192 bytes and its
  VRAM address through 255 alpha changes, without another upload.
  Changing the tint or a source byte triggers a new upload.
- The full diagnostic passes, including the descriptor fence, audio,
  floor, transparency, aura, and 120 frames. Log:
  `build/PS2/pcsx2-glow-dependency-pass.log`. The control with the previous
  combiner fails the reuse check on the first alpha change:
  `build/PS2/pcsx2-glow-dependency-before-control.log`.
- The game links 720 objects. At the same outside spot previously running at 14–16 FPS,
  the user confirms 16–18 FPS, with correct Navi, floor, shadows, and audio.
  The log retains only 3–4 uploads per frame in that view, and
  the twenty glow uploads disappear. The stable 20 FPS
  target remains pending. The previous version is preserved
  in `build/PS2-ntsc12/oot_ps2-ground-reuse-confirmed.elf`.

## Reuse of both floor layers — 2026-10-04

- Floor materials retain a base pass and a blending pass for
  each triangle, in the same order. Both layers now reuse their
  shared vertices during a sequence without state changes.
  The first layer's original colors and depth are preserved
  to prepare the second. The base clamp is applied after each overlaid
  layer; passes are not grouped, and their order is unchanged.
- Each state command invalidates both point sets. Temporary
  clipping vertices and depths calculated per
  triangle for decals retain their independent preparation.
- The EE test compares every point and the packet count against
  the immediate path in eight cases: zero/partial/opaque alpha, fog,
  perspective, different dimensions and shifts, decals, clipping,
  repeated indices, culling, and boundaries between state commands.
  It passes without differences, along with audio, capture, aura, and 120 frames.
  The isolated test drops from 1893328 to 1261975 cycles; log
  `build/PS2/pcsx2-ground-reuse-pass.log`.
- The user confirms correct floor, shadows, and audio, but the demanding
  view remains at 14–16 FPS. The stable 20 FPS target remains
  pending; the previous executable is preserved as
  `build/PS2-ntsc12/oot_ps2-material-mmi-confirmed.elf`.

## Material tables and texture checking — 2026-10-04

- Optimization of the outside area resumes before expanding the Deku
  Tree area. The user points out a view at 12–14 FPS. After the first
  optimization, they confirm 14–16 FPS and correct colors and audio. The next
  version preserves shadows, transparency, and audio, but the user
  confirms it remains at 14–16 FPS while standing still. The stable 20 FPS
  target is not yet considered achieved.
- Combiner tables expand the first-cycle product once
  and traverse its 256 values through finite differences. They preserve
  signed integer division and original saturation. Two-cycle
  materials preserve intermediate RGBA8, including COMBINED_ALPHA;
  alpha is converted to the GS scale only after the final cycle ends.
- XXH32 processes the four chains of aligned sources through
  EE MMI instructions. PMULTUW calculates two products per instruction,
  and PPACW combines their 32-bit portions; its semantics were checked against
  [PCSX2](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/MMI.cpp).
  It uses LQ loads only for complete blocks aligned to 16 bytes. Sources
  without that alignment and tails retain scalar reads.
  Every byte is still checked at each texture binding.
- The EE tests pass 25 materials × 9 constant changes × 16
  partial masks, compared with the scalar evaluator for every
  texel. They cover negative and quadratic products, previous-cycle alpha,
  saturation, packed RGBA16, and cache donor replacement.
  Independent XXH32 vectors, alignments 0–15,
  mutation of every byte, audio, capture, aura, and 120 frames also pass.
- Control comparison with the same tests and the previous combiner:
  complete tables go from 23961838 to 8668335 cycles; with channel
  reuse, from 9028600 to 4946683. The actual two-cycle material goes
  from 39557399 to 28547484 cycles, with the same checksum 193581.
  Checking aligned 2/4 KiB sources drops by approximately
  13–14 %. These are isolated measurements; they do not represent that FPS percentage.
  Logs: `build/PS2/pcsx2-material-mmi-pass.log` and
  `build/PS2/pcsx2-material-mmi-before-control.log`. The first confirmed
  session is in `build/PS2/pcsx2-material-polynomial-game-measured.log`.
- The game links 720 objects. Previous executables are saved in
  `build/PS2-ntsc12/oot_ps2-before-material-polynomial.elf` and
  `build/PS2-ntsc12/oot_ps2-material-polynomial.elf`.

## Opening pause, transparency, and title aura — 2026-10-04

- The user points out corruption and a delay when opening pause, missing
  inventory transparency, and an unmoving aura around the title.
  They later confirm that both inventory issues improved and that the aura
  now moves like a flame.
- The N64 background copy operated on `curFrameBuffer`, which the native
  renderer does not fill: drawing lives in GS VRAM. Its coverage passes were
  drawn on the screen, and the CPU filter read a buffer without a valid image.
  `Play_Draw` now emits a native capture after the scene ends, before the
  HUD. It waits for completion and uses that background for pause; on PS2,
  the coverage passes and N64 reconstruction filter are omitted.
- The capture downloads rows from the active framebuffer and publishes 320×240
  big-endian RGBA5551. It uses only 40 KiB of temporary memory, without allocating
  more VRAM. The VIF1 transfer follows the
  [ps2dev SDK](https://github.com/ps2dev/ps2sdk/blob/master/ee/debug/src/screenshot.c),
  adapted to CT16S format and the framebuffer width. It preserves gsKit's
  FINISH fence so that the next queue can continue.
- The original IA8 panels already contain partial alpha and use the XLU
  blender. For example, 2228 of the 2560 texels in `gPauseSelectItem11Tex`
  have alpha 11/15. With a valid capture restored, those parts allow
  the scenery to show through. Their alpha and original behavior are preserved.
- The title uses an I4 mask of 64×64 and I8 noise of 32×32, with shifts and
  a half-texel offset. Previously, TEXEL1 was evaluated as TEXEL0.
  That material now combines both data sources, its LOD fraction, colors, and alpha
  from both cycles. A reused table avoids resolving the combiner
  for every pixel. The cache includes the offset and content of both
  sources; the path is limited to this material and its dimensions.
- The EE tests check 230400 downloaded pixels, byte order,
  bounds, invalid commands, a second capture without changing the image, and
  queue continuity. The GS also passes drawing IA8 panels with
  zero, partial, and opaque alpha over a known image.
  The aura passes 81920 channel checks with fractional scrolling,
  different tints, alpha, reuse, and mutation of the second texture.
  Audio, portrait, and the diagnostic's 120 frames also pass.
  Logs: `build/PS2/pcsx2-pause-flame-pass.log` and
  `build/PS2/pcsx2-pause-flame-final-pass.log`. The game session shows
  several correct captures when reopening inventory; it is preserved in
  `build/PS2/pcsx2-pause-flame-game-confirmed.log`.
- The game links 720 objects.
  `build/PS2-ntsc12/oot_ps2-before-pause-flame.elf` is preserved as the previous copy.
  The stable 20 FPS target and full scene validation
  remain pending; these corrections do not establish them as achieved.

## Received models and equipment portrait — 2026-10-04

- The user confirms the equipment portrait, although it retains its
  original 64×112 resolution. Items remained absent: converting
  their resources was necessary, but did not fix the event that activates them.
- Diagnostics record completed rupee loading (`queue=1`),
  while `draw=0` remains at the end of the 22-frame animation.
  `Animation_OnFrameImpl` reconstructed the current frame through
  `(current - speed) + speed`. With adjusted speed 2/3 and rate 1.5,
  EE rounding loses precision exactly at the last frame, 21, and
  skips the event. The interval endpoints are compared with the actual
  current frame, preserving the original wrap and frame 0 convention.
  N64 code remains intact. Reference for default rounding:
  [PCSX2 configuration](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Pcsx2Config.cpp).
- The test of the actual predicate on EE fails at exactly that case with
  the previous code (`pcsx2-animation-before-item-fix.log`) and passes
  all 102401 cases after correction: positive/negative speeds, fractions,
  different rates, and wraps. Audio, graphics, portrait, and
  120 frames also pass in `build/PS2/pcsx2-item-animation-pass.log`.
- In-game logging records `draw=110`, completed loading, and a reference between
  the hands when receiving the rupee. The user confirms: «Yes, it appears».
  `pcsx2-item-frame-21-not-shown.log` and
  `pcsx2-item-animation-confirmed.log` are preserved; repeated traces are removed,
  leaving only two records per receipt, for loading and appearance.
  Next requested revision: corruption when opening pause, inventory
  transparency, and an animated title aura.
- The user confirms that Link's textures and access to the new room
  work. They report missing models when receiving the sword and other items,
  and a corrupted image in place of Link's equipment portrait.
  They later clarify that the recovery heart works correctly.
- Received items are loaded from Player, without necessarily appearing
  in room object lists. The play profile now converts all
  77 `object_gi_*` segments, using their explicit extraction ranges.
  `check_ps2_received_items.py` verifies 214 graphics lists, 143 vertex
  groups, and 57 textures; it preserves the 145 previous segments byte for byte.
  The test with previous data fails because `object_gi_key` is missing.
  Logs: `received-items-check.log`, `received-items-before-check.log`,
  and `kokiri-after-items-check.log`, within `build/PS2`.
- The renderer only followed the color pointer, always drawing into the
  displayed framebuffer. The 64×112 prerender therefore never reached the
  equipment texture. A separate CT32 target with Z24 is added,
  allocated before the cache, with coordinates at scale 1 during that drawing.
  Two native list commands delimit the portrait and afterward restore
  the main framebuffer and depth. Normal 640×448 output
  and texture interpolation are unchanged.
- Reading through VIF1 uses the SDK's `ps2_screenshot`. RGBA5551 is published in
  the byte order consumed by the decoder, with a transparent background.
  Only this portrait's N64 coverage/VI process is replaced;
  the CPU filter is not applied to a buffer the GS had not written.
  The portrait is reused while the segment, age, and equipment remain unchanged.
- The initial check detected that the native descriptor on the EE stack
  was interpreted as a segmented address; it now uses its physical address.
  Another check detected loss of FINISH: the SDK acknowledges and clears
  the event, while gsKit waits for it before submitting the next queue.
  An independent FINISH is sent after the read to preserve
  that sequence. References: [SDK read](https://github.com/ps2dev/ps2sdk/blob/master/ee/debug/src/screenshot.c)
  and [gsKit queue submission](https://github.com/ps2dev/gsKit/blob/master/ee/gs/src/gsCore.c).
- The GS test draws two different images into the same target and
  compares their 14336 pixels, alpha, RAM bounds, viewport, depth
  restoration, and texture update. It passes with zero differences,
  along with the rest of graphics/audio and 120 subsequent frames; log
  `build/PS2/pcsx2-portrait-items-pass.log`. The attempt without FINISH is preserved
  in `pcsx2-portrait-before-fence-fix.log`: it completes the first read and
  hangs when trying to submit the second drawing.
- The game links 719 objects. ELF and data are installed with SHA256 identical
  to the verified files. The user saves before restarting.
  The new version is opened; subsequent confirmations of equipment
  and a received item are described above. The backups `oot_ps2-before-item-portrait.elf`
  and `oot-before-item-models.z64/json` are kept in `build/PS2-ntsc12`.
  The actual session already completes three portrait reads, remains responsive,
  and maintains PCM without delays or drops. The cache has 1760 KiB
  after allocating both 32 KiB targets; it retains all 512 entries.

## Link's texture windows and remaining Kokiri rooms — 2026-10-03

- The user perceives another performance improvement, with drops in
  certain camera views, and reports a pink mark behind Link's ear and
  a dark green pattern on his torso. They provide no new FPS figure.
  They also encounter another hang when moving to another part of the forest.
- The original lists load the hair as RGBA16 of 16×16, but set
  a 32×16 window with a horizontal mask of 16 texels. The tunic uses
  I8 of 16×32 with a 32×32 window, mirroring, and clamp. The port confused
  the window with the image and read bytes of the next resource as texels;
  it also recognized mirroring only when the clamp bit was off.
- The period selectable by the mask is decoded, preserving
  the LOADTILE stride or LOADBLOCK TMEM row. Repetition or mirroring is then
  materialized within the bounded GS window. Coordinates
  are interpolated as before, and the cache includes the dimensions
  of the submitted image, along with the texels and palettes actually used.
  The clamp→mask/mirror order was checked against the reference code from
  [angrylion-rdp-plus](https://github.com/ata4/angrylion-rdp-plus/blob/master/src/core/n64video/rdp/tex.c).
- The new test uses the hair and tunic formats, masks, and rows,
  with 24- and 32-texel windows, repetition, mirroring, and padding bytes that
  reveal reads of the next resource. It also checks GS
  bounds, the cache key, and RAM changes inside and outside the used period.
  Its 5376 texels, the rest of graphics and audio, and 120 frames pass in
  `build/PS2/pcsx2-link-tile-window-pass.log`. The first check had
  14 alpha differences at I8 value 255: the new reference expected
  128, but the existing decoder retains 127. The reference was adjusted
  to verify the row correction without altering alpha quantization.
- Both hang logs end in Scene_CommandObjectList and
  Actor_Spawn without loading a scene other than Kokiri. The previous image
  converted only `spot04_room_0`: `spot04_room_1` and `spot04_room_2` were missing.
  The play profile now includes all three rooms and their dependencies. One
  record per room load is added to identify future transitions.
- `check_ps2_kokiri_rooms.py` validates commands, objects, actors, and textures
  against the original ROM: 33 scene resources and 68/46/22 room resources,
  as well as their VROM addresses. The 139 previous segments are identical
  byte for byte. The same check fails on the previous image because
  `spot04_room_1` is missing. Logs: `build/PS2/kokiri-rooms-check.log` and
  `build/PS2/kokiri-rooms-before-check.log`. The game links 718 objects
  and is installed alongside the new data image; both match their verified
  files by SHA256. The window responds, and startup records no
  exceptions or lost PCM blocks. The user confirms the Link correction
  and that they can now enter the area; the log shows loading
  `spot04_room_2` and returning to room 0 without exceptions. When picking up the
  sword, another received-model failure appears, and the equipment view
  shows a corrupted Link portrait; these are investigated separately.
- Backups of the previous version: `oot_ps2-before-link-windows.elf` and
  `oot-before-kokiri-rooms.z64/json`, within `build/PS2-ntsc12`.

## NEAD mixing and final material cycle — 2026-10-03

- The user confirms 18 FPS while standing still and 16 while walking in the same view of
  fences and plants, with correct image and audio retained. The new profile
  measures about 30–31 ms of translation after subtracting DSP, with 7–10 ms of DSP
  interleaved and other interrupts still included. Material
  tables cost 1,5–2 ms; ENVMIX_NEAD costs 1,3 ms per audio task.
  Capture: `build/PS2/pcsx2-channel-kernels-kokiri-profile.log`.
- NEAD mixing uses signed 32-bit products: s16 × u16 fits within
  that range and preserves the 16 bits retained by the previous conversion.
  The eight samples in each group are unrolled, preserving the order of
  reads, writes, saturation, XOR, and ramps, including aliasing between
  buffers. Overlapping controls retain the previous path.
- The table builder omits the second cycle when all evaluated
  channels only copy their first-cycle value, already bounded to 0–255.
  RGB still includes intermediate alpha; every other equation
  retains both cycles. Constants and cache keys are unchanged,
  and checks of textures modified in RAM are not omitted.
- Full EE/GS tests: 53248 mixing cases, including long and
  overlapping buffers; 20 materials and 2880 partial masks compared with the
  scalar reference for every texel value; 1408 passes with
  the final cycle omitted and 1046 retained. RGBA16, LRU, RESAMPLE,
  FILTER, lighting, clipping, dialogue, and 120 frames pass without differences.
  Log: `build/PS2/pcsx2-envmix-identity-pass.log`.
- Microtests comparable with the previous version: mixing
  20159461→15479007 cycles (23 % fewer), per-channel table cache
  11978894→9004768 (25 %). These improve isolated tasks and do not
  predict FPS. The game links 718 objects, is installed with SHA256
  identical to the new ELF, and opened without optional profiles. Game
  performance comparison remains pending. The window responds, and the first
  minute of PCM production loses no blocks and records no delays.
  Backup:
  `build/PS2-ntsc12/oot_ps2-before-signed-envmix.elf`.
- At the end of the previous session, invalid accesses are recorded during
  scene object loading and Actor_Spawn. The user confirms that
  they entered an area not yet prepared and asks to leave it until after
  FPS optimization. The complete log is preserved in
  `build/PS2/pcsx2-channel-kernels-end-session.log`; expanding asset conversion
  for that area remains pending.

## Profile of the slow Kokiri view — 2026-10-03

- The user confirms that the letters on the shop wall disappeared.
  The new outside capture, looking toward fences and plants,
  shows 14 FPS; their reply places that view at 14–16 FPS. Maintaining
  the game's natural 20 FPS in every camera view remains pending.
- The profile of that camera measures about 41 ms of translation after subtracting
  14–18 ms of DSP synthesis; other interrupts remain included. Combiner
  tables consume 3,5–4,2 ms with 9–10 builds per
  frame. Two I8 sources of 32×64 are converted again with seven
  environment colors whose alpha changes between frames. RESAMPLE costs
  0,7–1,6 ms per audio task and FILTER around 1 ms. No lost PCM
  blocks or new queue errors are recorded in the capture preserved
  in `build/PS2/pcsx2-kokiri-before-channel-kernels-profile.log`.
- Combiner tables reuse R, G, B, and alpha separately according to
  the constants affecting each channel. COMBINED and
  COMBINED_ALPHA propagate between cycles, preserving the intermediate alpha needed by
  RGB. Donor channels are copied before replacing the LRU entry,
  even when that entry is a channel's only donor. The partial
  builder preserves unrequested channels and RGBA16 tables.
- RESAMPLE uses consecutive pointers when the entire traversal and history
  fit in the actual buffer; it preserves the masked path for overflows
  and high indices. FILTER unrolls its eight products per output and uses
  unsigned 32-bit sums, preserving bits 15–30 of the previous
  64-bit result. Sequential updates of overlapping LUTs
  and raw history are preserved before copying the result.
- Exterior vertices are not projected initially, as they are rejected or
  projected again after clipping. Degenerate W, lighting, and
  texgen are preserved. Clipping reuses distances per plane, avoids
  integer divisions to traverse edges, and omits interior copies.
- Full EE/GS diagnostic without differences: 10496 RESAMPLE cases with
  both paths, 5121 FILTER cases and continuity; 20 materials and 2880 partial
  masks with a scalar reference, RGBA16, and LRU; 20432 polygons and 192
  comparisons of complete GS points, fog, decals, and G_CULLDL. Descriptor
  lifetime, dialogue, lighting, queues, the rest of audio,
  and 120 frames also pass in `build/PS2/pcsx2-channel-kernels-pass.log`.
- Microtests: RESAMPLE 16289975→11482973 cycles (30 % fewer), FILTER
  34074103→9350015 (73 %), complete tables→per-channel cache
  36307363→11978894 (67 %, including the helper's output copy),
  clipping 30798583→21546576 (30 %). The batch with 75 % exterior vertices
  drops from 9950956 to 8190698 (18 %); the fully visible batch is comparable
  (9647164→9695667). These are isolated tasks and are not extrapolated to FPS.
- The game is rebuilt with 718 objects, installed, and opened without optional
  profiles. The installed ELF matches the newly linked one by SHA256;
  startup produces PCM without lost blocks, and the window responds.
  User comparison in the same view: 18 FPS while standing still and 16 while walking,
  with correct image and audio. This is still not stable 20 FPS.
  Backup of the previous version:
  `build/PS2-ntsc12/oot_ps2-before-channel-kernels.elf`.

## Letters on the wall and slowdown when selecting seeds — 2026-10-03

- The user perceives an overall improvement with geometry reuse,
  but notices huge letters on the wall when selecting Deku Seeds in
  the shop, with a drop to 8 FPS. The dialogue below retains its size.
  The session records 271 uploads and only 28 hits per frame, zero texture
  retirements, and about 94 ms of translation. The previous log is preserved in
  `build/PS2/pcsx2-lighting-queues-runtime.log`.
- LRU eviction could lose a descriptor used by GIF draws still
  pending, even if the new upload occupied another free VRAM region.
  The previous region lost its history, and a later source could
  overwrite it without FINISH. Draws are now retired before losing
  that descriptor, in addition to checking VRAM overlap.
- Capacity increases from 256 to 512 descriptors, with 128 buckets and the same
  VRAM budget. Text shadow and color generate variants
  for each font source; the dialogue set exceeded 256 entries and
  forced continuous reloading. All RAM content
  and blending inputs are still checked, without freezing animated textures.
- Regression on the actual GS with 513 queued glyphs: when discarding the first
  descriptor, its region `0x230000` does not overlap the new `0x2b0000`, but
  the required FINISH occurs. A set of 300 glyphs generates 300 uploads
  on the first traversal and zero on the second. The rest of rendering,
  audio, lighting, queues, and 120 frames passes without failures in
  `build/PS2/pcsx2-dialogue-cache-pass.log`. The game is rebuilt with 718
  objects. The same diagnostic with the previous condition, retaining
  all 512 entries, fails exactly at retirement: zero FINISH instead
  of one (`build/PS2/pcsx2-descriptor-old.log`). This verifies that increasing
  capacity alone does not fix the VRAM lifetime failure.
- The version is installed and opened without profiling. Backup
  `build/PS2-ntsc12/oot_ps2-before-dialogue-cache.elf`. Comparison of the
  same shop product and FPS in other views remains pending; full
  fidelity to the original is not claimed based on the smoke test.

## Early point reuse and lighting — 2026-10-03

- Result of the previous version confirmed by the user: shop at
  20 FPS while standing still and 14–16 while walking; outside usually 14–16, reaching
  20 depending on the camera. The first outside view still causes a
  larger drop before stabilizing. An improvement is perceived, but the natural
  speed is not yet maintained in every view.
- Lookup of a shared textured GS point occurs before repeating
  its color and depth. SHADE and fog indicators are calculated
  once per TRI group. Temporary clipping and decal points still
  prepare their full values; the two-pass floor retains its
  order. The reference adds SHADE RGB/alpha, depth bounds,
  repeated indices, and an effective check of the fog tag.
- Lighting reuses RGB per normal under the same light table,
  checked once per G_VTX. 256 entries occupy 3 KiB; the generation
  invalidates in O(1), clearing only when the counter overflows. Alpha and
  other vertex fields are preserved. Without directional lights, the ambient
  value is copied directly. Conversion to signed integer preserves
  the result of colors bounded to 0–255 and avoids EE branches.
- The first pass compares 133650 lighting cases, each with a
  scalar path, cache, and repetition with another alpha: zero differences.
  Microtest with the same lights/normals: 5003932 versus 3476681 cycles
  (30 % fewer in that isolated task). GS points, audio, and 120 frames
  pass in `build/PS2/pcsx2-lighting-point-pass.log`. This is not extrapolated to FPS.
- Independent review of early lookup: discarded colors and
  depths are not read when reusing a point; state changes
  still end TRI groups. The batched textured submission test
  drops from 309397 cycles in the previous capture to 187347 in this one;
  both captures are microtests, not measurements of the full game.

## Temporary queue lifetimes after loads and animations — 2026-10-03

- The previous session's log accumulates queue-table-full errors
  as play continues. Local DMA queues and animation queue
  slots retained their semaphore registration after
  completing the task; heap freeing does not cover their short lifetimes.
- On PS2, queues are released after their last message in four DMA paths,
  the timer wait, the scheduler synchronization task, and
  loading an animation frame. DMA error exits
  are included. The limit of 64 is not increased, and queues are not freed within generic
  receive. Other platforms' logic is preserved.
- The queue lifetime test adds 384 completed tasks at 96 different
  addresses, retaining an independent pending queue, in addition to the
  768 heap lifetimes already covered. The final pass passes those 1152 cycles,
  lighting, rendering, audio, and 120 frames without failures:
  `build/PS2/pcsx2-lighting-queue-pass.log`. The game with the seven callsites
  is rebuilt with 718 objects. The version is installed and opened without profiling;
  backup `build/PS2-ntsc12/oot_ps2-before-lighting-queues.elf`, previous log
  in `build/PS2/pcsx2-color-runs-runtime.log`. FPS comparison
  and checking a long session with the corrected callsites remain pending.

## Profile excluding synthesis and untextured geometry batches — 2026-10-03

- The user confirms the presentation improvement: 16–18 FPS while walking,
  around 20 while standing still (sometimes 18 depending on the camera); shop at 14–16 FPS.
  Log of that version: `build/PS2/pcsx2-present-overlap-runtime.log`.
- The optional profile subtracts DSP synthesis cycles from its graphics
  times. A repeated counter read avoids capturing a start
  interrupted midway through measurement. Note preparation, other threads, and
  interrupts remain included; these are not fully exclusive times.
  The shop capture subtracts 8–13 ms of DSP: approximately
  45 ms remain, with 12 ms of vertex loading/processing and 25 ms of triangles.
  About 2577 untextured color evaluations cost 4 ms per frame.
  Log: `build/PS2/pcsx2-shop-self-profile-runtime.log`.
- Consecutive TRI groups now also batch untextured geometry,
  up to 20 triangles per packet, and reuse the color of the original
  shared vertices. Any state change/load/jump ends
  the group. Decal depth is still calculated per triangle;
  temporary clipping vertices retain their independent evaluation.
  The two-pass textured floor retains its previous order.
- GS point reference between immediate and batched submission: color, alpha,
  fog, decal depth, clipping, culling, capacity, and material/vertex
  change. The first pass detected that immediate TRI2 submission
  needed to restore its type after an untextured batch; this was corrected
  before installation. Final microtest: 397057 versus 247976 cycles (38 % fewer
  in isolated untextured geometry submission, without extrapolating to FPS).
- The second full pass passes rendering, audio, inventory background,
  and 120 frames without failures in `build/PS2/pcsx2-color-run-pass.log`.
  The subsequent transition to textured TRI2 also passes: 100 immediate
  packets versus 10 batched, without differences. The game is rebuilt
  with 718 objects. Subsequent comparison: shop while standing still 20, walking
  14–16; outside usually 14–16, with some views at 20.
- Independent review of the change: the fog fixture mode was corrected
  to actually enable fog, and points are checked to use XYZF2.
  That final full pass also passes. The version is opened with profiling
  disabled; backup `build/PS2-ntsc12/oot_ps2-before-color-runs.elf`.

## CPU update during GS presentation — 2026-10-02

- Result confirmed by the user after the audio optimizations:
  correct sound, 16–18 FPS while standing still with peaks of 20; 10–14 FPS
  while walking. The target remains unmet during movement. The log shows
  CPU updates of about 4–6 ms and presentation of about 10 ms, in addition
  to graphics translation and audio synthesis.
- Submission/retirement and presentation are separated. The worker emits FINISH,
  executes the queue, and waits for the GS before releasing the task data and
  signaling DP_DONE. It first frees the mailbox to accept a pending task without
  blocking the scheduler. The CPU can advance while waiting for the refresh;
  the worker finishes presenting before interpreting the next list.
  Audio priorities and the 20/30 Hz game/menu limits are preserved.
- The refresh is armed before DP_DONE. Presentation preserves a refresh
  that occurred while the CPU was updating, avoiding clearing it and waiting for
  another when the worker resumes. The buffer swap and setactive follow the
  installed SDK's sequence, also checked against the official gsKit code:
  https://github.com/ps2dev/gsKit/blob/master/ee/gs/src/gsCore.c.
- Test of the actual task bridge: 64 frames with artificially slow translation
  and presentation. Audio before DP, pending submission without waiting for
  presentation, separate command buffers, and serialized presentation:
  zero failures in `build/PS2/pcsx2-present-task-pass.log`.
- Actual GS diagnostic: three presentations after a 20 ms wait preserve
  the refresh and alternate buffers without another vblank wait (a maximum
  of 153 cycles measured in that call). Full audio/rendering and 120
  frames pass in `build/PS2/pcsx2-present-gs-pass.log`. Game rebuilt
  with 718 objects and opened; backup:
  `build/PS2-ntsc12/oot_ps2-before-present-overlap.elf`. User comparison of
  the outdoor area and inventory is pending; no FPS improvement is claimed
  from these isolated tests.

## Reusing NEAD mixer controls — 2026-10-02

- Mixing keeps gains and XOR masks in local variables during
  each group of eight samples. It continues updating the envelope between groups
  and preserves read/write order when buffers overlap.
  If the controls are inside a destination, the previous loop is used.
  Frequency, precision, reverb, and note count are not reduced.
- Independent reference: 40960 cases with rounded lengths, volume ramps,
  saturation, XOR inversion, wet-channel swapping, and
  sample/control/step overlaps. Zero differences in memory and
  final state. 1000 blocks of 256 samples: 22480504 cycles previously
  versus 20190423 on the new path, approximately 10 % less in
  isolated mixing. This does not represent a percentage improvement in FPS.
- The full diagnostic passes audio and 120 frames:
  `build/PS2/pcsx2-envmix-pass.log`. Game rebuilt with 718 objects and
  opened; backup: `build/PS2-ntsc12/oot_ps2-before-envmix-cached.elf`.
  Log from the previous ADPCM version preserved in
  `build/PS2/pcsx2-adpcm-unrolled-runtime.log`. User comparison of outdoor FPS
  and confirmation of the sound are pending.

## Synthesis cost and ADPCM predictor — 2026-10-02

- The previous audio counter measured note preparation but excluded
  the synthesis performed by the scheduler. The new counter measures that
  entire task: approximately 6–8 ms per block at 60 blocks per second
  in the observed scenes. That work interrupts the renderer and also
  appears in its timings. These are not exclusive times for each thread.
- Private per-opcode profile, enabled only with
  `host:ps2-audio-profile.enable`: ADPCM costs 2.4–3.7 ms per sampled task,
  ENVMIXER 1.8–2.7 ms, and RESAMPLE 1.3–2 ms. Logs preserved in
  `build/PS2/pcsx2-dsp-profile-runtime.log` and
  `build/PS2/pcsx2-audio-op-profile-runtime.log`. The enable file
  was removed before opening the version for gameplay.
- Eight-sample ADPCM predictor unrolled for EE: removes the triangular
  product loop, preserving the 32-bit sum, saturation, and history.
  Partial inputs and overlapping buffers use the previous path.
  4096 cases, lengths 0–8, extremes, overflow, and aliasing produce
  exactly the same samples. 10000 calls: 5541506 cycles for the
  reference versus 3500004 for the new predictor, approximately 37 % less
  in that isolated operation, without extrapolating it to the game's FPS.
- The full diagnostic passes audio and 120 frames in
  `build/PS2/pcsx2-adpcm-unrolled-pass.log`. Game rebuilt with 718 objects
  and opened; backup: `build/PS2-ntsc12/oot_ps2-before-adpcm-unrolled.elf`.
  Comparing outdoor FPS and checking the sound during gameplay are pending.

## Reusing state sent to the GS — 2026-10-02

- The user confirms that native reads do not change FPS either.
  New outdoor capture: triangle submission takes approximately
  13–38 ms per sample; game updates, about 5 ms. Samples
  include interruptions and vary with the camera. Log:
  `build/PS2/pcsx2-native-read-profile-runtime.log`.
- ALPHA, TEST, and ZBUF retain the last value actually sent for each
  GS context. Repeated packets are skipped; copies of RDP materials
  share that hardware reference. Ground blending passes and
  temporary depth clearing update the same cache when changing
  and restoring state. ZBUF includes the depth address and format.
  The cache is invalidated at the start of each task and when initializing
  the renderer, respecting gsKit's buffer and context settings.
- GS queue test: 300 repeated updates of the same mode
  (including partial commands and filtering) add no packets after the
  first three submissions. Context changes and switches back, ZBUF
  address/format changes, restoration from another RDPState copy, and
  depth clearing are verified. The full diagnostic passes 120 frames
  without failures, including texture and audio tests:
  `build/PS2/pcsx2-gs-state-pass.log`.
- Per-frame write and reuse counters, plus othermode time
  in the optional profile. Game rebuilt with 718 objects and
  opened with profiling disabled; backup:
  `build/PS2-ntsc12/oot_ps2-before-gs-state.elf`. In one outdoor sample,
  323 states are emitted and 735 are reused. The user confirms unchanged FPS;
  the target of a stable 20 FPS is not considered achieved. Log:
  `build/PS2/pcsx2-gs-state-runtime.log`.

## Native command and geometry reads — 2026-10-02

- Vertex, matrix, and viewport fields communicate their actual two-byte
  alignment to the compiler; words use four bytes. The interpreter reads
  both commands with a single alignment decision. The EE object uses
  LH/LHU and LW on aligned paths, avoiding the previous byte-by-byte reads and
  stack copies. Unaligned sources and the optional big-endian asset
  mode are retained.
- Test of 4096 cases: 16 offsets, signed and unsigned values,
  individual words, and command pairs. GCC transformed the first
  oracle into LWL/LWR loads; comparisons failed even though the printed
  values matched. The independent reference reads volatile bytes and
  returns a word from memory (LW). Comparisons pass with no differences;
  PC references also pass in both endianness modes.
- For 50000 aligned commands, 2037223 cycles for the previous reader versus
  709046 for the new one (approximately 65 % less in this isolated read).
  Unaligned: 2034519 versus 2122237; both checksums match. That
  small additional cost comes from checking alignment. Full
  diagnostic: 120 frames and zero failures in
  `build/PS2/pcsx2-native-read-pass.log`, including textures, audio, and saving.
- Game rebuilt with 718 objects; backup of the previous version:
  `build/PS2-ntsc12/oot_ps2-before-native-read.elf`. The game's FPS
  did not improve according to the user; the microtest is not extrapolated
  to outdoor performance.

## Aligned reads in texture checks — 2026-10-02

- EE object inspection: the hash's four-byte memcpy generated
  four LBU, four SB to the stack, and one LW for each word, even for
  aligned sources. The loop now checks alignment only once and
  communicates four-byte alignment to the compiler. The generated object
  uses one LW per word on that path; unaligned sources and tails
  retain the previous read. The XXH32 algorithm is unchanged, and no
  bytes, rows, or ground sources are skipped.
- Reference vectors, 16 alignments, two seeds, tails, and mutations
  of every byte pass. For 1000 hashes with different seeds and identical
  contents: 2048 bytes require 5678896 aligned cycles versus 12931413
  on the unaligned path; 4096 bytes, 11259006 versus 25781022. Checksums
  match (approximately 56 % fewer cycles in that isolated operation).
- The full diagnostic passes 120 frames and zero failures:
  `build/PS2/pcsx2-aligned-hash-pass.log`. Game rebuilt with 718 objects
  and opened to measure the actual effect; backup:
  `build/PS2-ntsc12/oot_ps2-before-aligned-hash.elf`. The user confirms
  similar FPS and a slight perceived improvement while walking. The hash
  percentage is not extrapolated to the full game; a stable 20 FPS remains pending.

## Reusing GS vertices — 2026-10-02

- Consecutive TRI groups reuse RGBAQ, STQ, and XYZ from shared
  original vertices. They also retain their fog and avoid repeating
  the application of the same tile. The cache lasts only for the group: any state
  command, vertex load, jump, or end of list ends it. Flushing a
  packet due to capacity retains the data. Temporary clipping vertices
  and decals with slope-based depth bias are excluded;
  the two-pass ground retains its previous path.
- Diagnostic-only capture compares full GS points with
  the immediate path, including perspective, fog, dynamic alpha,
  decals, clipping, and coordinate/PRIM changes between groups. They match
  with no differences. In 42 triangles sharing three vertices, 126
  preparations become 3, with 123 reuses. The full diagnostic
  passes 120 frames: `build/PS2/pcsx2-point-reuse-pass.log`.
- The private profile separates material compilation (included in conversion)
  and point preparation (included in submission). Counters show
  computed and reused points. The game compiles with 718 objects and is
  opened to check performance and the image; backup:
  `build/PS2-ntsc12/oot_ps2-before-point-reuse.elf`. The user confirms
  an unchanged cutscene, 10–12 FPS while walking and 14–16 while standing still:
  this change gives no perceptible FPS improvement. Log:
  `build/PS2/pcsx2-point-reuse-runtime.log`. The target of
  20 FPS is not considered achieved.

## Colors of untextured geometry — 2026-10-02

- After table-based conversion, the user confirms 14–16 FPS while standing still and
  about 12 FPS while moving; previously, they observed 10–12 FPS. They also report
  drops when Link appears in the cutscene. Those drops are not all attributed
  to the model without measuring other factors.
- Untextured polygons compile combiner selectors and constants
  per material, preserving the four shade channels, combined alpha
  dependencies, two cycles, and saturation in each cycle. SHADE is applied
  directly when the final equation allows it. The key checks both
  commands, cycle type, PRIM, and ENV even if state is restored
  without calling setters. The previous scalar evaluator is retained as an oracle.
- Differential test: 64 materials, four cycle types, all values
  of each shade channel individually and mixed, with no differences. For
  10000 evaluations, the three measured cases go from 8114418/7983042/15601124
  to 2469549/1188681/5104464 EE cycles (approximately 67–85 % less work
  in that isolated calculation; this does not represent the game's FPS increase).
  Full diagnostic: `build/PS2/pcsx2-vertex-color-pass.log`, 120 frames
  and zero failures, including audio and the existing texture tests.
- The private profile adds the time and number of untextured color evaluations,
  included in triangle preparation. The game compiles with
  718 objects; backup: `build/PS2-ntsc12/oot_ps2-before-vertex-color.elf`.
  The user replies “I don't notice a difference”: no FPS improvement is
  attributed to this change. The actual profile shows that untextured color
  calculation already takes little time; the rest of submission is still under investigation.
  Log: `build/PS2/pcsx2-vertex-color-runtime.log`.

## Intensity and palette conversion — 2026-10-02

- Optional diagnostic of texture occupancy and reload reasons, limited
  to 24 entries per sampled frame. It prints after measuring
  rendering and remains disabled on normal launches. With Link standing still
  outdoors, I4/I8 color/alpha changes predominate (fairies and effects),
  rather than source-byte changes. The cache uses nearly all of its 1792 KiB;
  increasing only the entry count does not eliminate that work. Some samples
  spend 20–24 ms on conversion, including interruptions from other threads.
  Log: `build/PS2/pcsx2-texture-cache-profile-runtime.log`.
- I4, I8, IA4, IA8, CI4, and CI8 prepare a table of 16 or 256 final colors
  per load. Each texel is expanded with one table read and one
  32-bit write. IA16 prepares separate tables for intensity and alpha. The
  previous GS alpha quantization, CI4 palettes, mirroring, and two-texture
  materials are preserved exactly; all source bytes are still checked
  to detect animations and inventory background changes.
- Differential EE test: all input codes for the seven
  formats, the 65536 IA16 intensity/alpha pairs, five materials, and
  CI4 palettes 0/3/15 match the previous conversion. The full diagnostic
  passes 120 frames, including audio, saving, and the renderer:
  `build/PS2/pcsx2-compact-textures-pass.log`.
- The user confirms 14–16 FPS while standing still and about 12 while moving, versus
  the previous 10–12 FPS. This is another partial improvement; a stable
  20 FPS has still not been reached. Log: `build/PS2/pcsx2-compact-textures-runtime.log`.

## Inventory strips and geometry batching — 2026-10-02

- The user confirms that audio is fixed and that, with the new
  strip batching, “The inventory now works well.” The outdoor area still
  runs at 8–10 FPS and needs further optimization. The PCSX2 version will
  be stabilized first; tests on an actual PS2 will be a later phase.
- PreRender no longer limits its loads to the 4 KiB of N64 TMEM on PS2:
  the native translator reads source RAM directly. It uses strips of up to
  32 rows, bounded by the 256 KiB conversion buffer. A 320x240 copy
  goes from 40 to 8 strips; coverage composition, from 60 to 8.
  In CT32, gsKit allocates 40 KiB for both a 320x6 strip and
  a 320x32 strip: the background goes from 1600 KiB to 320 KiB. Coverage uses
  another logical TMEM address to keep both sources separate.
- TEXRECT subtracts the LOADTILE origin before drawing the subregion,
  as 3D vertices already did. It preserves drawing coordinates and
  avoids sampling outside a strip whose source is already offset.
- Test with the lists generated by production PreRender: copy,
  coverage composition, tint, and region copy cover all 240 rows
  with eight rectangles. The first draw loads eight textures; the next
  loads zero, even after introducing 64 additional icons. Changing
  one pixel reloads only one strip. Full diagnostics pass
  120 frames: `build/PS2/pcsx2-prerender-pass.log` and
  `build/PS2/pcsx2-indexed-cache-pass.log`.
- Cache lookups are indexed in 64 groups by source address,
  preserving content, palette, and material checks. Replacements
  unlink evicted entries before reusing them.
  Collision, RAM change, replacement, and GS fence tests pass.
  The user perceives no further outdoor FPS improvement from
  this index; that bottleneck is not considered resolved.
- Consecutive TRI1/TRI2/QUAD commands share packets of up to
  20 triangles. They are flushed before any state change, vertex
  load, jump, or end of list; two-pass ground materials
  continue to be drawn separately. Tests of mixed commands, capacity
  limits, discarded polygons, and state changes pass. A
  comparison of 200 triangles reduces 100 packets to 10 and goes from 450379
  to 418223 EE cycles (about 7 % in that isolated test). The user
  still observes 8–10 FPS outdoors: the savings do not resolve the lag.
  Diagnostic: `build/PS2/pcsx2-triangle-runs-pass.log`.
- The detailed profile separates matrices, transformation, lighting, coordinate
  generation, clipping, and triangle preparation. In a static outdoor view,
  3555 vertices require about 2–3 ms for transformation and
  2–3 ms for lighting; matrices and clipping take about 1 ms each.
  Triangle preparation consumes 22–40 ms, including texture
  lookups and conversions. Interruptions can increase any
  sample; times for phases included within other phases must not be added together.
  Log: `build/PS2/pcsx2-geometry-profile-runtime.log`.
- RGBA16 conversion combines 5-5-5-1 expansion and the material table
  in a single pass, using packed channels; paths combining
  two textures retain their previous alpha calculation. Reciprocal scales
  of GS dimensions are prepared when loading or retrieving a texture, and
  coordinate shifts use exact powers of two. Neither resolution
  nor color format changes. Exhaustive tests of 65536 inputs
  for copy, tint, and two cycles pass, as do 512 combinations of
  shifts and perspective with negative coordinates and tile origins.
  The full diagnostic passes 120 frames, including the test of
  corrected audio: `build/PS2/pcsx2-fused-textures-pass.log`.
  Game rebuilt with 718 objects and opened to measure actual FPS;
  backup: `build/PS2-ntsc12/oot_ps2-before-fused-textures.elf`.
  After testing it, the user confirms “12 fps now 10-12,” versus the
  previous 8–10 FPS. This is a partial improvement; a stable
  20 FPS has still not been reached. Log: `build/PS2/pcsx2-fused-textures-runtime.log`.
  The private profile enable file is removed so that subsequent normal
  launches do not perform per-command measurements.

## Rendering, texture, and voice profiling — 2026-10-02

- Startup and menus now limit graphics admission to a maximum of 30 Hz
  (two retraces). Gameplay retains its updateRate=3, equivalent
  to a maximum of 20 Hz. The emulator's VPS=60 measures retraces and is not
  the game's frame rate. The production scheduler passes the
  updateRate=0/1/2/3 cases, priorities, busy DP, and counter wraparound.
- Optional profile through `host:ps2-gfx-profile.enable`, sampled every
  60 frames with COP0 Count. Separates vertices, triangles, rectangles,
  cache lookups, conversion, and GS upload. Times include interruptions
  from other threads; texture phases are included in tri/rect. Without the
  private file, the counter is not read on every command.
- Observed bottleneck: in the outdoor area, one sample spends 55 ms
  converting textures within 93 ms of graphics translation; the inventory
  reloads 85–122 textures per frame and consumes 123–244 ms. Gameplay
  updates take much less time than waiting for rendering. Previous logs:
  `build/PS2/pcsx2-before-lut-runtime.log`.
- Combiner tables compile selectors once and evaluate their
  simplified references. Cache expanded from 16 to 64 materials. Differential
  test against the previous equations: 31 materials, three cycle types,
  all values of each channel, including alpha and transparency.
  The user confirms some improvement but still observes 12 FPS or less and
  lag when opening the inventory: the performance target has not yet been achieved.
- Second optimization: block-based XXH32 replaces the per-byte FNV chain
  when checking texture contents. All bytes are still read,
  including noncontiguous rows and the second material in the paths. libxxhash
  vectors, 16 alignments, and changes to every byte pass on EE; 2000
  independent comparisons pass on the host. Per-frame checks that could
  bring back the ground flicker are not skipped.
- An invalid vertex read when opening the inventory caused TLB misses
  in f3dex2_vtx. Loads outside EE RAM are discarded, and the
  affected vertices are invalidated; a subsequent valid reload works. The origin
  of that corrupt pointer still requires diagnosis.
- Private capture accepts `voice` in `host:ps2-audio-capture.enable` and
  starts with the first voice-bank request. Child voice
  0x6820 was captured before output: the user confirms that it also sounds wrong.
  This places that defect before the IOP driver, although it does not exclude
  other playback faults. Note parameters are added during capture.
- HILOGAIN fixed: the game defines its argument as unsigned UQ4.4.
  The interpreter treated it as int8 and turned gains >=0x80 negative.
  Tests of both polarities and saturation for 0x10/0x7f/0x80/0xff pass.
  The voice is not considered fixed until its actual behavior is confirmed.
- The user confirms that gain does not change the voice cutoff or eliminate
  the thumps in the inventory; the second optimization does not improve
  their perceived FPS. The profile confirms that the menu still reloads about
  134–137 textures per frame: that load still needs to be reduced.
- A specific cause found in Audio_ProcessNotes: an N64 check
  rejected parentLayer for being below 0x7fffffff. All valid EE layers
  live below 32 MiB, so active notes did not
  update initialization, pitch, and volume. PS2 validates the native RAM
  range; N64 retains its check. A production test is added
  with a valid EE layer that verifies initial state, pitch, volume, and silence
  after muting on the next tick.
  The test passes (pitch 0xa000, volume 723/723 on the first note), and the
  full diagnostic passes 120 frames without failures in
  `build/PS2/pcsx2-live-notes-pass.log`. Version rebuilt and opened;
  backup: `build/PS2-ntsc12/oot_ps2-before-live-notes.elf`.
  The user confirms: “the audio is now fixed” with this latest version.
  The gameplay log now shows notes initializing during
  capture (previously they did not appear): `build/PS2/pcsx2-live-notes-runtime.log`.
  Private enable files are removed after capture; subsequent
  launches do not enable profiling or sound recording.
- New ADPCM tests with a nonzero predictor: whole-stream decoding versus
  separate blocks, history, and A_LOOP. The full diagnostic passes 120
  frames and zero failures: `build/PS2/pcsx2-hash-gain-pass.log`.
  Game built with 718 objects and opened with the existing memory card.
  Previous backup: `build/PS2-ntsc12/oot_ps2-before-hash-gain.elf`.

## GS frame synchronization — 2026-10-02

- The user confirms that some previously missing effects are now audible; Link's
  voice cuts off while the music continues. Audio is not considered fixed.
- On PS2 only, the scheduler stops waiting for the dummy N64 buffers after
  the DP_DONE signal: the graphics thread has already finished GS
  presentation. Admission of new tasks respects updateRate from the previous
  start (20 Hz with updateRate=3, 30 Hz with 2, 60 Hz with 1), without accumulating catch-up work after
  a slow frame. DP exclusion and audio priority are preserved,
  along with logical VI and fault framebuffer updates.
- The production scheduler test passes 20/30 Hz limits,
  late frames, audio priority, busy DP, synchronization tasks,
  and counter wraparound. Full diagnostic: 120 frames and zero failures,
  `build/PS2/pcsx2-sched-pass.log`. Game build: 716 objects.
- Comparison of the title sequence around frame 600,
  with the same geometry counts (1608 triangles, 691 draws): the
  CPU period goes from 61.4 ms to 50.6 ms, close to the 20 FPS limit.
  Graphics translation remains at about 31 ms. Outdoor gameplay (scene
  85) still has samples of 66–102 ms per frame, depending on camera and
  geometry: a stable rate of 20 FPS outside the house has not been reached.
  This change does not fix the choppy voice.
- Version opened with a visible window and the existing memory card. Backup
  of the previous ELF: `build/PS2-ntsc12/oot_ps2-before-gs-pacing.elf`.

## Sound sequences and read cache — 2026-10-02

- The user confirms that the captured internal audio sounds clean, but
  clicks increase with output lag and Link's voices are inaudible.
  The outdoor area still drops to 8–10 FPS; the performance target has not been achieved.
- Fixed two sequence-table operands that were read as native u16
  even though the scripts remain big-endian. ENV envelopes within
  sequences also retained that byte order: ADSR now identifies
  its origin on initialization and reads its points correctly. Already-converted
  sound sources and compiled envelopes retain their native
  reads. ADSR state retains its size/layout. Confirmation of
  the effect on voices and sound during gameplay is pending; they are not considered fixed.
- 64 KiB LRU cache for small, repeated ROM reads, shared
  under the existing mutex. Large loads do not evict audio sectors.
  Read writeback limits its range using the SDK's SyncDCache.
  Update, wait, audio, and read/hit measurements are added.
- EE tests: unaligned BE envelope, sequence alias, ADSR jump,
  comparison with native source, and reinitialization; cross-sector reads,
  hits without I/O, eviction, partial end, out-of-range access, reopening, and
  concurrent readers. The full diagnostic passes 120 frames, zero
  failures: `build/PS2/pcsx2-rom-cache-envelope-pass.log`.

## PCM queue and texture waits — 2026-10-01

- The user confirms that ground flickering is fixed; shadows and steps
  improved. The SDK's libsd module reduced clicks, but audio
  remains faulty and the outdoor area can still drop to 10 FPS.
- The previous log measures graphics translation of 55–77 ms in gameplay
  blocks, with 7–19 texture reloads/waits per sampled frame.
  There were 15 discarded PCM blocks in about seven minutes. These times
  include interruptions from other threads; they are not an isolated GS measurement.
- The cache remembers which accesses were retired by a FINISH barrier.
  Overwriting VRAM with pending reads retains the wait; other
  textures retired by that same barrier avoid duplicate waits.
  Every new texture binding marks it pending again,
  even if its descriptor was still loaded. Z24, alpha, and mirroring are preserved.
- Combiner decoding is cached by its words and modes,
  preserving each material's equations, colors, and factors.
- TRI2 groups its surviving triangles with the same material into a
  GS packet; it does not group two-pass ground or cross commands. Tests
  pass partial/total rejection, a subsequent TRI1, and the order of both passes.
  In one outdoor sample, 1325 draws used 887 packets and 17 uploads
  had a wait. The counter remained at 10 FPS: fewer packets do not
  demonstrate a sufficient improvement in frame rate.
- The SDK's audsrv could advance beyond the available samples and
  play old data, confusing an empty queue with a full one. The project
  compiles a local copy of the IOP server with explicit occupancy,
  serialized access, circular reads, and silence filling when PCM is missing.
  Format changes reset the queue; it is consumed when the
  SPU2 transfer signal arrives. The installed SDK remains unmodified.
  Sources and licenses: `src/platform/ps2/iop/audsrv/UPSTREAM.md`.
- EE tests pass a full queue, circular wraparound, twenty underruns,
  recovery, and reset, along with shared retirement and subsequent
  texture reuse. Full renderer/audio passes 120 frames:
  `build/PS2/pcsx2-pcm-ring-fence-pass.log`. The user confirms unchanged clicks
  after fixing the queue. There were no discarded blocks in about four minutes,
  but they appeared later; different play sessions prevent comparing the rate.
- Optional private capture accepts a delay in PCM blocks; the two-second
  gameplay sample has a peak of 7223/32768, zero clipping, and a maximum
  jump of 3425. The user hears it as clean. This does not rule out synthesis faults
  at other times. Capture flag removed after the test.

## Recording: shadows, mirrored repeat, and audio output — 2026-10-01

- Reviewed a local 48.42-second recording: file selection, Link's
  house, and exit into the forest. Windows audio contains large jumps
  and full-scale peaks. The previous two-second native title
  capture reaches only 570/32768 and does not clip; these are different moments,
  so the comparison still does not demonstrate where the noise is generated.
- The renderer ignored `G_TX_MIRROR`, present in forest materials.
  One normal period and one reversed period are cached per requested axis, with
  continuous coordinates and GS repeat across both periods. Mirrored variants
  have independent keys; alpha is preserved, and the enlarged image is not
  generated on cache hits. Textures that are too large
  retain the previous approximation to respect the available buffer.
- Shadows use `SETUPDL_44` / `G_RM_ZB_OVL_SURF2` (`ZMODE_DEC`). A
  slope-proportional depth bias is added only to decals
  with comparison and no Z writes. It is limited and saturated in Z16/Z24;
  ordinary geometry retains its depths. This approximates the
  N64's delta-Z tolerance; the user confirms improvement during gameplay.
- Repeated, unchanged colors and combiners retain loaded
  textures, avoiding additional hashes and lookups. Actual changes
  still invalidate them; simulation speed is unchanged.
- Navi uses green when pointing to objects in PROP/BG/etc. categories, according to
  `sAttentionColors` and `Attention_SetNaviState` in `src/code/z_actor.c`.
  The recording shows the `Check` action next to the picture in the house.
  That color change is consistent with the original behavior.
- The SDK's `freesd.irx` / libsd module is included before audsrv,
  retaining the ROM fallback if it fails. Output compatibility test;
  the user subsequently confirms fewer clicks. The build links 716 objects.
- The full EE diagnostic passes 120 frames; S/T mirroring,
  alpha pixel, cache variant, coordinate, decal slope,
  limit, and occlusion tests have zero failures. Log:
  `build/PS2/pcsx2-shadow-mirror-pass.log`. The user confirms improved
  shadows/steps and reduced noise, with a full solution still pending.

## Click diagnosis — 2026-10-01

- The user confirms that the tree is now correctly occluded, but
  clicks remain unchanged even with continuous 48 kHz output.
- Optional private capture of synthesized stereo PCM before sending it
  to audsrv. Creating `ps2-audio-capture.enable` next to the ELF enables a
  256000-byte capture after 900 submitted blocks. At 32 kHz, this is two seconds.
  It accumulates in RAM and writes `ps2-audio-native.raw` once; that
  write may briefly stall the diagnostic frame.
  Without the enable file, PCM is neither allocated nor copied.
- The full build and EE diagnostic pass, including audio
  tests and 120 frames: `build/PS2/pcsx2-pcm-capture-pass.log`.
  Capture allows synthesis defects to be separated from playback faults;
  the noise is not yet considered resolved.

## Neighboring resources of the starting forest — 2026-10-01

- The log captured a fault in `Room_SetupFirstRoom` when entering
  `spot10` (Lost Woods, entrance 011e). The scene had not been converted,
  and its room list was read as invalid native pointers.
- The `play` profile adds `spot10` with its ten rooms, the Kokiri
  houses, and the shop; their object dependencies are also extracted.
  The new image contains 139 native segments. Their room lists
  and ROM ranges, checksum, and byte-for-byte preservation of
  previously adapted segments are verified. Exploring those scenes is still pending.

## Continuous 48 kHz PCM output — 2026-10-01

- The user confirms that the previous FIR did not eliminate noise/clicks.
  Audio is not considered resolved based on the FIR test result.
- Synthesis retains 32 kHz; EE output interpolates stereo PCM to 48 kHz,
  maintaining phase and the previous sample between blocks. audsrv receives its native
  format, avoiding its conversion of 341 to 512 repeated samples per block.
  Reference: [audsrv upsamplers](https://github.com/ps2dev/ps2sdk/blob/master/iop/sound/audsrv/src/upsamplers.c).
- AI feedback converts the IOP queue to bytes at the game's sample rate
  before subtracting its reserve. Capacity is checked against the full
  output size before modifying phase; both channels are preserved.
- The EE test verifies interpolation of a stereo ramp, continuous output
  versus a split after 11 samples, rejection without advancing state, direct 48 kHz
  output, and AI feedback. The full diagnostic passes 120 frames:
  `build/PS2/pcsx2-output-resample-pass.log`. The user subsequently confirms
  that clicks persist: the conversion did not resolve the audible fault.

## Occlusion after expanding VRAM and audio filter — 2026-10-01

- Regression fixed: `gsKit_init_screen` changes CT16/Z24 to CT16/Z16.
  The renderer was still sending 24-bit Z, and truncation broke depth
  ordering. CT16S/Z24 is used to preserve RGB5551, precision, and a
  1792 KiB cache. The effective format is logged after screen initialization.
  Reference: [gsKit init](https://github.com/ps2dev/gsKit/blob/master/ee/gs/src/gsInit.c).
- Depth conversion checks the effective format, limits its
  extremes, and also supports Z16 without overflow. The EE test traverses
  65537 depths in four formats and requires monotonic ordering and bounds.
- The imported FIR filter directly indexed sample and coefficient pairs
  using the word order of emulated memory, even though EE
  buffers are consecutive. The PS2 path uses samples n-k and preserves
  the previous eight input samples. The two-block impulse fails
  at 9 samples before the change and passes with zero differences afterward.
- The full EE diagnostic passes 120 frames with color=0a, depth=01
  (CT16S/Z24), clipping, cache, and audio. Log:
  `build/PS2/pcsx2-depth-filter-pass.log`.
- The previous CT16 version reduced reloads in title frame 600
  from 102 to 1; mean translation time for the block went from 54003 to 29027 us.
  This compares the title; it is neither an outdoor measurement nor
  confirmation of audible quality. Occlusion confirmed by the user;
  paths still need validation during gameplay.

## VRAM and animated texture retention — 2026-10-01

- GS color changes from CT24 (stored in 32-bit words) to RGB5551 CT16,
  the original framebuffer format. The 640×448 output is preserved and Z24 is requested;
  the effective-format regression is fixed in the previous entry. The allocation grows from
  704 to 1792 KiB, limited to the actual available space minus 32 KiB.
  Size reference: [gsKit](https://github.com/ps2dev/gsKit/blob/master/ee/gs/src/gsTexture.c).
- A previous version of the same source/material is reused when its
  bytes, palette, or constant factors change in another frame. Variants
  used together are retained; historical versions previously accumulated
  and evicted stable textures. FINISH before overwriting is retained.
- The EE test with 320 color changes retains 32 stable textures, the VRAM
  of the changing texture, and two simultaneous colors from the same source.
  Combined clipping, cache, audio, and 120 frames in CT16 also pass.
  Log: `build/PS2/pcsx2-cache-reuse-ct16-pass.log`, actual allocation 1835008 bytes.
  The CT24 variant also passes with 720896 bytes in
  `build/PS2/pcsx2-cache-reuse-ct24-pass.log`.
- Resampling test added: a note at pitch 0.75 split into two blocks
  produces exactly the same samples as one continuous block, preserving
  FIR history and phase. This does not yet confirm the game's musical fidelity.
- Full build of 715 objects in `oot_ps2-next.elf`. Comparing the new
  version outdoors with the same camera and checking paths are pending.

## Cutout two-texture materials, stereo, and measurement — 2026-10-01

- The user confirms that the transparent holes in the staircase are fixed.
  Flickering on outdoor paths, performance, and distorted audio
  remain pending; they are not considered resolved by that confirmation.
- RGBA combination before cutout added for the two-tile RGBA16 material
  with the same mapping used in `spot04`. The cache key checks both
  sources, their bytes, strides, and PRIM_LOD_FRAC. The normal path is retained when
  tiles have different mappings. EE tests pass zero/full alpha,
  partial blending, second-source mutation, and cache retention.
- Removed two per-vertex SHADE queries and redundant CLAMP packets
  when the value/context does not change. Translation and flip measured every
  300 tasks to distinguish CPU cost from presentation waits.
- `StereoData` preserves the original bits of sequence and command bytes
  on EE; its fields were previously interpreted in reverse order. The test
  verifies each strong-channel, headphone, and stereo-policy flag.
  This corrects interpretation; its audible impact still needs confirmation.
- The renderer/audio diagnostic passes 120 frames, and the task bridge passes
  64 deliveries with audio before DP. Logs:
  `build/PS2/pcsx2-path-stereo-pass.log`, `build/PS2/pcsx2-task-profile-pass.log`.
  The previous log produced 370.136 s of PCM in 370.508 s, with no discarded blocks;
  that continuity does not confirm musical quality. Original timing is
  retained: during Play, R_UPDATE_RATE=3 (approximately 20 FPS NTSC).

## Ground blending and audio reserve — 2026-10-01

- The ground material in `spot04` blends TEXEL0 and TEXEL1 using
  ENV_ALPHA, then applies SHADE. The renderer previously replaced TEXEL1
  with TEXEL0. It now recognizes that opaque combiner and draws two GS passes,
  each with its own tile, coordinates, depth, and fog. Other two-texture
  combiners remain pending. Changing ENV_ALPHA retains both
  textures in the cache; with zero alpha, only the first pass is drawn.
- The AI length reported to the engine excludes 20 ms of audsrv reserve.
  It previously included the entire IOP queue, keeping the synthesizer on
  minimum-sized blocks. The test links the actual driver with a simulated server and verifies
  32/48 kHz, reserve, errors, and rejection of blocks that do not fit in full.
- Native EE tests pass two-texture blending, cache retention when
  changing the factor, exclusion of cutout materials, and the audio driver.
  Full build: 715 objects. Log:
  `build/PS2/pcsx2-ground-audio-pass.log`. Visual verification of the ground
  and audio continuity in the game remain pending.

## Alpha cutouts and cache cost — 2026-10-01

- The GS test now recognizes CVG_X_ALPHA used by TEX_EDGE materials,
  even when G_AC_THRESHOLD is disabled. It approximates partial coverage with
  a half-alpha threshold and also discards color/depth in the holes.
  For the explicit test, it uses blend-color alpha and updates its
  threshold when that changes. EE tests pass staircase mode, opaque surfaces,
  a variable threshold, and preservation of depth testing/writing.
- VRAM space selection uses per-block ages and a sliding maximum,
  preserving the previous eviction criterion with a linear search.
  It avoids waiting for FINISH in regions with no previous use; it retains the wait
  when overwriting a region or removing a valid descriptor. Tests of
  32-strip retention, 128 glyphs, a large texture, and eviction pass.
- Per-frame hit, load, and wait counters added to measure
  cache pressure. Visual verification of the staircase and performance
  comparison with the same in-game camera are still pending.

## Audio independent of the renderer and JPEG backgrounds — 2026-10-01

- Native GS fog connected to textured and untextured triangles, using FGE,
  XYZF2, and FOGCOL. Previously, only a generic calculation existed with no connection
  to drawing. It now interprets actual G_MW_FOG parameters over Z/W,
  inverts the factor for GS, and limits activation to the standard two-cycle
  FOG_SHADE_A stage. It preserves 24-bit depth and restores
  fog state before drawing backgrounds/text. EE tests pass
  start, midpoint, saturation, narrow range, and per-material disabling.
  STQ emission reference: [gsKit](https://github.com/ps2dev/gsKit/blob/master/ee/gs/src/gsTexture.c).
- Graphics translation moves from the scheduler to an EE thread with libultra
  priority 10, below Graph (11) and AudioMgr (12). It delivers SP on dispatch
  and keeps DP busy until drawing and the screen swap are complete.
  Alarms every 128 commands removed. The native test passes 64 slow
  tasks, with audio before DP and correct signal ordering.
- The new thread's initial priority of 13 was corrected during tests:
  it prevented AudioMgr from being serviced on time. With priority 10, the game submits
  14.939 seconds of PCM in 15 seconds, with no discarded blocks or intervals longer
  than two blocks. The previous attempt submitted 9.920 seconds in 68.251
  seconds, with 577 late intervals and 148 discarded blocks. Metrics measure
  delivery to audsrv; they do not yet confirm musical fidelity or sustained FPS.
- Native decoder using libjpeg, already installed in the SDK: dimensions and
  capacity limited to 320×240, big-endian RGBA5551 output, and copying only
  after completion. Damaged input retains the original JPEG. EE tests
  pass known colors, repetition, dimensions, and corruption.
- Recognition of F3DEX2/S2DEX changes and COPY/1CYCLE background rectangles.
  RGBA16 backgrounds with no rotation or shifted origin are drawn in strips of up to
  32 rows. The test verifies eight strips for 320×240 and their pixels, even
  after loading other textures. Other formats/origins remain pending.
- Bounded camera and background diagnostic in single-image rooms.
  The house resources contain untextured green polygons; checking
  when the background and fades should cover them in the cutscene is still pending.
- The game decodes the house's actual 320×240 JPEG and logs drawn
  S2DEX backgrounds. PCSX2 indicates 20 FPS in the observed cutscene. A long
  test continues producing PCM close to real time, although it logs
  some additional discarded blocks; it does not confirm perfect audio or a comparable
  benchmark across all scenes.
- Full build: 715 objects. Local test logs:
  `build/PS2/pcsx2-jpeg-background-pass.log`,
  `build/PS2/pcsx2-task-priority-pass.log` and
  `build/PS2-ntsc12/pcsx2-game.log`.

## Name letters, keyboard cache, and memory card — 2026-10-01

- `Font_LoadOrderedFont` interprets the big-endian halves of the message that
  orders NTSC letters. Previously, the bytes of SHIFT-JIS codes
  were swapped and other glyphs were loaded. PS2 also limits length and destination.
  The EE test links the actual loader, checks Latin letters, a line break,
  and an unterminated message, without reading beyond its length.
- Cache expanded from 64 to 256 entries, with 1 KiB blocks in the same
  704 KiB of VRAM. The keyboard caused repeated conversions when exceeding
  64 letter/color combinations. The test retains 128 glyphs between passes,
  along with medium strips, large textures, eviction, and combiner colors.
  This avoids repeated work; a comparable FPS benchmark is still pending.
- Compatible `TEXEL_ALPHA * PRIMITIVE_ALPHA` fades pass the variable factor
  to the GS vertex. Texture/LUT keys exclude that factor. The test changes
  alpha without reloading VRAM and rejects states that need the CPU path.
  RGBA32, IA8, and IA16 convert alpha 255 to 128, avoiding the previous maximum of 127.
- `libmc` receives IOP flags directly: newlib flags replaced with
  `FIO_O_*`. Reads previously returned -5. Verified against the code of
  [PS2SDK](https://github.com/ps2dev/ps2sdk/blob/master/ee/rpc/memorycard/src/libmc.c).
- Alternating `save.dat`/`save.bak` saving, revision and checksum, close
  checking, and rereading before declaring success. The previous format is also read.
  An error preserves the previous copy and allows initialization to be retried.
- Diagnostic with MCMAN/MCSERV and a new virtual card: two writes pass,
  selection of the latest revision, damage to the new copy, and recovery
  of the previous one. After closing and restarting PCSX2, all 32768 persisted bytes pass.
  The test uses `OOT_PS2_TEST`, separate from the game's save `OOT_PS2`.
- The new `oot-port.ps2` enabled in the isolated configuration; the previously
  configured card is retained. The game logs verified saves up to
  revision 5 and enters scene 52. This does not validate a complete playthrough.
- Private logs: `build/PS2/pcsx2-smoke-glyph-cache.log`,
  `build/PS2/pcsx2-memorycard-write-recovery.log`,
  `build/PS2/pcsx2-memorycard-cold-restart.log` and
  `build/PS2-ntsc12/pcsx2-font-card-first-game.log`.

## Cache, reflections, and file-select tests — 2026-10-01

The title capture shows Link riding Epona, terrain, walls, and
the Zelda logo in its colors. Start opens file select, showing its text,
buttons, and background. **A complete playable run has not yet been validated.**

- Texture cache with 64 entries and size-based allocation over the same
  704 KiB of VRAM. It retires draws before overwriting regions and invalidates
  all overlapping descriptors. The diagnostic checks retention
  of 32 medium textures and loading a 256×256 texture within the limit.
- Combiner table cache: reuses its 512 evaluations per state.
  Keys include only prim/env components that the combiner uses.
  Tests of color changes, eviction, and irrelevant colors in COPY cycle.
- Hashing source and palette bytes preserves the cache between frames
  without leaving modified textures stale at the same RAM address.
- Spherical and linear reflection coordinates from the normal transformed
  by modelview and both LookAt vectors from MOVEMEM. Tests with identity,
  rotation, linear mode, and a custom LookAt vector.
- The renderer briefly yields the thread every 128 commands to allow controller
  sampling during slow frames. Start has been checked in the emulator.
- `play` profile: 104 segments, including 573 player animation tables
  in native order and initial resources. Compressed JPEGs are preserved intact;
  decoding them and drawing prerendered backgrounds remain pending.
- The previous extended test exhausted the queue side table when changing
  scenes. Freeing an allocation or destroying a PS2 arena removes
  its semaphores. The diagnostic links the actual allocator and passes 768 cycles,
  arena cleanup, and preservation of an unrelated queue.
- Private logs: `build/PS2/pcsx2-smoke-cache-final.log` and
  `build/PS2-ntsc12/pcsx2-lut-filechoose.log`. The intermediate version showed
  4–10 FPS in the title and 20 in file select; these are neither a final
  benchmark nor validation of performance on PS2 hardware.

## Scene matrix, textures, and SRAM — 2026-09-30

The game links with 713 objects. Captures after correcting the identity
matrix show terrain, the moon, walls, and the Zelda title in their colors.
**It is still not playable:** visual fidelity, performance, and gameplay
validation remain pending. The scene runs at about 4 FPS.

- `gIdentityMtx` used `gdSPDefMtx`, which packs components into big-endian
  words. On EE, it swapped adjacent halves, including the homogeneous
  component: many scene vertices ended up behind the camera. PS2
  initializes the constant with native halves. The math test links
  `sys_matrix.c` and checks the actual constant as well as the converters.
- CI4 palettes loaded into their TMEM positions, preserving other palettes;
  affected CI descriptors are invalidated. IA4 decoder added with three bits
  of intensity and one of alpha. Both pass pixel tests on EE.
- Triangle coordinates sent using STQ, avoiding the UV clamp of gsKit
  helpers. Negative coordinates, repeat, shifts, and perspective when enabled
  by RDP are preserved. Regional clamp excludes padding
  in strips that are not powers of two. Mirroring, TEXEL1, and reflection
  coordinate generation remain pending.
- `SsSram_ReadWrite` connected to a 32 KiB shadow SRAM with a semaphore and
  offset-based access. A partial write saves the full image to
  preserve options and other save files. A missing card leaves SRAM empty;
  a failed write retains changes in RAM and logs the error.
- The memory-card backend checks type/format, directory creation,
  file version, and size before calculating its checksum. No card
  is formatted. The current isolated configuration has no formatted PS2
  card; actual persistence on it still needs validation.
- PCSX2 diagnostic: empty SRAM, partial writes, reload, backend error,
  bounds, actual identity, IA4, palettes, STQ, and previous tests pass. Private
  logs: `build/PS2/pcsx2-smoke-stq.log`, `build/PS2-ntsc12/pcsx2-identity.log`.

## Renderer and native synthesis — 2026-09-30

The diagnostic run in PCSX2 passes PCM, mixing, stereo interleaving,
ADPCM, 8-bit PCM, and history, along with geometry and system tests. It tests
the actual permanent allocator and batching of refresh notifications.
All three instrument-bank tests pass: shared structures,
Sample bitfields, and rejection of invalid pointers/predictor books.
**It is still not a playable game.** After the fixes, the title continues
animating, reaches 600 submitted frames, and enters the file-select
state when Start is pressed. The capture shows the Zelda logo, sky, and part of
Link/Epona; the scene, textures, and performance (about 4 FPS) are still lacking.
Audio submits non-silent buffers: for example, 2240 bytes accepted by
audsrv with a maximum amplitude of 12288. This verifies PCM generation and submission,
not the music's fidelity. No TLB exceptions or watchdog were logged
in that run; private logs are retained in `build/PS2-ntsc12`.

- Fixed the argument order of gsKit Gouraud triangles: colors
  were being interpreted as coordinates. The subsequent capture
  makes Epona recognizable; its colors are still wrong and the scene is missing.
- Homogeneous triangle clipping before dividing by W, checked with
  near-plane crossing and geometry completely outside the volume.
- Fixed blender fields, final cycle, actual Z-write control,
  rectangle coordinates, and the relationship between texture load/render
  descriptors, including their row strides.
- CPU combiner for prim/env constants and per-vertex modulation; a lookup
  table and a VRAM cache to reduce work. TEXEL1 is still approximated with
  TEXEL0; it does not reproduce all combinations, fog, or perspective.
- Fixed refresh notifications that filled the scheduler queue
  during a slow frame and caused SP/DP signals to be lost. The renderer
  had finished, but Graph waited until the watchdog. PS2 retains
  one pending refresh; other messages retain their order and semantics.
  The watchdog remains finite (60 seconds) and logs the latest stage.
- Rectangles in normal cycles use the combiner and primitive
  transparency; black fades were previously drawn opaque. The COPY cycle
  bypasses the combiner, as in RDP.
- Data profile expanded to demonstration scenes and their objects through
  explicit dependencies; extraction of all segments is validated.
- Soundfont conversion through their pointers and structures: instruments,
  percussion, effects, envelopes, samples, loops, and predictors. Compressed
  waveform data and sequence bytecode remain intact.
- NEAD/OoT synthesis interpreter adapted to EE, with GPL-2.0-or-later code
  from Mupen64Plus and provenance in `src/platform/ps2/audio/rsp_hle/README.md`.
  The game engine remains compiled as native code. AudioCmd byte order
  and s8/u16 arguments fixed, along with detection of pointers
  to already-relocated samples. DSP tasks and PCM amplitude are logged.
- Fixed the missing return in `AudioHeap_AllocPermanent` for PS2: the
  compiler returned the address of gAudioCtx, and instrument DMA
  overwrote audio state. The EE test verifies pointers, cache
  data, and pool exhaustion. `title_static` and
  `parameter_static` converted to continue testing file select.

## Full linking and scene loading — 2026-09-26

The full game now links with 707 objects. PCSX2 completes `Play_Init`, and the
diagnostic run reached 900 frames in the `Play` state in the title
sequence with no logged exceptions; **it has not yet been validated as
playable, nor has its image been visually confirmed**.

- Metadata generator expanded with actual message and resource addresses,
  and original microcode used to identify tasks; no placeholder resources.
- Fixed startup recursion within `memset`, using newlib/SDK.
  Header region reading corrected for EE byte order.
- SP/DP status, FCSR, and thread enumeration services completed; explicit
  game faults are written to the console before stopping.
- Private converter `tools/prepare_ps2_rom.py`: uses structures discovered
  by the upstream extractor, including resources without an initial XML declaration.
  It converts fields by size, preserves textures and byte blobs, and logs
  each resource in a manifest. It does not automatically convert the entire ROM.
- Conversion of Hyrule Field, common resources, adult Link, Epona, and the title.
  Includes scene objects and their collision data; the `title` profile selects
  11 segments. Invalid access in `DynaPoly_AddBgActorToLookup` fixed.
  Cutscenes distinguish words, halfwords, and single-byte fields.
  `tools/check_ps2_cutscenes.py` verifies camera, positions, sequences,
  timing, text, transitions, and rejection of truncated data.
- `osContInit` preserves already-open ports and returns a bitmask.
  Controller reading and querying signal the SI completion expected by PadMgr.
- `tools/run_ps2_game.ps1` starts the test with an isolated configuration and preserves
  the previous log; it replaces only this test's process, identified by ELF.
- The renderer's segment table separated from the engine's table: commands
  `G_MW_SEGMENT` write physical RSP bases without corrupting CPU pointers.
  The EE test confirms both independent tables and 120 submitted frames.
  Matrices, printf, queues, timers, and concurrent DMA
  also passed again in `build/PS2/pcsx2-smoke.log`.

Pending: visual validation (window capture timed out), interactive
control and file selection, conversion of the remaining scenes/objects,
game saving, and audio DSP processing. Title frame progression
does not replace these checks.

## Progress with actual tools — 2026-09-25 (historical)

This section supersedes the conclusions of the historical review with mock
headers below. On this date, the full game still did not link;
the September 26 progress is documented above.

- ps2dev/GCC 15.2.0 installed in Ubuntu WSL; calls incompatible
  with actual ps2sdk/gsKit, compiler flags, and the SDK link script corrected.
- Recovered audio, libc, libc64, libu64, buffers, and C matrix and
  trigonometry routines from the upstream revision pinned in `UPSTREAM.md`.
- Extracted the local US NTSC 1.2 ROM. DMA/audio tables generated in native
  format and with actual VROM addresses, without replacing unknown resources with zero.
- Heap/framebuffer initialization adapted to the EE address space;
  N64 code is no longer copied over native code, nor is BSS cleared from boot.
- Actor, state, and effect tables use resident code. Player, pause,
  and map markers no longer try to load their instructions from the N64 ROM.
  Reviewing static data reinitialization when changing overlays is pending,
  as is measuring total RAM usage with all resources integrated.
- libultra matrices fixed to use the same 16-bit halfwords
  as `Matrix_MtxFToMtx` and the renderer in little-endian.
- Actual EE timer with 64-bit wraparound, timer registration
  without requiring initialized memory, and event submission outside the critical section.
- `_Printf` through newlib and nested interrupt restoration added.
  The game's assert symbol separated from newlib's, which has a different signature.
- Removed the mixing of normal/Master Quest sources and N64/iQue
  debugging implementations from the PS2 build.
- Conversion of 110 texture includes: the numeric value of each u64
  is reversed to preserve the original bytes when compiling them for EE. This
  transformation is not applied to vertices or matrices.
- ROM reads serialized between threads using a semaphore. Partial
  reads are completed, and accesses outside the file are rejected; zero
  padding is no longer presented as a successful transfer. Scalar PI reads
  convert big-endian and accept the cartridge's KSEG1 alias.

### Verification

PCSX2 2.8.0, with an isolated configuration and the existing BIOS, ran the
diagnostic ELF compiled with ps2dev. The log confirmed:

```text
[PS2 MATH TEST] PASS: native matrix layout and trigonometry (0)
[PS2 PRINTF TEST] PASS: game varargs and callback (0)
[PS2 OS TEST] PASS: queue ABI, FIFO/full/empty, timer, monotonic clock
[PS2 ROM TEST] PASS: file DMA, boundaries and endian (0)
[PS2 ROM THREAD TEST] PASS: two concurrent readers (0)
[PS2 SMOKE] PASS: 120 frames submitted
```

The OS test also checks nested interrupt restoration.
`tools/check_ps2_texture_words.py` verified that the 110 textures preserve
exactly their 212352 original bytes within an object compiled for EE.
Rendering has not been visually validated, and physical hardware has not been tested.
The latest pass compiled 709 objects and reached the link stage but failed
with 8633 unresolved symbols and no duplicate definitions. The local log
is `build/PS2/game-native-final.log`; the deduplicated list is in
`build/PS2/unresolved-symbols.txt`. Compiling the C units does not mean having
a functional game ELF.

### Remaining work

Integrate symbols and resource conversion (scenes, collision, skeletons,
animations, and display lists); replace N64 microcode/RCP,
fault, and JPEG dependencies; implement audio DSP and soundfont conversion;
validate startup, title screen, controls, and saving with the actual game.
The ROM read path still does not provide all resources in native
format. Globally enabling an endian option does not resolve the mix of
CPU-created structures and data read from ROM.

## How this was verified

Reading the code is not enough to know whether it compiles. For this
particular port:

1. A set of mock headers was created to imitate the signatures of
   ps2sdk/gsKit (`kernel.h`, `libpad.h`, `libmc.h`, `gsKit.h`...) so
   the PS2 layer could be run through the compiler without the actual
   toolchain installed.
2. It was compiled in **32-bit mode** (`-m32`), because EE is 32-bit;
   in 64-bit mode, all struct-size `_Static_assert` checks give false
   negatives.
3. The **actual game structs** (`include/ultra64/*.h`
   with `-D_LANGUAGE_C=1 -DNON_MATCHING=1`) were compiled, and every signature
   implemented by the PS2 layer was compared against the actual prototype. This found several
   mistakes made in the port (see below) that a simple reading would not have caught.
4. All **637 game source files** (`src/code`,
   `src/boot`, `src/overlays`) were compiled with the final Makefile flags.

**Result of the latest pass:** 0 code errors in all 637
game files. Those that do not compile (462) fail only because of
`assets/... No such file or directory` — unextracted assets, which are
a prerequisite step of the decomp tools, rather than a code bug.

## Build system

- **Makefile**: previously compiled 145 files; now compiles all 656 actual files.
  `src/overlays` (511 files: all actors, `z_title.c`,
  `z_select.c`, the pause menu) and `src/audio` were entirely absent
  from the build.
- Correctly excluded the 18 `*.inc.c` files (fragments included by
  other `.c` files, rather than compilation units) and `fault_n64.c`
  (conflicts with `fault.c` when `PLATFORM_N64=0`).
- Missing flags that blocked the entire build: `-Iinclude/libc`,
  `-DOOT_VERSION=NTSC_1_0`, `-D_LANGUAGE_C=1`, `-DNON_MATCHING=1`,
  `-DDEBUG_FEATURES=0`, `-DOOT_DEBUG=0`, `BUILD_DATE`/`BUILD_CREATOR`.
- Automatic dependency generation (`-MMD -MP`).

## Startup (`ps2_main.c`, new `ps2_boot.c`)

- It called `Game_Main()`, a function that does not exist anywhere
  in the decomp. It now replicates the actual chain:
  `bootproc() → Idle_ThreadEntry → Main() → Graph_ThreadEntry`.
- `Sched_Init()` was called with 0 arguments; the actual function takes 6
  (`Scheduler*, stack, priority, viModeType, arg4, IrqMgr*`).
- IOP initialization order corrected: IOP reset first,
  then `SIO2MAN → PADMAN`, `MCMAN → MCSERV`, `LIBSD → audsrv.irx`
  (previously, PADMAN was loaded without SIO2MAN, and `audsrv_init()` was called
  without loading its IRX).
- Nobody called `rdp_to_gs_init()` → GS pointer was `NULL`.

## libultra stubs (`src/platform/ps2/libultra_stub/`)

- **`osMessage.c`**: `OSMesgQueue` was oversized (~44 bytes instead of
  the actual 24), corrupting neighboring memory on every
  `osCreateMesgQueue()`. The struct now has the exact size, and the
  semaphores are stored in a side table.
- **`osThread.c`**: the thread stack confused top with base and used
  a fixed size of 4 KB. It now uses its own 32 KB stacks, managed
  separately, without touching the game's `OSThread` struct.
- **`osTimer.c`** (previously broken): timers were registered in an array
  that nobody traversed; there is now an actual linked list, and
  `ps2_timer_tick()` processes it each frame.
- **`osTask.c`**: `osSpTaskStart` never signaled that the graphics
  task had finished → the graphics thread hung forever
  on the first frame. It now fires SP/DP events.
- **`osEvent.c`, `osVi.c`, `osRom.c`, `osMisc.c`** (new, previously
  absent): `osSetEventMesg`/vertical retrace, reading the "ROM" from a
  file via DMA, and rumble connected to `osMotorStart/Stop` (which are
  actually **macros**, rather than functions — a mistake in the port corrected after
  compiling against the actual header).
- **`osContPad.c`**: duplicate port initialization removed
  (`ps2_main.c` and this file opened the same ports with different DMA
  buffers); C-button mapping corrected (Circle/R3 conflicted).

### Mistakes in the port found only by compiling against the actual headers

- `osAppNMIBuffer` was declared as `void*`; the actual declaration is
  `s32 osAppNMIBuffer[0x10]` (an array, rather than a pointer).
- `osMotorStart`/`osMotorStop` are macros that expand to
  `__osMotorAccess()`, rather than functions — implementing them as functions
  would never have linked them.
- `osAfterPreNMI`, `osContStartReadData`, `osContStartQuery`,
  `osContSetCh` return `s32`, rather than `void`.
- `G_MV_VIEWPORT`/`G_MV_L0` used **old** F3DEX values
  (`0x80`/`0x86`) instead of F3DEX2 values (`8`/`10`). `gbi.h` places the
  "F3DEX_GBI_2" comment right in the opposite branch, making it easy
  to get wrong.

## Graphics (`src/platform/ps2/gfx/`)

- **`ps2_segment.h`** (new): translation of segmented addresses
  (`gSegments[]`), previously absent everywhere — any vertex/texture
  pointer in an actual display list was interpreted
  as a literal address.
- **`rdp_to_gs.c`**: `G_VTX` (0x01) collided with `G_SPNOOP` in the
  switch — two `case` labels with the same value do not even compile. Vertex
  loading fell into the "do nothing" branch.
- **`f3dex2_ps2.c`**: the viewport was never configured (it looked for the index
  in the wrong opcode), `G_POPMTX` read the wrong word,
  and geometry mode cleared the upper 8 bits through an incomplete mask.
- **`rdp_color.c`**: `G_SETOTHERMODE_H/L` were missing (the ones OoT actually
  uses); two separate files wrote the GS `TEST` register
  independently and overwrote each other.

## Audio and saving

- Symbol collision with the actual decomp: `Audio_Init` and
  `func_800F4C58` already exist in `src/audio/game/general.c` with a different
  signature. Everything renamed to `PS2Audio_*`.
- Mixer resampling lost the carry (`&= 0xFFFF` discarded
  bits in high-pitched sounds).
- Saving rewritten with the actual `libmc` API (`mcOpen`/`mcSync`...);
  it previously used `open`/`read`/`write`, which do not apply to `mc0:` on PS2, and
  nobody called anything in this file.

## Linker (`linker/linkfile.ps2`, new `tools/gen_segment_syms.py`)

- It defined none of the ~165 `_xxxSegmentStart/End/...` symbols
  referenced by the game. `linkfile.ps2` manually defines those needed
  to boot; `gen_segment_syms.py` automatically generates the
  rest by reading the code (`linker/segments.ld`).

## Asset endianness (`tools/swap_assets.py`, new)

The easiest finding to overlook in the entire port: there are TWO kinds
of display list — those in the **assets** (extracted from ROM,
big-endian) and those the game **constructs at runtime**
(compiled for PS2, little-endian). No read mode handles
both correctly. The solution is to convert assets to little-endian once
when extracting them; `swap_assets.py` does so by data type (swapping
`.dlist`/`.mtx` in 16/32-bit words, preserving the 4 color bytes
of each `.vtx`, and leaving 1-byte textures such as I4/I8 entirely untouched,
as swapping them would break them).

## What is NOT resolved (and why it is more than another simple patch)

- **RDP color combiner** (`G_SETCOMBINE`): determines how
  texture/color/shading are blended on each surface. Without this, almost
  everything has the wrong color. It requires translating the N64
  combiner machine to GS TFX modes and iterating while looking at the results
  on screen.
- **Actual audio synthesis** (music, rather than just effects): the decomp's
  audio engine (`src/audio/`) has not even been copied into the port. Porting
  N64 MIDI-like sequence playback is an entire subsystem.
- **Overlay relocation**: actors are linked statically
  rather than being loaded at runtime at a variable address, which is how
  the N64 overlay system actually works.

These three points are months of work, rather than isolated bugs, and require
seeing them run on the console/emulator to iterate — they cannot be completed
"blindly."
