#!/usr/bin/env python3
"""Compare real EE cutscene initializers with the independent ROM field layout.

The control object keeps N64 command packing. Its numeric words reconstruct
the canonical big-endian stream, which the upstream parser describes by field.
No ROM or save file is modified.
"""
import argparse
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

from ps2_cutscene_layout import cutscene_groups

project = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--upstream', type=Path,
                    default=Path.home() / 'oot-ps2-tools/oot-upstream')
parser.add_argument('--include-root', type=Path, default=project / 'include')
parser.add_argument('--elf', type=Path, help='Also check the arrays linked in the game ELF')
args = parser.parse_args()
upstream = args.upstream.resolve()
sys.path.insert(0, str(upstream))
from tools import csdis

ps2dev = Path(os.environ.get('PS2DEV', Path.home() / 'oot-ps2-tools/ps2dev'))
prefix = str(ps2dev / 'ee/bin/mips64r5900el-ps2-elf-')
testdir = project / 'build/PS2/compiled-cutscene-test'
testdir.mkdir(parents=True, exist_ok=True)
tree_source = project / 'src/overlays/actors/ovl_Bg_Treemouth/z_bg_treemouth_cutscene_data.c'
source = testdir / 'cutscenes.c'
source.write_text('#include "' + tree_source.as_posix() + '"\n' + '''
CutsceneData gDekuTreeLayoutFixtureCs[] = {
    CS_HEADER(12, 600),
    CS_CAM_EYE_SPLINE(10, 100),
        CS_CAM_POINT(CS_CAM_CONTINUE, -7, 32, CS_FLOAT(0x42360000, 45.5f), -123, 456, -789, 0),
        CS_CAM_POINT(CS_CAM_CONTINUE, -7, 32, CS_FLOAT(0x42360000, 45.5f), -123, 456, -789, 0),
        CS_CAM_POINT(CS_CAM_CONTINUE, -7, 32, CS_FLOAT(0x42360000, 45.5f), -123, 456, -789, 0),
        CS_CAM_POINT(CS_CAM_STOP, -7, 32, CS_FLOAT(0x42360000, 45.5f), -123, 456, -789, 0),
    CS_LIGHT_SETTING_LIST(1), CS_LIGHT_SETTING(17, 10, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
    CS_RUMBLE_CONTROLLER_LIST(1), CS_RUMBLE_CONTROLLER(0, 10, 100, 0xAB, 0xCD, 0xEF, 0, 0),
    CS_PLAYER_CUE_LIST(1),
        CS_PLAYER_CUE(2, 10, 40, 0x8000, 5, 0xFFFF, -10, 20, -30, 40, -50, 60,
                      CS_FLOAT(0, 0.0f), CS_FLOAT(0, 0.0f), CS_FLOAT(0, 0.0f)),
    CS_TEXT_LIST(1), CS_TEXT(0x1234, 10, 20, 1, 0x5678, 0x9ABC),
    CS_TRANSITION(3, 50, 80),
    CS_START_SEQ_LIST(1), CS_START_SEQ(0x61, 10, 100, 0, 0, 0, 0, 0, 0, 0, 0),
    CS_STOP_SEQ_LIST(1), CS_STOP_SEQ(0x61, 100, 101, 0, 0, 0, 0, 0, 0, 0, 0),
    CS_FADE_OUT_SEQ_LIST(1), CS_FADE_OUT_SEQ(1, 20, 30, 0, 0, 0, 0, 0, 0, 0, 0),
    CS_TIME_LIST(1), CS_TIME(0, 20, 30, 18, 45),
    CS_DESTINATION(2, 100, 101),
    CS_MISC_LIST(1), CS_MISC(2, 10, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
    CS_END_OF_SCRIPT(),
};
u32 gSceneNativeByteFixture[] = { CMD_BBBB(-1, -7, 0xAA, 0xCD) };
''')

def compile_data(label, native):
    obj = testdir / f'{label}.o'
    binary = testdir / f'{label}.bin'
    subprocess.run([prefix + 'gcc', '-G0', '-O2', '-fno-toplevel-reorder',
                    '-std=gnu11', '-D_LANGUAGE_C=1', '-DF3DEX_GBI_2',
                    '-DOOT_VERSION=NTSC_1_2', f'-DPLATFORM_PS2={native}',
                    '-DPLATFORM_N64=0', '-DPLATFORM_GC=0', '-DPLATFORM_IQUE=0',
                    '-I' + str(args.include_root.resolve()),
                    '-I' + str(project / 'include'), '-I' + str(project / 'include/libc'),
                    '-c', str(source), '-o', str(obj)], check=True)
    subprocess.run([prefix + 'objcopy', '--dump-section', f'.data={binary}', str(obj)], check=True)
    symbols = subprocess.check_output([prefix + 'nm', '-S', str(obj)], text=True)
    arrays = {}
    data = binary.read_bytes()
    address = int(re.search(r'^([0-9a-fA-F]+) [0-9a-fA-F]+ [Dd] gSceneNativeByteFixture$',
                            symbols, re.MULTILINE)[1], 16)
    scene_bytes = data[address:address + 4]
    assert (scene_bytes if native else scene_bytes[::-1]) == bytes((255, 249, 170, 205)), \
        'Compiled scene byte command uses wrong memory order'
    for address, size, name in re.findall(r'^([0-9a-fA-F]+) ([0-9a-fA-F]+) [Dd] (gDekuTree\w+Cs)$',
                                         symbols, re.MULTILINE):
        start, length = int(address, 16), int(size, 16)
        assert length % 4 == 0 and start + length <= len(data)
        arrays[name] = data[start:start + length]
    assert len(arrays) == 5, f'Expected four Deku scripts and the mixed-layout fixture: {arrays.keys()}'
    return arrays

control = compile_data('n64-packing', 0)
native = compile_data('ee-packing', 1)
total_bytes = 0
camera_lists = 0
for name, payload in control.items():
    original = b''.join(struct.pack('>I', word[0]) for word in struct.iter_unpack('<I', payload))
    expected = bytearray(original)
    for start, end, width in cutscene_groups(original, 0, len(original), csdis):
        for offset in range(start, end, width):
            expected[offset:offset + width] = original[offset:offset + width][::-1]
    assert native[name] == expected, f'EE initializer differs from native field layout: {name}'
    total_bytes += len(expected)
    # Traverse the EE stream independently with native structure offsets.
    cursor = 8
    for _ in range(struct.unpack_from('<I', expected)[0]):
        opcode = struct.unpack_from('<i', expected, cursor)[0]
        cursor += 4
        if opcode in (1, 2, 5, 6, 7, 8):
            start_frame, end_frame = struct.unpack_from('<HH', expected, cursor + 2)
            assert start_frame < end_frame <= 3000
            cursor += 8
            points = 0
            while True:
                assert cursor + 16 <= len(expected), f'Missing camera stop: {name}'
                flag, roll, duration, fov, x, y, z, padding = struct.unpack_from('<bbHf4h', expected, cursor)
                assert flag in (0, -1) and 0.0 < fov < 180.0
                assert duration <= 1000 and all(-10000 < v < 10000 for v in (x, y, z))
                cursor += 16
                points += 1
                if flag == -1:
                    break
            assert points >= 4
            camera_lists += 1
        else:
            count = struct.unpack_from('<I', expected, cursor)[0]
            cursor += 4
            size = 8 if opcode in (45, 1000) else 12 if opcode in (9, 19, 140) else 48
            assert count <= 12
            for _ in range(count):
                if opcode in (4, 86, 87):
                    unused, sequence, start_frame, end_frame = struct.unpack_from('<BBHH', expected, cursor)
                    assert unused == 0 and sequence > 0 and start_frame < end_frame <= 3000
                elif opcode != 21:
                    value, start_frame, end_frame = struct.unpack_from('<3H', expected, cursor)
                    assert start_frame < end_frame <= 3000
                cursor += size
        assert cursor <= len(expected)
    assert expected[cursor:] == struct.pack('<iI', -1, 0), f'Bad script terminator: {name}'
if args.elf:
    elf = args.elf.resolve()
    binary = testdir / 'game-data.bin'
    subprocess.run([prefix + 'objcopy', '--dump-section', f'.data={binary}', str(elf)], check=True)
    sections = subprocess.check_output([prefix + 'objdump', '-h', str(elf)], text=True)
    data_base = int(re.search(r'^\s*\d+\s+\.data\s+[0-9a-fA-F]+\s+([0-9a-fA-F]+)',
                             sections, re.MULTILINE)[1], 16)
    symbols = subprocess.check_output([prefix + 'nm', '-S', str(elf)], text=True)
    linked = binary.read_bytes()
    checked = 0
    for address, size, name in re.findall(r'^([0-9a-fA-F]+) ([0-9a-fA-F]+) [Dd] (gDekuTree\w+Cs)$',
                                         symbols, re.MULTILINE):
        if name not in native:
            continue
        start, length = int(address, 16) - data_base, int(size, 16)
        assert 0 <= start <= start + length <= len(linked)
        assert linked[start:start + length] == native[name], f'Stale linked cutscene: {name}'
        checked += 1
    assert checked == 4, f'Missing Deku scripts in game ELF: {checked}'
    print('PASS: the four corrected scripts are present in the linked game ELF.')
print(f'PASS: four compiled EE Deku scripts plus command fixture, {camera_lists} bounded camera lists, '
      f'{total_bytes} bytes match independent mixed-width fields, including sequence byte IDs.')
