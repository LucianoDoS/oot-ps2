#!/usr/bin/env python3
"""Generate native-endian DMA/audio tables and real VROM linker offsets.

Run with the upstream extraction's virtualenv; outputs are private build data.
No asset address is fabricated to make the linker succeed.
"""
import argparse
from pathlib import Path
import struct
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--upstream', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--version', default='ntsc-1.2')
args = parser.parse_args()
sys.path.insert(0, str(args.upstream / 'tools'))
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
