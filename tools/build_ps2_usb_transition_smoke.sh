#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/ps2_env.sh
make PLATFORM=PS2 OOT_VERSION=NTSC_1_2 BUILD_DIR=build/PS2-ntsc12 \
    OOT_UPSTREAM="${OOT_UPSTREAM:-$HOME/oot-ps2-tools/oot-upstream}" \
    PS2_USB_BOOT=1 PS2_RUNTIME_DIAGNOSTIC=1 PS2_USB_TRANSITION_SMOKE=1 \
    TARGET=oot_ps2_usb_transition_test.elf -j2
