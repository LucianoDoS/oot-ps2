#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/ps2_env.sh
make PLATFORM=PS2 OOT_VERSION=NTSC_1_2 BUILD_DIR=build/PS2-ntsc12 \
    OOT_UPSTREAM="${OOT_UPSTREAM:-$HOME/oot-ps2-tools/oot-upstream}" \
    PS2_USB_BOOT=1 PS2_RUNTIME_DIAGNOSTIC=1 PS2_RUNTIME_AUTOMATIC="${PS2_LOCALE_GAME_AUTO:-0}" PS2_RUNTIME_GFX_SPARSE="${PS2_LOCALE_GAME_SPARSE:-0}" PS2_LOCALE_GAME_SMOKE=1 \
    TARGET="${PS2_LOCALE_GAME_TARGET:-oot_ps2_usb_locale_test.elf}" -j2
