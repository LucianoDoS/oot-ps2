#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/ps2_env.sh
upstream="${OOT_UPSTREAM:-$HOME/oot-ps2-tools/oot-upstream}"
build=build/PS2-ntsc12
python3 tools/generate_ps2_usb_data.py --image "$build/oot-full-next.z64" --output "$build/generated/usb_data_expected.h"
make PLATFORM=PS2 OOT_VERSION=NTSC_1_2 BUILD_DIR="$build" OOT_UPSTREAM="$upstream" \
    PS2_USB_BOOT=1 TARGET=oot_ps2_usb.elf -j2
"$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc" -D_EE -G0 -O2 -std=gnu11 -Wall -Wextra \
    -Iinclude/ps2 -I"$build/generated" -I"$PS2SDK/ee/include" -I"$PS2SDK/common/include" \
    -T"$PS2SDK/ee/startup/linkfile" -Wl,-zmax-page-size=128 \
    tools/ps2_usb_diagnostic.c src/platform/ps2/ps2_usb_boot.c \
    tools/ps2_cache_smoke.c src/platform/ps2/ps2_cache.c \
    "$build/generated/usbd_irx.c" "$build/generated/usbhdfsd_irx.c" \
    -L"$PS2SDK/ee/lib" -lmc -lpatches -ldebug -o "$build/oot_ps2_usb_diagnostic.elf"
if [[ "${1:-}" == --host-test ]]; then
    make PLATFORM=PS2 OOT_VERSION=NTSC_1_2 BUILD_DIR="$build" OOT_UPSTREAM="$upstream" \
        PS2_USB_BOOT=1 PS2_USB_HOST_TEST=1 TARGET=oot_ps2_usb_game_host_test.elf -j2
    "$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc" -D_EE -G0 -O2 -std=gnu11 -Wall -Wextra \
        -DPS2_USB_HOST_TEST=1 -Iinclude/ps2 -I"$build/generated" \
        -I"$PS2SDK/ee/include" -I"$PS2SDK/common/include" \
        -T"$PS2SDK/ee/startup/linkfile" -Wl,-zmax-page-size=128 \
        tools/ps2_usb_diagnostic.c src/platform/ps2/ps2_usb_boot.c \
        tools/ps2_cache_smoke.c src/platform/ps2/ps2_cache.c \
        -L"$PS2SDK/ee/lib" -lmc -lpatches -ldebug -o "$build/oot_ps2_usb_host_test.elf"
fi
