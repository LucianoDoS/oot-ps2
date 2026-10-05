#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/ps2_env.sh
mkdir -p build/PS2
upstream="${OOT_UPSTREAM:-$HOME/oot-ps2-tools/oot-upstream}"
python3 tools/generate_ps2_save_icon.py --upstream "$upstream" --output build/PS2/memorycard-icon
"$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc" -D_EE -G0 -O2 -std=gnu11 -Wall -Wextra \
    -DPS2_HAS_SAVE_ICON=1 -Iinclude/ps2 -I"$PS2SDK/ee/include" -I"$PS2SDK/common/include" \
    -T"$PS2SDK/ee/startup/linkfile" -Wl,-zmax-page-size=128 \
    tools/ps2_save_icon_install.c src/platform/ps2/io/ps2_save_icon.c \
    build/PS2/memorycard-icon/save_icon_data.c -L"$PS2SDK/ee/lib" -lmc \
    -o build/PS2/oot_ps2_save_icon_install.elf
