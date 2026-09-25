#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/ps2_env.sh
mkdir -p build/PS2/smoke
cc="$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc"
flags=(-D_EE -DPLATFORM_PS2=1 -G0 -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter
       -fno-strict-aliasing -Iinclude/ps2 -I"$PS2SDK/ee/include"
       -I"$PS2SDK/common/include" -I"$GSKIT/include")
objects=()
for src in tools/ps2_smoke.c tools/ps2_os_smoke.c src/platform/ps2/gfx/*.c \
    src/platform/ps2/libultra_stub/osMessage.c src/platform/ps2/libultra_stub/osTimer.c \
    src/platform/ps2/libultra_stub/osInterrupt.c src/platform/ps2/libultra_stub/osPrintf.c; do
    obj="build/PS2/smoke/$(basename "${src%.c}").o"
    "$cc" "${flags[@]}" -c "$src" -o "$obj"
    objects+=("$obj")
done
for src in tools/ps2_math_smoke.c tools/ps2_printf_smoke.c src/libultra/gu/mtxutil.c src/libultra/gu/sins.c src/libultra/gu/coss.c; do
    obj="build/PS2/smoke/$(basename "${src%.c}").o"
    "$cc" "${flags[@]}" -D_LANGUAGE_C=1 -DF3DEX_GBI_2 -DNON_MATCHING=1 \
        -Iinclude -Iinclude/libc -c "$src" -o "$obj"
    objects+=("$obj")
done
"$cc" -G0 -T"$PS2SDK/ee/startup/linkfile" -Wl,-zmax-page-size=128 \
    -o build/PS2/oot_ps2_smoke.elf "${objects[@]}" \
    -L"$PS2SDK/ee/lib" -L"$GSKIT/lib" -lgskit -ldmakit -lm \
    -Wl,-Map=build/PS2/oot_ps2_smoke.map
"$PS2DEV/ee/bin/mips64r5900el-ps2-elf-readelf" -h build/PS2/oot_ps2_smoke.elf
