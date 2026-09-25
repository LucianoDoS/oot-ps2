# Source provenance

The initial port was supplied in `Downloads/oot-ps2-corregido` and preserved
unchanged in the first Git commit of this workspace.

The missing `src/audio`, `src/libc`, `src/libc64`, and `src/libu64` directories
were restored from https://github.com/zeldaret/oot at commit
`1bef952ff61a6dd1945c7887c1babd94efe95f72` (2026-09-24).
They are source imports, not a claim that audio or overlays already work on PS2.

`src/buffers` and the portable C matrix/trigonometry routines in `src/libultra/gu`
come from that same commit. N64 kernel sources and assembly are not included.

ROMs and extracted/generated game assets are kept outside Git. The local
working extraction is US NTSC 1.2, with normalized ROM SHA1
`41b3bdc48d98c48529219919015a1af22f5057c2`.

Real SDK used for validation: ps2dev Ubuntu archive, GCC 15.2.0.
Archive SHA256: `00287c79c402e61418811df28faa38f6665b8a755e68cd1e69026b188946c6c1`.
The toolchain lives in `/home/lucia/oot-ps2-tools/ps2dev` inside Ubuntu WSL.
