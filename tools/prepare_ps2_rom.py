#!/usr/bin/env python3
"""Build a private, decompressed PS2 data image with explicitly supported native segments.

Uses the pinned upstream extractor to discover hidden display lists/vertices;
never infers their ranges from filenames or scans arbitrary bytes for opcodes.
Unsupported resource kinds abort conversion rather than corrupting the image.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--upstream', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--version', default='ntsc-1.2')
parser.add_argument('--segment', action='append', default=[])
parser.add_argument('--profile', choices=('logo', 'title', 'play'), default='logo')
args = parser.parse_args()
upstream = args.upstream.resolve()
output = args.output.resolve()
sys.path.insert(0, str(upstream))
from tools import version_config, dmadata, csdis
from ps2_cutscene_layout import cutscene_groups
from ps2_soundfont_layout import convert_audiobank
from tools.assets.descriptor import base
from tools.assets.extract import extract_xml_z64 as extract
from tools.assets.extract.extase import File
from tools.assets.extract.extase.memorymap import MemoryContext
from tools.assets.extract.extase.cdata_resources import CDataResource
from tools.assets.extract.extase.repr_c_struct import CData_Value, CData_Array, CData_Struct
from tools.assets.extract.extase_oot64.dlist_resources import MtxResource, TextureResource
from tools.assets.n64 import G_IM_FMT, G_IM_SIZ

def scalar_groups(schema, start):
    """Walk explicit upstream layouts, preserving byte fields and padding."""
    if isinstance(schema, CData_Value):
        if schema.size > 1:
            yield (start, start + schema.size, schema.size)
    elif isinstance(schema, CData_Array):
        for index in range(schema.length):
            yield from scalar_groups(schema.element_cdata, start + index * schema.element_cdata.size)
    elif isinstance(schema, CData_Struct):
        for _, member in schema.members:
            yield from scalar_groups(member, start)
            start += member.size
    else:
        raise ValueError(f'Unsupported C layout: {type(schema).__name__}')

config = version_config.load_version_config(args.version)
for asset in config.assets:
    asset.xml_path = upstream / asset.xml_path
data = bytearray((upstream / 'baseroms' / args.version / 'baserom-decompressed.z64').read_bytes())
entries = dmadata.read_dmadata(memoryview(data), config.dmadata_start)
entries_by_name = dict(zip(config.dmadata_segments, entries, strict=True))
context = MemoryContext({(entry.vrom_start, entry.vrom_end): name for name, entry in entries_by_name.items()})
identity = File('sys_matrix__gIdentityMtx', size=0x40)
identity.add_resource(MtxResource(identity, 0, 'gIdentityMtx'))
context.set_direct_file(config.variables['gIdentityMtx'], identity)
shadow = File('z_en_jsjutan__sShadowTex', size=0x800)
shadow.add_resource(TextureResource(shadow, 0, 'sShadowTex', G_IM_FMT.I, G_IM_SIZ._8b, 32, 64))
context.set_direct_file(config.variables['sShadowTex'], shadow)
profiles = {
    'logo': ['nintendo_rogo_static'],
    'title': ['nintendo_rogo_static', 'spot00_scene', 'spot00_room_0',
              'gameplay_keep', 'gameplay_field_keep', 'object_link_boy',
              'object_horse', 'object_mag', 'object_spot00_objects',
              'object_spot00_break', 'object_wood02', 'title_static', 'parameter_static',
              'link_animetion'],
}
profiles['play'] = profiles['title'] + [
    'object_link_child', 'icon_item_static', 'icon_item_24_static',
    'icon_item_field_static', 'do_action_static', 'map_name_static',
    'link_home_scene', 'link_home_room_0', 'spot04_scene', 'spot04_room_0',
]
selected = set(args.segment or profiles[args.profile])
if not args.segment and args.profile in ('title', 'play'):
    scene_prefixes = ['jyasinboss', 'spot17', 'hiral_demo']
    if args.profile == 'play':
        # These are reachable directly from the initial forest. Raw N64
        # room-list pointers cannot be consumed by native EE scene code.
        scene_prefixes += ['spot10', 'kokiri_home', 'kokiri_home3',
                           'kokiri_home4', 'kokiri_home5', 'kokiri_shop']
    for scene_prefix in scene_prefixes:
        selected.update(name for name in entries_by_name
                        if name == scene_prefix + '_scene' or name.startswith(scene_prefix + '_room_'))
object_names = {int(index, 16): name for index, name in re.findall(
    r'/\*\s*0x([0-9A-Fa-f]+)\s*\*/\s*DEFINE_OBJECT\((\w+),',
    (upstream / 'include/tables/object_table.h').read_text())}
captured = {}
original_paths = File.set_resources_paths

def capture(file, *paths):
    original_paths(file, *paths)
    if file.name in selected:
        captured[file.name] = list(file._resources)

File.set_resources_paths = capture
extract.WRITE_EXTRACT = False
extract.WRITE_SOURCE = False
extract.USE_RICH = False
extract.z64_resource_handlers.register_resource_handlers()
extraction = extract.ExtractionContext(args.version, context,
    upstream / 'extracted' / args.version / 'baserom',
    upstream / 'build' / args.version, upstream / 'extracted' / args.version, set())
try:
    pools = list(base.get_resources_desc(config))
    while True:
        for index, pool in enumerate(pools):
            if any(isinstance(coll.backing_memory, base.BaseromFileBackingMemory)
                   and coll.backing_memory.name in selected and coll.backing_memory.name not in captured
                   for coll in pool.collections):
                extract.process_pool(extraction, pool)
        dependencies = set()
        for name, resources in captured.items():
            base_offset = entries_by_name[name].vrom_start
            for resource in resources:
                if type(resource).__name__ == 'ObjectListResource':
                    for offset in range(resource.range_start, resource.range_end, 2):
                        object_id = int.from_bytes(data[base_offset + offset:base_offset + offset + 2], 'big')
                        if object_id not in object_names:
                            raise ValueError(f'Unknown object ID {object_id:#x} in {name}')
                        dependencies.add(object_names[object_id])
                elif type(resource).__name__ == 'SceneCommandsResource':
                    for offset in range(resource.range_start, resource.range_end, 8):
                        command = base_offset + offset
                        if data[command] == 7:  # SPECIAL_FILES selects the secondary keep.
                            object_id = int.from_bytes(data[command + 4:command + 8], 'big')
                            if object_id:
                                dependencies.add(object_names[object_id])
        missing = dependencies - selected
        if not missing:
            break
        selected.update(missing)
finally:
    File.set_resources_paths = original_paths
if selected != captured.keys():
    raise ValueError(f'Segments not extracted: {selected - captured.keys()}')

manifest = {'version': args.version, 'source_sha256': hashlib.sha256(data).hexdigest(),
            'segments': {}, 'unconverted_segments_remain': True}
for name, resources in captured.items():
    entry = entries_by_name[name]
    records = []
    for resource in resources:
        start = entry.vrom_start + resource.range_start
        end = entry.vrom_start + resource.range_end
        if not entry.vrom_start <= start <= end <= entry.vrom_end:
            raise ValueError(f'Resource outside segment: {name}/{resource.symbol_name}')
        kind = type(resource).__name__
        if kind == 'DListResource':
            groups = [(start, end, 4)]
        elif kind == 'MtxResource':
            groups = [(start, end, 2)]
        elif kind == 'CutsceneResource':
            groups = cutscene_groups(data, start, end, csdis)
        elif kind == 'VtxArrayResource':
            if (end - start) % 16:
                raise ValueError(f'Invalid vertex size: {resource.symbol_name}')
            groups = [(offset, offset + 12, 2) for offset in range(start, end, 16)]
        elif kind in ('TextureResource', 'TLUTResource', 'TextureSplitTlutResource', 'ZeroPaddingResource', 'BinaryBlobResource'):
            groups = []  # The GS texture decoder consumes the original byte stream.
            # Unreferenced byte blobs retain their exact contents; the manifest
            # identifies them explicitly, without guessing a scalar layout.
        elif kind == 'JFIFResource':
            if data[start:start + 2] != b'\xff\xd8':
                raise ValueError(f'Invalid JPEG signature: {resource.symbol_name}')
            groups = []  # Compressed JPEG markers and bitstream remain big-endian bytes.
        elif kind == 'SceneCommandsResource':
            if (end - start) % 8:
                raise ValueError(f'Invalid scene commands: {resource.symbol_name}')
            groups = []
            for offset in range(start, end, 8):
                opcode = data[offset]
                if opcode > 0x19:
                    raise ValueError(f'Unknown scene command: {opcode:#x}')
                # These commands store separate bytes, not a native u32 payload.
                if opcode not in (0x05, 0x10, 0x11, 0x12, 0x15, 0x16):
                    groups.append((offset + 4, offset + 8, 4))
        elif isinstance(resource, CDataResource):
            if resource.cdata_ext.size != end - start:
                raise ValueError(f'Invalid C layout size: {resource.symbol_name}')
            groups = scalar_groups(resource.cdata_ext, start)
        else:
            raise ValueError(f'Unsupported {name}/{resource.symbol_name}: {kind}')
        for group_start, group_end, width in groups:
            if not start <= group_start <= group_end <= end:
                raise ValueError(f'Conversion outside resource: {resource.symbol_name}')
            if (group_end - group_start) % width:
                raise ValueError(f'Invalid resource alignment: {resource.symbol_name}')
            for offset in range(group_start, group_end, width):
                data[offset:offset + width] = data[offset:offset + width][::-1]
        records.append({'symbol': resource.symbol_name, 'offset': resource.range_start,
                        'size': resource.range_end - resource.range_start, 'kind': kind})
    manifest['segments'][name] = records
manifest['audio_fonts'] = convert_audiobank(data, entries_by_name, config)
output.parent.mkdir(parents=True, exist_ok=True)
manifest['output_sha256'] = hashlib.sha256(data).hexdigest()
output.write_bytes(data)
output.with_suffix('.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(f'Wrote {output}: {len(data)} bytes, native segments: {", ".join(sorted(captured))}')
