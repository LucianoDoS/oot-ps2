#!/usr/bin/env python3
"""Require a complete, exception-free runtime scene sweep log."""
import argparse
import json
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--log', type=Path, required=True)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
text = args.log.read_text(errors='replace')
header = re.search(r'\[PS2 SCENE SWEEP\] cases=(\d+) frames=(\d+)', text)
assert header, 'Missing scene sweep header'
case_count, frames = map(int, header.groups())
assert not re.search(r'TLB Miss|Address Error|branch in delay slot|\[PS2 FAULT\]|\[PS2 SCENE SWEEP\] FAIL', text), \
    'Runtime fault in scene sweep'
result = re.search(r'\[PS2 SCENE SWEEP RESULT\] PASS cases=(\d+) failures=0', text)
assert result and int(result[1]) == case_count, 'Sweep incomplete or failed'
begin = re.findall(r'\[PS2 SCENE SWEEP\] BEGIN case=(\d+) scene=(\d+) age=(\d+) entrance=([0-9a-f]+) name=(\w+)', text)
passed = re.findall(r'\[PS2 SCENE SWEEP\] PASS case=(\d+) expected=(\d+) actual=(\d+) age=(\d+) frames=(\d+) room=(-?\d+)', text)
assert len(begin) == len(passed) == case_count
cases = []
for index, (start, finish) in enumerate(zip(begin, passed, strict=True)):
    case, scene, age, entrance, name = start
    pcase, expected, actual, page, pframes, room = map(int, finish)
    assert int(case) == pcase == index
    assert int(scene) == expected == actual and int(age) == page
    assert pframes >= frames and room >= 0
    cases.append({'case': index, 'scene': actual, 'age': 'child' if page else 'adult',
                  'entrance': int(entrance, 16), 'segment': name, 'frames': pframes, 'room': room})
report = {'test_scope': 'Initial entrance, fixed frames, debug save and no controller input',
          'case_count': case_count, 'scene_count': len({c['scene'] for c in cases}),
          'full_progression_validated': False, 'cases': cases}
overlay = re.search(r'\[PS2 ACTOR OVERLAY TEST\] banks=(\d+) dataBytes=(\d+) bssBytes=(\d+) failures=(\d+)', text)
if overlay:
    banks, data_bytes, bss_bytes, failures = map(int, overlay.groups())
    assert banks > 0 and data_bytes > 0 and bss_bytes > 0 and failures == 0, 'Actor overlay reset failed'
    report['actor_overlay_reset'] = {'banks': banks, 'data_bytes': data_bytes, 'bss_bytes': bss_bytes}
if args.output:
    args.output.write_text(json.dumps(report, indent=2) + '\n')
print(f'PASS: {case_count} runtime cases, {report["scene_count"]} scenes, at least {frames} frames per case; no CPU faults.')
print('NOTE: does not test every room, puzzle, fight or complete progression.')
