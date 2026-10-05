#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/ps2_env.sh
upstream="${OOT_UPSTREAM:-$HOME/oot-ps2-tools/oot-upstream}"
make PLATFORM=PS2 OOT_VERSION=NTSC_1_2 BUILD_DIR=build/PS2-ntsc12 OOT_UPSTREAM="$upstream" \
    PS2_USB_BOOT=1 PS2_RUNTIME_DIAGNOSTIC=1 PS2_FLOOR_DIAGNOSTIC=1 TARGET=oot_ps2_usb_floor.elf -j2
