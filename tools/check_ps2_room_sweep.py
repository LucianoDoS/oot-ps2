#!/usr/bin/env python3
"""Validate runtime room/event results and each native GS capture against the selected fixtures."""
import argparse
import hashlib
import json
from pathlib import Path
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--log', type=Path, required=True)
p.add_argument('--cases', type=Path, required=True)
p.add_argument('--captures', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--allow-partial', action='store_true', help='Preserve verified completed captures after a failure; never report PASS')
a = p.parse_args()
fixtures = json.loads(a.cases.read_text())
text = a.log.read_text(errors='replace')
cases = fixtures['cases']
faults = re.findall(r'[^\r\n]*(?:\[PS2 [^\r\n]*\] FAIL\b|\[PS2 FAULT\]|TLB Miss|Address ?Error|branch in delay slot)[^\r\n]*', text)
if not a.allow_partial:
    assert not faults, 'Runtime failure'
header = re.search(r'\[PS2 ROOM SWEEP\] cases=(\d+) frames=(\d+)', text)
assert header and int(header[1]) == len(cases)
result = re.search(r'\[PS2 ROOM SWEEP RESULT\] PASS cases=(\d+) failures=0', text)
if not a.allow_partial:
    assert result and int(result[1]) == len(cases), 'Sweep incomplete'
begins = re.findall(r'\[PS2 ROOM SWEEP\] BEGIN case=(\d+) scene=(\d+) age=(\d+) room=(\d+) name=(\w+)', text)
passes = re.findall(r'\[PS2 ROOM SWEEP\] PASS case=(\d+) scene=(\d+) room=(\d+) frames=(\d+) capture=(\d+)', text)
events = re.findall(r'\[PS2 ROOM EVENTS\] PASS case=(\d+) equipment=2 rupees=2 switches=64 chests=32 restored=1 failures=0', text)
residents = re.findall(r'\[PS2 ROOM RESIDENTS\] PASS case=(\d+) actors=(\d+) objects=(\d+) failures=0', text)
captures = re.findall(r'\[PS2 ROOM CAPTURE\] PASS case=(\d+) file=host:(room-capture-\d+\.ppm)', text)
incomplete = bool(faults) or not result or len(passes) != len(cases)
failed_cases = [cases[int(b[0])]['case'] for b in begins[len(passes):]]
if a.allow_partial:
    assert begins, 'No room was started'
    assert len(begins) >= len(passes) and len(events) >= len(passes) and len(residents) >= len(passes) and len(captures) >= len(passes)
    cases, begins, events, residents, captures = (v[:len(passes)] for v in (cases, begins, events, residents, captures))
else:
    assert len(begins) == len(passes) == len(events) == len(residents) == len(captures) == len(cases)
rows = []
for i, (case, begin, end, event, resident, capture) in enumerate(zip(cases, begins, passes, events, residents, captures, strict=True)):
    idx, scene, age, room, segment = begin
    assert tuple(map(int, (idx, scene, age, room))) == (i, case['scene'], case['age'], case['room']) and segment == case['segment']
    idx, scene, room, frames, done = map(int, end)
    assert (idx, scene, room, done) == (i, case['scene'], case['room'], 1) and frames >= case['frames']
    assert int(event) == int(resident[0]) == int(capture[0]) == i
    image_path = a.captures / capture[1]
    raw = image_path.read_bytes()
    assert raw.startswith(b'P6\n320 240\n255\n') and len(raw) == 15 + 320 * 240 * 3, 'Invalid/partial capture'
    pixels = raw[15:]
    unique = len(set(zip(pixels[0::3], pixels[1::3], pixels[2::3])))
    black = sum(1 for x in range(0, len(pixels), 3) if max(pixels[x:x+3]) < 8) / 76800
    # Coverage metrics flag suspect images for review, not visual correctness.
    rows.append(dict(case=case['case'], scene=scene, room=room, age=int(age), segment=segment,
                     geometry_intentionally_hidden=bool(re.search(rf'\[PS2 ROOM GEOMETRY\] case={i} hidden=1 intentional=1', text)),
                     anchor=case['anchor'], anchor_source=case['anchor_source'],
                     frames=frames, actors=int(resident[1]), objects=int(resident[2]),
                     image=str(image_path.resolve()), image_sha256=hashlib.sha256(raw).hexdigest(),
                     unique_colors=unique, black_fraction=round(black, 4), review_image=unique < 32 or black > 0.65))
report = dict(status='FAIL' if faults else ('INCOMPLETE' if incomplete else 'PASS'), rooms=len(rows), scenes=len({r['scene'] for r in rows}),
              failed_cases=failed_cases, runtime_failures=faults,
              total_referenced_rooms=fixtures['total_rooms'], image_sha256=fixtures['image_sha256'],
              scope='Room initialization/update/draw, actor/object residency, equipment/rupee and switch/chest flag APIs, GS captures',
              full_progression_validated=False, puzzles_and_boss_victories_validated=False,
              visual_correctness_validated=False, real_ps2_validated=False, cases=rows)
a.output.write_text(json.dumps(report, indent=2) + '\n')
print(f'{report["status"]}: {len(rows)} completed rooms verified; {report["scenes"]} scenes; failed cases={failed_cases}.')
