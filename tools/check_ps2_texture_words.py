#!/usr/bin/env python3
"""Verify texture bytes in a real EE object against upstream packed texture words."""
import os
from pathlib import Path
import re
import struct
import subprocess

project = Path(__file__).resolve().parents[1]
upstream = Path(os.environ.get('OOT_UPSTREAM', Path.home() / 'oot-ps2-tools/oot-upstream'))
ps2dev = Path(os.environ.get('PS2DEV', Path.home() / 'oot-ps2-tools/ps2dev'))
generated = project / 'build/PS2-ntsc12/generated'
paths = sorted(generated.glob('assets/**/*.inc.c'))
if not paths:
    raise SystemExit('Run prepare_ps2_includes.py first.')
testdir = project / 'build/PS2/texture-test'
testdir.mkdir(parents=True, exist_ok=True)
source = testdir / 'textures.c'
lines = []
expected = bytearray()
for i, path in enumerate(paths):
    original = upstream / 'build/ntsc-1.2' / path.relative_to(generated)
    words = re.findall(r'0x[0-9A-Fa-f]{16}\b', original.read_text())
    for word in words:
        expected.extend(struct.pack('>Q', int(word, 16)))
    lines += [f'const unsigned long long texture_{i}[] __attribute__((section(".texture_test"), aligned(8))) = {{',
              f'#include "{path}"', '};']
source.write_text('\n'.join(lines) + '\n')
prefix = str(ps2dev / 'ee/bin/mips64r5900el-ps2-elf-')
obj = testdir / 'textures.o'
binary = testdir / 'textures.bin'
subprocess.run([prefix + 'gcc', '-G0', '-fno-toplevel-reorder', '-c', str(source), '-o', str(obj)], check=True)
subprocess.run([prefix + 'objcopy', '--dump-section', f'.texture_test={binary}', str(obj)], check=True)
actual = binary.read_bytes()
if actual != expected:
    raise SystemExit(f'FAIL: EE texture bytes differ ({len(actual)} vs {len(expected)} bytes).')
print(f'PASS: {len(paths)} textures, {len(actual)} bytes preserved in a real EE object.')
