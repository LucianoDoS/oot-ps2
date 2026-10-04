#!/usr/bin/env python3
"""Verify received-item models using the extractor's explicit resource ranges."""
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
names = [name for name in entries if name.startswith('object_gi_')]
counts = {'DListResource': 0, 'VtxArrayResource': 0, 'TextureResource': 0}
for name in names:
    assert name in manifest['segments'], f'Missing received-item model: {name}'
    kinds = set()
    for resource in manifest['segments'][name]:
        kind = resource['kind']
        start = entries[name].vrom_start + resource['offset']
        end = start + resource['size']
        expected = bytearray(original[start:end])
        if kind in ('DListResource', 'MtxResource'):
            step = 4 if kind == 'DListResource' else 2
            for offset in range(0, len(expected), step):
                expected[offset:offset+step] = expected[offset:offset+step][::-1]
        elif kind == 'VtxArrayResource':
            assert len(expected) % 16 == 0
            for vertex in range(0, len(expected), 16):
                for member in range(vertex, vertex+12, 2):
                    expected[member:member+2] = expected[member:member+2][::-1]
        elif kind not in ('TextureResource', 'TLUTResource', 'TextureSplitTlutResource', 'ZeroPaddingResource', 'BinaryBlobResource'):
            raise AssertionError(f'Untested received-item layout: {name}/{kind}')
        assert native[start:end] == expected, f'Bad native resource: {name}/{resource["symbol"]}'
        kinds.add(kind)
        if kind in counts:
            counts[kind] += 1
    assert {'DListResource', 'VtxArrayResource'} <= kinds, f'Missing model geometry in {name}'
assert {'object_gi_sword_1', 'object_gi_heart'} <= set(names)
preserved = 0
if args.previous:
    previous_manifest = json.loads(args.previous.with_suffix('.json').read_text())
    previous = args.previous.read_bytes()
    for name in previous_manifest['segments']:
        entry = entries[name]
        assert previous[entry.vrom_start:entry.vrom_end] == native[entry.vrom_start:entry.vrom_end], f'Previous native segment changed: {name}'
        preserved += 1
print(f'PASS: {len(names)} received-item models, {counts}; {preserved} previous native segments preserved.')
