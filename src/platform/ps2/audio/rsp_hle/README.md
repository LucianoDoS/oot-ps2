# Audio DSP source provenance

The files in this directory come from `mupen64plus/mupen64plus-rsp-hle`,
commit `8a7a472a7172eb2c8725b305eae26818ed7b51a2`.
Original copyright notices and GPL-2.0-or-later terms are preserved.
See LICENSES for the upstream license text.

Only the audio synthesis routines are called. The game runs as native EE code;
this does not import an N64 CPU, graphics renderer or emulator frontend.

PS2 changes select consecutive native sample layout in common.h, use integer
address arithmetic for real EE pointers in memory.h, and route audio DMA
through those helpers in alist.c. The native scheduler owns task completion.
The OoT opcode table also implements signed eight-bit PCM (opcode 23), with
fixtures verifying its history and output samples on the EE.

The filter uses consecutive native samples and taps on PS2. Upstream's
unrolled filter directly indexes word-swizzled pairs; an impulse spanning
two synthesis blocks verifies tap order and preservation of raw history.
