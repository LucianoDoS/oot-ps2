#!/usr/bin/env python3
"""Group resident actors' writable sections so their original load state can be restored."""
import argparse
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
actors = re.findall(r'^/\*.*?\*/\s*DEFINE_ACTOR\((\w+),\s*(\w+),',
                    (root / 'include/tables/actor_table.h').read_text(), re.M)
out = args.build_dir / 'generated'
out.mkdir(parents=True, exist_ok=True)
data = ['SECTIONS {', '  .ps2_actor_data ALIGN(128) : {', '    ps2_actor_data_start = .;']
bss = ['SECTIONS {', '  .ps2_actor_bss ALIGN(128) (NOLOAD) : {']
decls, rows = [], []
for name, actor_id in actors:
    directory = root / 'src/overlays/actors' / ('ovl_' + name)
    sources = sorted(path for path in directory.glob('*.c') if not path.name.endswith('.inc.c'))
    assert sources, f'Missing actor sources: {name}'
    prefix = 'ps2_actor_' + name
    for lines, suffix, sections in ((data, 'data', '.data .data.* .sdata .sdata.* .gnu.linkonce.d* .gnu.linkonce.s*'),
                                    (bss, 'bss', '.bss .bss.* .sbss .sbss.* .scommon COMMON')):
        lines += ['    . = ALIGN(16);', f'    {prefix}_{suffix}_start = .;']
        for source in sources:
            obj = (args.build_dir / source.relative_to(root)).with_suffix('.o').as_posix()
            assert not re.search(r'[\s"{}()]', obj), 'Use a build directory without whitespace in WSL.'
            lines.append(f'    "{obj}"({sections})')
        lines.append(f'    {prefix}_{suffix}_end = .;')
        decls.append(f'extern unsigned char {prefix}_{suffix}_start[], {prefix}_{suffix}_end[];')
    rows.append(f'    [{actor_id}] = {{{prefix}_data_start, {prefix}_data_end, '
                f'{prefix}_bss_start, {prefix}_bss_end}},')
data += ['    ps2_actor_data_end = .;', '  }', '} INSERT BEFORE .data;']
bss += ['  }', '} INSERT BEFORE .bss;']
contents = {'actor_overlays.ld': '\n'.join(data + bss) + '\n',
            'actor_overlays.inc': '\n'.join(decls + ['static const ActorBank sActorBanks[ACTOR_ID_MAX] = {'] + rows + ['};']) + '\n'}
for filename, content in contents.items():
    path = out / filename
    if not path.exists() or path.read_text() != content:
        path.write_text(content)
print(f'Prepared writable section boundaries for {len(actors)} resident actor overlays.')
