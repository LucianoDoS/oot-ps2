#!/usr/bin/env python3
"""Check Deku Tree data against native scene, collision and animation layouts.

Reads private images only. The explicit formats below follow the C structs,
independently of the extractor's conversion schema.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import struct
import sys

from ps2_cutscene_layout import cutscene_groups

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--upstream', type=Path, required=True)
parser.add_argument('--image', type=Path, required=True)
parser.add_argument('--previous', type=Path, required=True)
args = parser.parse_args()
upstream = args.upstream.resolve()
sys.path.insert(0, str(upstream))
from tools import version_config, dmadata, csdis

manifest = json.loads(args.image.with_suffix('.json').read_text())
prior_manifest = json.loads(args.previous.with_suffix('.json').read_text())
config = version_config.load_version_config(manifest['version'])
original = (upstream / 'baseroms' / manifest['version'] / 'baserom-decompressed.z64').read_bytes()
native = args.image.read_bytes()
previous = args.previous.read_bytes()
assert len(native) == len(original) == len(previous)
entries = dict(zip(config.dmadata_segments, dmadata.read_dmadata(memoryview(original), config.dmadata_start), strict=True))
object_names = {int(index, 16): name for index, name in re.findall(
    r'/\*\s*0x([0-9A-Fa-f]+)\s*\*/\s*DEFINE_OBJECT\((\w+),',
    (upstream / 'include/tables/object_table.h').read_text())}
required = {name for name in entries if name.startswith(('ydan_room_', 'ydan_boss_room_'))}
required.update(('ydan_scene', 'ydan_boss_scene', 'gameplay_dangeon_keep'))
assert required <= manifest['segments'].keys(), f'Missing Deku resources: {required - manifest["segments"].keys()}'
assert prior_manifest['segments'].keys() <= manifest['segments'].keys(), 'Lost a previously converted segment'
added = manifest['segments'].keys() - prior_manifest['segments'].keys()
assert added, 'No newly converted resources'

# The only allowed data-image changes are inside the newly added segments.
cursor = 0
for entry in sorted((entries[name] for name in added), key=lambda value: value.vrom_start):
    assert previous[cursor:entry.vrom_start] == native[cursor:entry.vrom_start], 'Changed data outside new segments'
    cursor = entry.vrom_end
assert previous[cursor:] == native[cursor:], 'Changed data after new segments'

formats = {
    'ActorEntryListResource': '8h',
    'TransitionActorEntryListResource': '4b6h',
    'ObjectListResource': 'H', 'RoomListResource': 'II',
    'SpawnListResource': 'BB', 'ExitListResource': 'H',
    'EnvLightSettingsListResource': '18B2H',
    'RoomShapeCullableResource': 'BB2xII', 'RoomShapeNormalResource': 'BB2xII',
    'RoomShapeCullableEntryArrayResource': '4hII', 'RoomShapeNormalEntryArrayResource': 'II',
    'CollisionResource': '6hH2xIH2xIIIH2xI',
    'CollisionVtxListResource': '3h', 'BgCamFuncDataResource': '3h',
    'CollisionPolyListResource': '8H', 'CollisionSurfaceTypeListResource': 'II',
    'CollisionBgCamListResource': 'HHI', 'CollisionWaterBoxesResource': '5h2xI',
    'DListResource': 'II', 'MtxResource': '32H', 'VtxArrayResource': '3hH2h4B',
    'AnimationResource': 'H2xIIH2x', 'AnimationFrameDataResource': 'h',
    'AnimationJointIndicesResource': '3H',
    'SkeletonNormalResource': 'IB3x', 'StandardLimbsArrayResource': 'I',
    'StandardLimbResource': '3hBBI',
}
unchanged = {'TextureResource', 'TLUTResource', 'TextureSplitTlutResource',
             'ZeroPaddingResource', 'BinaryBlobResource'}
counts = Counter()

def pointer_in_segment(pointer, name):
    if pointer == 0:
        return
    expected_segment = 2 if name.endswith('_scene') else 3 if '_room_' in name else 6
    assert pointer >> 24 == expected_segment, f'Wrong segment pointer in {name}: {pointer:08x}'
    assert (pointer & 0xffffff) < entries[name].vrom_end - entries[name].vrom_start, \
        f'Pointer outside {name}: {pointer:08x}'

for name in sorted(added):
    entry = entries[name]
    for resource in manifest['segments'][name]:
        kind = resource['kind']
        start = entry.vrom_start + resource['offset']
        end = start + resource['size']
        assert entry.vrom_start <= start <= end <= entry.vrom_end
        source, actual = original[start:end], native[start:end]
        if kind in unchanged:
            assert actual == source, f'Changed texture/blob bytes: {name}/{resource["symbol"]}'
        elif kind == 'SceneCommandsResource':
            assert len(source) % 8 == 0
            for offset in range(0, len(source), 8):
                opcode = source[offset]
                assert actual[offset:offset + 4] == source[offset:offset + 4]
                if opcode in (5, 16, 17, 18, 21, 22):
                    assert actual[offset:offset + 8] == source[offset:offset + 8]
                else:
                    assert struct.unpack_from('<I', actual, offset + 4) == struct.unpack_from('>I', source, offset + 4)
                if opcode == 7:
                    object_id = struct.unpack_from('<I', actual, offset + 4)[0]
                    if object_id:
                        assert object_names[object_id] in manifest['segments']
        elif kind == 'CutsceneResource':
            expected = bytearray(source)
            for lo, hi, width in cutscene_groups(source, 0, len(source), csdis):
                for offset in range(lo, hi, width):
                    expected[offset:offset + width] = source[offset:offset + width][::-1]
            assert actual == expected
        else:
            assert kind in formats, f'Unverified C layout: {name}/{kind}'
            fmt = formats[kind]
            assert len(source) % struct.calcsize('>' + fmt) == 0
            assert list(struct.iter_unpack('>' + fmt, source)) == list(struct.iter_unpack('<' + fmt, actual)), \
                f'Native field mismatch: {name}/{resource["symbol"]}'
        if kind == 'ObjectListResource':
            for (object_id,) in struct.iter_unpack('<H', actual):
                assert object_names[object_id] in manifest['segments'], f'Missing object {object_id} for {name}'
        elif kind == 'RoomListResource':
            prefix = name.removesuffix('_scene')
            for room, (lo, hi) in enumerate(struct.iter_unpack('<II', actual)):
                room_name = f'{prefix}_room_{room}'
                assert room_name in manifest['segments']
                assert (lo, hi) == (entries[room_name].vrom_start, entries[room_name].vrom_end), \
                    f'Incorrect VROM range for {room_name}'
        elif kind == 'CollisionResource':
            for offset in (16, 24, 28, 32, 40):
                pointer_in_segment(struct.unpack_from('<I', actual, offset)[0], name)
        elif kind in ('RoomShapeCullableResource', 'RoomShapeNormalResource'):
            shape, count, first, last = struct.unpack('<BB2xII', actual)
            size = 16 if shape == 2 else 8
            assert shape in (0, 2) and count <= 64
            pointer_in_segment(first, name)
            assert last == first + count * size
            assert (last & 0xffffff) <= entry.vrom_end - entry.vrom_start
        counts[kind] += 1
print(f'PASS: Deku Tree + boss, 14 rooms, {len(added)} new segments, native commands, '
      f'collision, geometry, skeletons and animations; {sum(counts.values())} resources checked.')
print(f'PASS: object dependencies present and all data outside new segments unchanged, '
      f'including {len(prior_manifest["segments"])} previously converted segments and audio fonts.')
