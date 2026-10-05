#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/ps2_env.sh
upstream="${OOT_UPSTREAM:-$HOME/oot-ps2-tools/oot-upstream}"
python3 tools/generate_ps2_skulltula_pose.py --upstream "$upstream" \
    --output build/PS2-ntsc12/generated/skulltula_pose_cases.h
make PLATFORM=PS2 OOT_VERSION=NTSC_1_2 BUILD_DIR=build/PS2-ntsc12 \
    OOT_UPSTREAM="$upstream" PS2_SKULLTULA_SMOKE=1 TARGET=oot_ps2_skulltula_smoke.elf -j2
mkdir -p build/PS2-skulltula-test
cp build/PS2-ntsc12/oot_ps2_skulltula_smoke.elf build/PS2-skulltula-test/
cp build/PS2-ntsc12/oot-full-next.z64 build/PS2-skulltula-test/oot.z64
