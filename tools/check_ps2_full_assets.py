#!/usr/bin/env python3
"""Validate complete described ROM data using the native C layouts.

Only reads private local images. Fixed layouts are taken from include/{scene,
room,animation,animation_legacy,skin,curve,collision,path}.h, not the converter.
This checks data coverage and field order, not a playthrough of every scene.
"""
import argparse
from collections import Counter
import hashlib
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
from tools.assets.descriptor import base

manifest = json.loads(args.image.with_suffix('.json').read_text())
prior = json.loads(args.previous.with_suffix('.json').read_text())
config = version_config.load_version_config(manifest['version'])
for asset in config.assets:
    asset.xml_path = upstream / asset.xml_path
original = (upstream / 'baseroms' / manifest['version'] / 'baserom-decompressed.z64').read_bytes()
native = args.image.read_bytes()
previous = args.previous.read_bytes()
assert len(native) == len(original) == len(previous)
assert hashlib.sha256(original).hexdigest() == manifest['source_sha256'] == prior['source_sha256']
assert hashlib.sha256(native).hexdigest() == manifest['output_sha256'], 'Incorrect output checksum'
entries = dict(zip(config.dmadata_segments, dmadata.read_dmadata(memoryview(original), config.dmadata_start), strict=True))

expected = {coll.backing_memory.name for pool in base.get_resources_desc(config)
    for coll in pool.collections if isinstance(coll.backing_memory, base.BaseromFileBackingMemory)
    and coll.backing_memory.name not in ('code', 'n64dd')
    and not coll.backing_memory.name.startswith('ovl_')}
assert expected == manifest['segments'].keys(), f'Coverage mismatch: {expected ^ manifest["segments"].keys()}'
assert prior['segments'].keys() <= manifest['segments'].keys(), 'Lost a previously converted segment'
added = manifest['segments'].keys() - prior['segments'].keys()
assert added, 'No added data segments'
cursor = 0
for entry in sorted((entries[n] for n in added), key=lambda e: e.vrom_start):
    assert previous[cursor:entry.vrom_start] == native[cursor:entry.vrom_start], 'Changed previous data/audio/code'
    cursor = entry.vrom_end
assert previous[cursor:] == native[cursor:], 'Changed data after added segments'

# Independent scalar layouts, with explicit padding and byte fields.
formats = {
    'ActorEntryListResource': '8h', 'TransitionActorEntryListResource': '4b6h',
    'ObjectListResource': 'H', 'RoomListResource': 'II',
    'SpawnListResource': 'BB', 'ExitListResource': 'H', 'AltHeadersResource': 'I',
    'EnvLightSettingsListResource': '18B2H', 'PathListResource': 'B3xI',
    'Vec3sArrayResource': '3h', 'BgCamFuncDataResource': '3h',
    'RoomShapeCullableResource': 'BB2xII', 'RoomShapeNormalResource': 'BB2xII',
    'RoomShapeCullableEntryArrayResource': '4hII', 'RoomShapeNormalEntryArrayResource': 'II',
    'RoomShapeDListsEntryResource': 'II',
    'RoomShapeImageSingleResource': 'BB2x4I2H2B2H2x',
    'RoomShapeImageMultiResource': 'BB2xIB3xI',
    'RoomShapeImageMultiBgEntryArrayResource': 'HBx3I2H2B2H2x',
    'CollisionResource': '6hH2xIH2xIIIH2xI',
    'CollisionVtxListResource': '3h', 'CollisionPolyListResource': '8H',
    'CollisionSurfaceTypeListResource': 'II', 'CollisionBgCamListResource': 'HHI',
    'CollisionWaterBoxesResource': '5h2xI',
    'DListResource': 'II', 'MtxResource': '32H', 'VtxArrayResource': '3hH2h4B',
    'AnimationResource': 'H2xIIH2x', 'AnimationFrameDataResource': 'h',
    'AnimationJointIndicesResource': '3H', 'PlayerAnimationResource': 'H2xI',
    'PlayerAnimationDataResource': 'h',
    'SkeletonNormalResource': 'IB3x', 'SkeletonNormalLODResource': 'IB3x',
    'SkeletonSkinResource': 'IB3x', 'SkeletonFlexResource': 'IB3xB3x',
    'SkeletonFlexLODResource': 'IB3xB3x',
    'StandardLimbsArrayResource': 'I', 'LODLimbsArrayResource': 'I', 'SkinLimbsArrayResource': 'I',
    'StandardLimbResource': '3hBBI', 'LODLimbResource': '3hBBII',
    'SkinLimbResource': '3hBBiI', 'SkinAnimatedLimbDataResource': 'HHII',
    'SkinLimbModifArrayResource': '3H2xII', 'SkinVertexArrayResource': 'H2h3bB',
    'SkinTransformationArrayResource': 'Bx3hBx',
    'CurveSkeletonHeaderResource': 'IB3x', 'SkelCurveLimbArrayResource': 'I',
    'SkelCurveLimbResource': 'BB2xII', 'CurveAnimationHeaderResource': 'IIIhh',
    'CurveInterpKnotArrayResource': 'H3hf', 'KnotCountsArrayResource': 'B',
    'ConstantDataArrayResource': 'h',
    'LegacyAnimationResource': 'hhII', 'LegacyJointKeyArrayResource': '6h',
    'LegacyLimbResource': 'I3f3h2xII', 'LegacyLimbsArrayResource': 'I',
}
unchanged = {'TextureResource', 'TLUTResource', 'TextureSplitTlutResource',
             'ZeroPaddingResource', 'BinaryBlobResource', 'JFIFResource'}
object_names = {int(index, 16): name for index, name in re.findall(
    r'/\*\s*0x([0-9A-Fa-f]+)\s*\*/\s*DEFINE_OBJECT\((\w+),',
    (upstream / 'include/tables/object_table.h').read_text())}
scene_names = re.findall(r'^/\*.*?\*/\s*DEFINE_SCENE\((\w+),',
    (upstream / 'include/tables/scene_table.h').read_text(), re.M)
# Debug-only scenes do not exist in the NTSC 1.2 data image.
retail_scenes = [name for name in scene_names if name in entries]
assert set(retail_scenes) <= manifest['segments'].keys()
assert {name for name in object_names.values() if name in entries} <= manifest['segments'].keys()
counts = Counter()
room_lists = 0

def check_pointer(pointer, name, length=1):
    if pointer == 0:
        assert length == 0, f'Null required pointer in {name}'
        return
    keep_segments = {'gameplay_keep': 4, 'gameplay_field_keep': 5, 'gameplay_dangeon_keep': 5}
    segment = keep_segments.get(name, 2 if name.endswith('_scene') else 3 if '_room_' in name else 6)
    assert pointer >> 24 == segment, f'Wrong pointer segment in {name}: {pointer:08x}'
    offset = pointer & 0xffffff
    assert 0 <= offset <= entries[name].vrom_end - entries[name].vrom_start - length, \
        f'Pointer outside {name}: {pointer:08x} + {length}'

for name, records in sorted(manifest['segments'].items()):
    entry = entries[name]
    ranges = []
    for resource in records:
        kind = resource['kind']
        start = entry.vrom_start + resource['offset']
        end = start + resource['size']
        assert entry.vrom_start <= start <= end <= entry.vrom_end
        source, actual = original[start:end], native[start:end]
        if kind in unchanged:
            assert actual == source, f'Changed byte stream: {name}/{resource["symbol"]}'
            if kind == 'JFIFResource':
                assert actual[:2] == b'\xff\xd8'
        elif kind == 'SceneCommandsResource':
            assert len(source) % 8 == 0
            for offset in range(0, len(source), 8):
                opcode = source[offset]
                assert 0 <= opcode <= 0x19
                assert actual[offset:offset + 4] == source[offset:offset + 4]
                if opcode in (5, 16, 17, 18, 21, 22):
                    assert actual[offset:offset + 8] == source[offset:offset + 8]
                else:
                    assert struct.unpack_from('<I', actual, offset + 4) == struct.unpack_from('>I', source, offset + 4), (
                        f'Native field mismatch: {name}/{resource["symbol"]} '
                        f'scene command {opcode:02x} at +{offset:04x}'
                    )
                if opcode == 7:
                    object_id = struct.unpack_from('<I', actual, offset + 4)[0]
                    if object_id:
                        assert object_names[object_id] in manifest['segments']
        elif kind == 'CutsceneResource':
            expected_bytes = bytearray(source)
            for lo, hi, width in cutscene_groups(source, 0, len(source), csdis):
                for offset in range(lo, hi, width):
                    expected_bytes[offset:offset + width] = source[offset:offset + width][::-1]
            assert actual == expected_bytes, f'Cutscene mismatch: {name}/{resource["symbol"]}'
        else:
            assert kind in formats, f'Unverified C layout: {name}/{kind}'
            fmt = formats[kind]
            assert len(source) % struct.calcsize('>' + fmt) == 0
            assert list(struct.iter_unpack('>' + fmt, source)) == list(struct.iter_unpack('<' + fmt, actual)), \
                f'Native field mismatch: {name}/{resource["symbol"]}'

        if kind == 'ObjectListResource':
            for (object_id,) in struct.iter_unpack('<H', actual):
                assert object_names[object_id] in manifest['segments'], f'Missing object {object_id} in {name}'
        elif kind == 'RoomListResource':
            prefix = name.removesuffix('_scene')
            for room, (lo, hi) in enumerate(struct.iter_unpack('<II', actual)):
                room_name = f'{prefix}_room_{room}'
                assert room_name in manifest['segments']
                assert (lo, hi) == (entries[room_name].vrom_start, entries[room_name].vrom_end)
            room_lists += 1
        elif kind == 'CollisionResource':
            for offset in (16, 24, 28, 32, 40):
                pointer = struct.unpack_from('<I', actual, offset)[0]
                if pointer:
                    check_pointer(pointer, name)
        elif kind in ('RoomShapeCullableResource', 'RoomShapeNormalResource'):
            shape, count, first, last = struct.unpack('<BB2xII', actual)
            stride = 16 if shape == 2 else 8
            assert shape in (0, 2)
            if shape == 2:
                assert count <= 64
            check_pointer(first, name, count * stride)
            assert last == first + count * stride
        elif kind == 'PathListResource':
            for count, pointer in struct.iter_unpack('<B3xI', actual):
                check_pointer(pointer, name, count * 6)
        elif kind == 'AltHeadersResource':
            for (pointer,) in struct.iter_unpack('<I', actual):
                if pointer:
                    check_pointer(pointer, name, 8)
        counts[kind] += 1
        ranges.append((start, end))
    # Unreferenced bytes and gaps are never guessed to be native scalars.
    cursor = entry.vrom_start
    for start, end in sorted(ranges):
        assert start >= cursor, f'Overlapping resources in {name}'
        assert native[cursor:start] == original[cursor:start], f'Changed undescribed bytes in {name}'
        cursor = end
    assert native[cursor:entry.vrom_end] == original[cursor:entry.vrom_end]

scenes = sum(name.endswith('_scene') for name in manifest['segments'])
rooms = sum('_room_' in name for name in manifest['segments'])
objects = sum(name.startswith('object_') for name in manifest['segments'])
print(f'PASS: {len(manifest["segments"])} data segments, {scenes} scenes, {rooms} rooms, {objects} objects.')
print(f'PASS: {sum(counts.values())} resources, {len(counts)} native layouts/byte streams, {room_lists} room lists.')
print(f'PASS: retail scene/object tables covered; dependencies, paths, collision and pointers within bounds.')
print(f'PASS: {len(prior["segments"])} previous segments and all audio/code/outside data unchanged.')
print('NOTE: resource validation does not establish complete gameplay or hardware compatibility.')
