#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/ps2_env.sh
upstream="${OOT_UPSTREAM:-$HOME/oot-ps2-tools/oot-upstream}"
mkdir -p build/PS2/smoke
cc="$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc"
flags=(-D_EE -DPLATFORM_PS2=1 -G0 -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter
       "-DPS2_SMOKE_COLOR_PSM=${PS2_SMOKE_COLOR_PSM:-GS_PSM_CT16S}"
       -fno-strict-aliasing '-DPS2_ROM_TEST_PATH="host:ps2-rom-smoke.bin"' -Iinclude/ps2 -I"$PS2SDK/ee/include"
       -I"$PS2SDK/common/include" -I"$PS2SDK/ports/include" -I"$GSKIT/include")
objects=()
for src in tools/ps2_audio_driver_smoke.c src/platform/ps2/audio/ps2_audio_driver.c tools/ps2_jpeg_smoke.c tools/ps2_smoke.c tools/ps2_os_smoke.c tools/ps2_rom_threads_smoke.c tools/ps2_renderer_smoke.c tools/ps2_audio_smoke.c tools/ps2_sram_smoke.c src/platform/ps2/io/ps2_sram.c src/platform/ps2/gfx/*.c \
    src/platform/ps2/audio/ps2_rsp_audio.c src/platform/ps2/audio/rsp_hle/*.c \
    src/platform/ps2/libultra_stub/osMessage.c src/platform/ps2/libultra_stub/osTimer.c \
    src/platform/ps2/libultra_stub/osInterrupt.c src/platform/ps2/libultra_stub/osPrintf.c \
    src/platform/ps2/libultra_stub/osRom.c; do
    obj="build/PS2/smoke/$(basename "${src%.c}").o"
    "$cc" "${flags[@]}" -c "$src" -o "$obj"
    objects+=("$obj")
done
for src in tools/ps2_font_smoke.c src/code/z_kanfont.c src/code/kanji_tex.c tools/ps2_queue_heap_smoke.c src/libc64/__osMalloc_gc.c tools/ps2_math_smoke.c tools/ps2_printf_smoke.c tools/ps2_rom_smoke.c tools/ps2_audio_heap_smoke.c src/code/sys_matrix.c src/audio/internal/heap.c src/libultra/gu/mtxutil.c src/libultra/gu/sins.c src/libultra/gu/coss.c; do
    obj="build/PS2/smoke/$(basename "${src%.c}").o"
    "$cc" -Iinclude -Iinclude/libc "${flags[@]}" -D_LANGUAGE_C=1 -DF3DEX_GBI_2 -DNON_MATCHING=1 \
        -ffunction-sections -fdata-sections -DPLATFORM_N64=0 -DPLATFORM_GC=0 -DPLATFORM_IQUE=0 \
        -DOOT_VERSION=NTSC_1_2 -DMML_VERSION=MML_VERSION_OOT \
        -Iinclude -Iinclude/libc -I"$upstream" -I"$upstream/extracted/ntsc-1.2" \
        -I"$upstream/build/ntsc-1.2" -c "$src" -o "$obj"
    objects+=("$obj")
done
"$cc" -G0 -T"$PS2SDK/ee/startup/linkfile" -Wl,-zmax-page-size=128 \
    -Wl,--gc-sections -o build/PS2/oot_ps2_smoke.elf "${objects[@]}" \
    -L"$PS2SDK/ee/lib" -L"$PS2SDK/ports/lib" -L"$GSKIT/lib" -lgskit -ldmakit -ljpeg -lm \
    -Wl,-Map=build/PS2/oot_ps2_smoke.map
"$PS2DEV/ee/bin/mips64r5900el-ps2-elf-readelf" -h build/PS2/oot_ps2_smoke.elf
python3 -c 'from pathlib import Path; Path("build/PS2/ps2-rom-smoke.bin").write_bytes(bytes(range(256)) * 16)'
