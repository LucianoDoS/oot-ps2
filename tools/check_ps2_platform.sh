#!/usr/bin/env bash
# Compile every production platform unit with the real PS2 SDK.
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/ps2_env.sh
mkdir -p build/PS2
mapfile -t objects < <(find src/platform/ps2 -name '*.c' ! -name ps2_seq.c ! -name ps2_sfx.c | sort | sed 's|^|build/PS2/|;s|\.c$|.o|')
make -k -j4 PREFIX="$PS2DEV/ee/bin/mips64r5900el-ps2-elf-" "${objects[@]}"
