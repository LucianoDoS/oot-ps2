#!/usr/bin/env python3
"""Generate texture include files referenced directly by game translation units."""
import os
import argparse
from pathlib import Path
import re
import subprocess

project = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=project / 'build/PS2-ntsc12/generated')
args = parser.parse_args()
upstream = Path(os.environ.get('OOT_UPSTREAM', Path.home() / 'oot-ps2-tools/oot-upstream'))
version = 'ntsc-1.2'
roots = [project, upstream, upstream / 'extracted' / version, upstream / 'build' / version]
targets = set()
textures = set()
pending = list((project / 'src').rglob('*.c'))
seen = set()
while pending:
    source = pending.pop()
    if source in seen:
        continue
    seen.add(source)
    for name in re.findall(r'#include\s+"(assets/[^"\n]+\.c)"', source.read_text()):
        png = name[:-6] + '.png' if name.endswith('.inc.c') else ''
        if png and any((root / png).is_file() for root in roots):
            textures.add(name)
        existing = next((root / name for root in roots if (root / name).is_file()), None)
        if existing:
            pending.append(existing)
            continue
        if not name.endswith('.inc.c'):
            continue
        png = name[:-6] + '.png'
        if any((root / png).is_file() for root in roots):
            targets.add(f'build/{version}/{name}')
if targets:
    ps2dev = Path(os.environ.get('PS2DEV', Path.home() / 'oot-ps2-tools/ps2dev'))
    subprocess.run(['make', '-C', str(upstream), f'VERSION={version}',
                    f'MIPS_BINUTILS_PREFIX={ps2dev}/ee/bin/mips64r5900el-ps2-elf-',
                    *sorted(targets)], check=True)
print(f'Generated {len(targets)} required texture includes.')

# Texture payloads remain N64 byte streams (the GS converter reads BE pixels).
# GCC stores a u64 initializer in EE byte order, so reverse its numeric value
# to preserve the original eight bytes. Vertices/matrices are native structures
# and must NOT go through this transformation.
for name in sorted(textures):
    source = upstream / 'build' / version / name
    text = source.read_text()
    converted, count = re.subn(r'0x[0-9A-Fa-f]{16}\b',
        lambda match: '0x' + bytes.fromhex(match[0][2:])[::-1].hex().upper(), text)
    if not count:
        raise RuntimeError(f'Expected packed u64 texture words in {source}')
    dest = args.output / name
    dest.parent.mkdir(parents=True, exist_ok=True)
    if not dest.exists() or dest.read_text() != converted:
        dest.write_text(converted)
print(f'Prepared {len(textures)} native u64 texture includes preserving ROM byte order.')
