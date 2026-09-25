#!/usr/bin/env bash
# Real game build with the locally extracted US 1.2 assets, in separate objects.
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/ps2_env.sh
upstream="${OOT_UPSTREAM:-$HOME/oot-ps2-tools/oot-upstream}"
test -d "$upstream/extracted/ntsc-1.2" || { echo 'Extract the US 1.2 ROM first.' >&2; exit 1; }
python3 tools/prepare_ps2_includes.py --output build/PS2-ntsc12/generated
exec make -k -j2 PLATFORM=PS2 OOT_VERSION=NTSC_1_2 \
    BUILD_DIR=build/PS2-ntsc12 OOT_UPSTREAM="$upstream" OOT_ASSET_VERSION=ntsc-1.2 "$@"
