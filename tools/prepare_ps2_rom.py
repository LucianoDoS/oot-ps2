#!/usr/bin/env python3
"""Build a private, decompressed PS2 data image with explicitly supported native segments.

Uses the pinned upstream extractor to discover hidden display lists/vertices;
never infers their ranges from filenames or scans arbitrary bytes for opcodes.
Unsupported resource kinds abort conversion rather than corrupting the image.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--upstream', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--version', default='ntsc-1.2')
parser.add_argument('--segment', action='append', default=[])
parser.add_argument('--profile', choices=('logo', 'title'), default='logo')
args = parser.parse_args()
upstream = args.upstream.resolve()
output = args.output.resolve()
sys.path.insert(0, str(upstream))
from tools import version_config, dmadata, csdis
from ps2_cutscene_layout import cutscene_groups
from tools.assets.descriptor import base
from tools.assets.extract import extract_xml_z64 as extract
from tools.assets.extract.extase import File
from tools.assets.extract.extase.memorymap import MemoryContext
from tools.assets.extract.extase.cdata_resources import CDataResource
from tools.assets.extract.extase.repr_c_struct import CData_Value, CData_Array, CData_Struct

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
profiles = {
    'logo': ['nintendo_rogo_static'],
    'title': ['nintendo_rogo_static', 'spot00_scene', 'spot00_room_0',
              'gameplay_keep', 'gameplay_field_keep', 'object_link_boy',
              'object_horse', 'object_mag', 'object_spot00_objects',
              'object_spot00_break', 'object_wood02'],
}
selected = set(args.segment or profiles[args.profile])
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
    for pool in base.get_resources_desc(config):
        if any(isinstance(coll.backing_memory, base.BaseromFileBackingMemory)
               and coll.backing_memory.name in selected for coll in pool.collections):
            extract.process_pool(extraction, pool)
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
output.parent.mkdir(parents=True, exist_ok=True)
manifest['output_sha256'] = hashlib.sha256(data).hexdigest()
output.write_bytes(data)
output.with_suffix('.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(f'Wrote {output}: {len(data)} bytes, native segments: {", ".join(sorted(captured))}')
