#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/ps2_env.sh
upstream="${OOT_UPSTREAM:-$HOME/oot-ps2-tools/oot-upstream}"
python3 tools/generate_ps2_room_smoke.py --image build/PS2-ntsc12/oot-full-next.z64 \
    --output build/PS2-ntsc12/generated/room_smoke_cases.h "$@"
make PLATFORM=PS2 OOT_VERSION=NTSC_1_2 BUILD_DIR=build/PS2-ntsc12 \
    OOT_UPSTREAM="$upstream" PS2_ROOM_SMOKE=1 TARGET=oot_ps2_room_smoke.elf -j2
mkdir -p build/PS2-room-test
cp build/PS2-ntsc12/oot_ps2_room_smoke.elf build/PS2-room-test/
cp build/PS2-ntsc12/oot-full-next.z64 build/PS2-room-test/oot.z64
cp build/PS2-ntsc12/generated/room_smoke_cases.json build/PS2-room-test/cases.json
