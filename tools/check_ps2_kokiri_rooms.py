#!/usr/bin/env python3
"""Check native scene/room structures against the private upstream ROM.

Run with the upstream virtualenv, which provides version_config's dependencies.
The original image is read only; this test never changes a save or data image.
"""
import argparse
import json
from pathlib import Path
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--upstream', type=Path, required=True)
parser.add_argument('--image', type=Path, required=True)
parser.add_argument('--previous', type=Path)
args = parser.parse_args()
upstream = args.upstream.resolve()
sys.path.insert(0, str(upstream))
from tools import version_config, dmadata

manifest = json.loads(args.image.with_suffix('.json').read_text())
config = version_config.load_version_config(manifest['version'])
original = (upstream / 'baseroms' / manifest['version'] / 'baserom-decompressed.z64').read_bytes()
native = args.image.read_bytes()
entries = dict(zip(config.dmadata_segments, dmadata.read_dmadata(memoryview(original), config.dmadata_start), strict=True))
names = ['spot04_scene'] + [f'spot04_room_{room}' for room in range(3)]
counts = {}
for name in names:
    assert name in manifest['segments'], f'Missing native Kokiri segment: {name}'
    entry = entries[name]
    counts[name] = 0
    for resource in manifest['segments'][name]:
        kind = resource['kind']
        start = entry.vrom_start + resource['offset']
        end = start + resource['size']
        expected = bytearray(original[start:end])
        if kind == 'SceneCommandsResource':
            assert len(expected) % 8 == 0
            for command in range(0, len(expected), 8):
                if expected[command] not in (0x05, 0x10, 0x11, 0x12, 0x15, 0x16):
                    expected[command+4:command+8] = expected[command+4:command+8][::-1]
        elif kind in ('ObjectListResource', 'ActorEntryListResource'):
            for offset in range(0, len(expected), 2):
                expected[offset:offset+2] = expected[offset:offset+2][::-1]
        elif kind in ('TextureResource', 'TLUTResource', 'TextureSplitTlutResource'):
            pass
        elif kind == 'RoomListResource':
            assert len(expected) == 3*8
            for room in range(3):
                room_entry = entries[f'spot04_room_{room}']
                actual_start = int.from_bytes(native[start+room*8:start+room*8+4], 'little')
                actual_end = int.from_bytes(native[start+room*8+4:start+room*8+8], 'little')
                assert (actual_start, actual_end) == (room_entry.vrom_start, room_entry.vrom_end)
            continue
        else:
            continue
        assert native[start:end] == expected, f'Layout or texture mismatch: {name}/{resource["symbol"]}'
        counts[name] += 1
    assert counts[name] >= 3, f'Insufficient scene/room layout coverage: {name}'

preserved = 0
if args.previous:
    previous_manifest = json.loads(args.previous.with_suffix('.json').read_text())
    previous = args.previous.read_bytes()
    for name in previous_manifest['segments']:
        entry = entries[name]
        assert previous[entry.vrom_start:entry.vrom_end] == native[entry.vrom_start:entry.vrom_end], f'Previous native segment changed: {name}'
        preserved += 1
print(f'PASS: Kokiri scene + 3 rooms, native command/object/actor lists and unchanged textures: {counts}; {preserved} previous segments preserved.')
