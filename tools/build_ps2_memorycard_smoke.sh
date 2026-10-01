#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/ps2_env.sh
mkdir -p build/PS2
"$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc" -D_EE -G0 -O2 -std=gnu11 -Wall -Wextra \
    '-DPS2_SAVE_DIR="OOT_PS2_TEST"' -Iinclude/ps2 -I"$PS2SDK/ee/include" -I"$PS2SDK/common/include" \
    -T"$PS2SDK/ee/startup/linkfile" -Wl,-zmax-page-size=128 \
    tools/ps2_memorycard_smoke.c src/platform/ps2/io/ps2_controller.c \
    -L"$PS2SDK/ee/lib" -lmc -o build/PS2/oot_ps2_memorycard_smoke.elf
