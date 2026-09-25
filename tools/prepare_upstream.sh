#!/usr/bin/env bash
# Usage: bash tools/prepare_upstream.sh /path/to/your/us-rev-b-rom.n64
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/ps2_env.sh
rom="${1:?Provide the path to your own US revision B (1.2) ROM}"
upstream="${OOT_UPSTREAM:-$HOME/oot-ps2-tools/oot-upstream}"
revision=1bef952ff61a6dd1945c7887c1babd94efe95f72
test -f "$rom"
if [ ! -d "$upstream/.git" ]; then
    mkdir -p "$upstream"
    git -C "$upstream" init
    git -C "$upstream" remote add origin https://github.com/zeldaret/oot.git
    git -C "$upstream" fetch --depth=1 origin "$revision"
    git -C "$upstream" checkout --detach FETCH_HEAD
fi
test "$(git -C "$upstream" rev-parse HEAD)" = "$revision" || {
    echo "Unexpected upstream revision; use a separate checkout at $revision." >&2
    exit 1
}
# Detect the actual byte order; extensions are not reliable.
python3 - "$rom" "$upstream" <<'PY'
import hashlib
from pathlib import Path
import sys
data = bytearray(Path(sys.argv[1]).read_bytes())
if data[:4] == bytes.fromhex('37804012'):
    data[::2], data[1::2] = data[1::2], data[::2]
elif data[:4] == bytes.fromhex('40123780'):
    for i in range(0, len(data), 4):
        data[i:i+4] = data[i:i+4][::-1]
if hashlib.sha1(data).hexdigest() != '41b3bdc48d98c48529219919015a1af22f5057c2':
    raise SystemExit('This setup requires the unmodified US 1.2 ROM.')
destination = Path(sys.argv[2]) / 'baseroms/ntsc-1.2/baserom.z64'
if destination.exists() and destination.read_bytes() != data:
    raise SystemExit('Refusing to overwrite a different base ROM.')
destination.write_bytes(data)
PY
make -C "$upstream" setup VERSION=ntsc-1.2 \
    MIPS_BINUTILS_PREFIX="$PS2DEV/ee/bin/mips64r5900el-ps2-elf-"
OOT_UPSTREAM="$upstream" python3 tools/prepare_ps2_includes.py
