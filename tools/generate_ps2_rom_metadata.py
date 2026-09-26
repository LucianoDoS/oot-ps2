#!/usr/bin/env python3
"""Generate native-endian DMA/audio tables and real VROM linker offsets.

Run with the upstream extraction's virtualenv; outputs are private build data.
No asset address is fabricated to make the linker succeed.
"""
import argparse
from pathlib import Path
import struct
import sys
import json

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--upstream', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--version', default='ntsc-1.2')
args = parser.parse_args()
args.upstream = args.upstream.resolve()
args.output = args.output.resolve()
sys.path.insert(0, str(args.upstream / 'tools'))
sys.path.insert(0, str(args.upstream))
import version_config
import dmadata

config = version_config.load_version_config(args.version)
rom = (args.upstream / 'baseroms' / args.version / 'baserom-decompressed.z64').read_bytes()
entries = dmadata.read_dmadata(memoryview(rom), config.dmadata_start)
names = list(config.dmadata_segments)
if len(entries) != len(names):
    raise SystemExit('Segment name count does not match the ROM DMA table.')
out = args.output
out.mkdir(parents=True, exist_ok=True)
(out / 'assets/audio').mkdir(parents=True, exist_ok=True)
ld = ['/* Real offsets from the local decompressed ROM; generated. */']
c = ['/* Generated from local ROM metadata. Do not distribute. */', '#include "dma.h"', '#include "audio.h"']
c.append('DmaEntry gDmaDataTable[] = {')
for name, entry in zip(names, entries):
    ld += [f'PROVIDE(_{name}SegmentRomStart = 0x{entry.vrom_start:X});',
           f'PROVIDE(_{name}SegmentRomEnd = 0x{entry.vrom_end:X});']
    c.append(f'    {{{{0x{entry.vrom_start:X}, 0x{entry.vrom_end:X}}}, 0x{entry.rom_start:X}, 0x{entry.rom_end:X}}},')
c += ['    {{0, 0}, 0, 0}', '};']
code_entry = entries[names.index('code')]
code = rom[code_entry.vrom_start:code_entry.vrom_end]
code_vram = config.dmadata_segments['code'].vram

# Preserve the actual task microcode bytes and identities. The PS2 HLE consumes
# task command lists, not these N64 instructions; no zero-sized dummy blobs.
for blob in config.incbins:
    if not blob.name.startswith(('rspboot', 'aspMain', 'gspF3', 'gspS2', 'njpgdsp')):
        continue
    entry = entries[names.index(blob.segment)]
    offset = entry.vrom_start + blob.vram - config.dmadata_segments[blob.segment].vram
    payload = rom[offset:offset + blob.size]
    if len(payload) != blob.size or blob.size % 8:
        raise ValueError(f'Invalid microcode blob: {blob.name}')
    c.append(f'u64 {blob.name}Start[{blob.size // 8}] __attribute__((aligned(16))) = {{')
    for chunk in struct.iter_unpack('<Q', payload):
        c.append(f'    0x{chunk[0]:016X}ULL,')
    c.append('};')
    ld.append(f'PROVIDE({blob.name}End = {blob.name}Start + 0x{blob.size:X});')

# Deliberate runtime segment-base placeholders used by the original GBI code.
for segment in (1, 6, 14, 15):
    address = segment << 24
    ld.append(f'PROVIDE(D_{address:08X} = 0x{address:08X});')

# Message pointers are segmented offsets, not native RAM addresses. The game
# subtracts them to locate byte strings in the corresponding DMA file.
message_count = 0
for language, table in [('jpn', 'sJpnMessageEntryTable'), ('nes', 'sNesMessageEntryTable'),
                         ('staff', 'sStaffMessageEntryTable')]:
    cursor = config.variables[table] - code_vram
    segment_name = f'{language}_message_data_static'
    segment_entry = entries[names.index(segment_name)]
    segment_size = segment_entry.vrom_end - segment_entry.vrom_start
    first_address = None
    for index in range(10000):
        text_id, type_pos, padding, address = struct.unpack_from('>HBBI', code, cursor + index * 8)
        if text_id == 0xFFFF:
            break
        if first_address is None:
            first_address = address
            ld.append(f'PROVIDE(_{segment_name}SegmentStart = 0x{address:X});')
        if not 0 <= address - first_address <= segment_size:
            raise ValueError(f'Message {text_id:04X}/{language} lies outside its DMA segment')
        ld.append(f'PROVIDE(_message_0x{text_id:04X}_{language} = 0x{address:X});')
        message_count += 1
    else:
        raise ValueError(f'Missing terminator in {table}')

# Reuse the version-aware upstream descriptor parser: it understands relative
# offsets, arrays and cross-file resources. Do not guess addresses from names.
from tools.assets.descriptor import base as asset_desc
for asset in config.assets:
    asset.xml_path = args.upstream / asset.xml_path
asset_addresses = {}
for pool in asset_desc.get_resources_desc(config):
    for collection in pool.collections:
        if not isinstance(collection.start_address, asset_desc.SegmentStartAddress):
            continue  # Native code/overlay globals must come from EE objects.
        backing = collection.backing_memory
        if not isinstance(backing, asset_desc.BaseromFileBackingMemory) or backing.name not in names:
            continue
        entry = entries[names.index(backing.name)]
        for resource in collection.resources:
            if not 0 <= resource.offset < entry.vrom_end - entry.vrom_start:
                raise ValueError(f'Asset outside its DMA segment: {resource.symbol_name}')
            address = (collection.start_address.segment << 24) | resource.offset
            info = {'address': address, 'segment': backing.name, 'offset': resource.offset,
                    'kind': type(resource).__name__}
            old = asset_addresses.get(resource.symbol_name)
            if old is not None and old != info:
                raise ValueError(f'Ambiguous asset symbol: {resource.symbol_name}')
            asset_addresses[resource.symbol_name] = info
for symbol, info in sorted(asset_addresses.items()):
    ld.append(f'PROVIDE({symbol} = 0x{info["address"]:X});')
(out / 'asset_addresses.json').write_text(json.dumps(asset_addresses, indent=2) + '\n')
tables = {}
for symbol, prefix, filename, count_name in [
    ('gSoundFontTable', 'Soundfont', 'soundfont_sizes.h', 'NUM_SOUNDFONTS'),
    ('gSequenceTable', 'Sequence', 'sequence_sizes.h', 'NUM_SEQUENCES'),
    ('gSampleBankTable', 'SampleBank', None, None),
]:
    offset = config.variables[symbol] - code_vram
    count, medium, address = struct.unpack_from('>hhI', code, offset)
    if not 0 < count < 256:
        raise SystemExit(f'Invalid {symbol} count: {count}')
    rows = [struct.unpack_from('>IIbbhhh', code, offset + 16 + i * 16) for i in range(count)]
    tables[symbol] = rows
    c += [f'#pragma weak {symbol} = ps2_{symbol}',
          f'struct {{ AudioTableHeader header; AudioTableEntry entries[{count}]; }} ps2_{symbol} = {{',
          f'    {{{count}, {medium}, {address}, {{0}}}},', '    {']
    for row in rows:
        c.append('        {' + ', '.join(str(value) for value in row) + '},')
    c += ['    }', '};']
    if filename:
        header = [f'#define {count_name} {count}']
        for i, row in enumerate(rows):
            size = row[1]
            if size == 0:
                size = rows[row[0]][1]
            header.append(f'#define {prefix}_{i}_SIZE 0x{size:X}')
        (out / 'assets/audio' / filename).write_text('\n'.join(header) + '\n')

# The sequence->font map starts with BE u16 offsets, followed by byte lists.
offset = config.variables['gSequenceFontTable'] - code_vram
count = len(tables['gSequenceTable'])
offsets = struct.unpack_from(f'>{count}H', code, offset)
length = max(index + 1 + code[offset + index] for index in offsets)
mapping = bytearray(code[offset:offset+length])
for i, index in enumerate(offsets):
    struct.pack_into('<H', mapping, i * 2, index)
c += ['u8 gSequenceFontTable[] __attribute__((aligned(16))) = {']
for i in range(0, len(mapping), 16):
    c.append('    ' + ', '.join(f'0x{x:02X}' for x in mapping[i:i+16]) + ',')
c += ['};']
(out / 'rom_metadata.c').write_text('\n'.join(c) + '\n')
(out / 'rom_segments.ld').write_text('\n'.join(ld) + '\n')
print(f'Generated {len(entries)} DMA entries and 3 native audio tables in {out}')
print(f'Resolved {message_count} message offsets and {len(asset_addresses)} documented segmented assets.')
