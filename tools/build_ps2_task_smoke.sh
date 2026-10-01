#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/ps2_env.sh
mkdir -p build/PS2
"$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc" -D_EE -G0 -O2 -std=gnu11 -Wall -Wextra \
    -Iinclude/ps2 -I"$PS2SDK/ee/include" -I"$PS2SDK/common/include" -I"$GSKIT/include" \
    -T"$PS2SDK/ee/startup/linkfile" -Wl,-zmax-page-size=128 \
    tools/ps2_task_smoke.c src/platform/ps2/libultra_stub/osTask.c \
    src/platform/ps2/libultra_stub/osEvent.c src/platform/ps2/libultra_stub/osMessage.c \
    -L"$PS2SDK/ee/lib" -o build/PS2/oot_ps2_task_smoke.elf
