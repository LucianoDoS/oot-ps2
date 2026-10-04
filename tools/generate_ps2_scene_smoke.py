#!/usr/bin/env python3
"""Generate test entrances from the real scene and entrance tables."""
import argparse
import json
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--image', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--start', type=int, default=0)
parser.add_argument('--count', type=int)
parser.add_argument('--frames', type=int, default=60)
args = parser.parse_args()
assert args.start >= 0 and args.frames >= 30
project = Path(__file__).resolve().parents[1]
manifest = json.loads(args.image.with_suffix('.json').read_text())
scenes = re.findall(r'^/\*\s*0x([0-9A-Fa-f]+)\s*\*/\s*DEFINE_SCENE\((\w+),\s*\w+,\s*(\w+),',
    (project / 'include/tables/scene_table.h').read_text(), re.M)
pattern = r'^/\*\s*0x([0-9A-Fa-f]+)\s*\*/\s*DEFINE_ENTRANCE\(\w+,\s*(\w+),\s*(\d+),'
groups = []
for block in (project / 'include/tables/entrance_table.h').read_text().split('\n\n'):
    entries = re.findall(pattern, block, re.M)
    if entries:
        if len(entries) < 4:
            # Standalone entries are cutscene-only continuations, not bases
            # of the four ordinary age/time layers.
            continue
        groups.append([(int(index, 16), scene) for index, scene, _ in entries])
cases = []
for scene_id, name, enum in scenes:
    if name not in manifest['segments']:
        continue
    matched = False
    for age in (1, 0):
        for group in groups:
            # Play_Init adds the age/time layer to the group's base entrance.
            # Market scenes use separate data segments for these variants.
            for night in (0, 1):
                layer = (0 if age else 2) + night
                if group[layer][1] == enum:
                    cases.append((group[0][0], int(scene_id, 16), age,
                                  0 if night else 0x8000, name))
                    matched = True
                    break
            else:
                continue
            break
    assert matched, f'No ordinary entrance layer for {enum}'
cases = cases[args.start:None if args.count is None else args.start + args.count]
assert cases
lines = ['/* Private test selection, generated from the local C tables. */',
         f'#define PS2_SCENE_SMOKE_FRAMES {args.frames}',
         'static const struct { u16 entrance; u8 scene; u8 age; u16 time; const char* name; } sSceneCases[] = {']
lines += [f'    {{0x{entrance:04x}, {scene}, {age}, 0x{time:04x}, "{name}"}},'
          for entrance, scene, age, time, name in cases]
lines.append('};')
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text('\n'.join(lines) + '\n')
print(f'Prepared {len(cases)} scene/age cases starting at {args.start}, {args.frames} frames per case.')
