#!/usr/bin/env python3
"""Select every referenced room from private converted data, with test-only anchors."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--image', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--start', type=int, default=0)
p.add_argument('--count', type=int)
p.add_argument('--frames', type=int, default=45)
a = p.parse_args()
assert a.start >= 0 and a.frames >= 30 and (a.count is None or a.count > 0)
project = Path(__file__).resolve().parents[1]
a.output.parent.mkdir(parents=True, exist_ok=True)
entrances = a.output.with_name('room_smoke_entrances.h')
subprocess.run([sys.executable, str(project / 'tools/generate_ps2_scene_smoke.py'),
                '--image', str(a.image), '--output', str(entrances)], check=True)
variants = {}
for ent, scene, age, time, name in re.findall(
        r'\{0x([0-9a-f]+), (\d+), (\d+), 0x([0-9a-f]+), "(\w+)"\}', entrances.read_text()):
    variants.setdefault(name, []).append((int(ent, 16), int(scene), int(age), int(time, 16)))
manifest = json.loads(a.image.with_suffix('.json').read_text())
assert manifest['profile'] == 'full' and manifest['described_data_complete']
data = a.image.read_bytes()
assert hashlib.sha256(data).hexdigest() == manifest['output_sha256']
ranges = {name: int(start, 16) for name, start in re.findall(
    r'PROVIDE\(_(\w+)SegmentRomStart = 0x([0-9A-Fa-f]+)\);',
    (a.image.parent / 'generated/rom_segments.ld').read_text())}

def resources(segment, kind):
    return [r for r in manifest['segments'][segment] if r['kind'] == kind]

def records(segment, resource, fmt):
    base = ranges[segment] + resource['offset']
    size = struct.calcsize(fmt)
    assert resource['size'] % size == 0
    return [struct.unpack_from(fmt, data, pos) for pos in range(base, base + resource['size'], size)]

cases = []
adult_scenes = {3, 4, 5, 7, 10, 11, 12, 13, 14, 15, 20, 21, 22, 24, 25, 26}
for name, choices in variants.items():
    choice = next((v for v in choices if v[2] == (0 if v[1] in adult_scenes else 1)), choices[0])
    entrance, scene, age, time = choice
    lists = resources(name, 'RoomListResource')
    assert lists and len({r['size'] for r in lists}) == 1, f'Variant room counts differ: {name}'
    rooms = records(name, lists[0], '<2I')
    player_lists = resources(name, 'ActorEntryListResource')
    players = records(name, player_lists[0], '<8h') if player_lists else []
    spawns = resources(name, 'SpawnListResource')
    spawns = records(name, spawns[0], '<2B') if spawns else []
    doors = resources(name, 'TransitionActorEntryListResource')
    doors = records(name, doors[0], '<4b6h') if doors else []
    for room, (start, end) in enumerate(rooms):
        segment = next((n for n in manifest['segments'] if ranges.get(n) == start and '_room_' in n), None)
        assert segment and start < end <= len(data), f'Unknown room {name}:{room}'
        anchor, source = (0, 0, 0, 0), 'default'
        for index, spawn_room in spawns:
            if spawn_room == room and index < len(players):
                _, x, y, z, _, yaw, _, _ = players[index]
                anchor, source = (x, y, z, yaw), 'spawn'
                break
        if source == 'default':
            for front, _, back, _, _, x, y, z, yaw, _ in doors:
                if room in (front, back):
                    angle = yaw * math.pi / 32768
                    sign = -1 if front == room else 1
                    anchor = (round(x + sign * 280 * math.sin(angle)), y,
                              round(z + sign * 280 * math.cos(angle)), yaw)
                    source = 'door'
                    break
        if source == 'default':
            actors = resources(segment, 'ActorEntryListResource')
            if actors and actors[0]['size']:
                _, x, y, z, _, yaw, _, _ = records(segment, actors[0], '<8h')[0]
                anchor, source = (x, y + 30, z, yaw), 'actor'
        cases.append(dict(case=len(cases), entrance=entrance, scene=scene, age=age, time=time,
                          room=room, segment=segment, scene_segment=name,
                          anchor=anchor, anchor_source=source, frames=a.frames))
total = len(cases)
cases = cases[a.start:None if a.count is None else a.start + a.count]
assert cases
lines = ['/* Private generated room fixtures. Never linked into the normal game. */',
         f'#define PS2_ROOM_SMOKE_FRAMES {a.frames}',
         'static const struct { u16 entrance; u8 scene, age; u16 time; u8 room, anchored;',
         '    s16 x, y, z, yaw; const char* name; } sRoomCases[] = {']
for c in cases:
    x, y, z, yaw = c['anchor']
    assert all(-32768 <= v <= 32767 for v in (x, y, z, yaw))
    lines.append(f'    {{0x{c["entrance"]:04x}, {c["scene"]}, {c["age"]}, 0x{c["time"]:04x}, '
                 f'{c["room"]}, {int(c["anchor_source"] != "default")}, {x}, {y}, {z}, {yaw}, "{c["segment"]}"}},')
lines.append('};')
a.output.write_text('\n'.join(lines) + '\n')
a.output.with_suffix('.json').write_text(json.dumps(dict(total_rooms=total, start=a.start,
    image_sha256=manifest['output_sha256'], scope='One selected age per room; synthetic stationary anchors',
    cases=cases), indent=2) + '\n')
print(f'Prepared {len(cases)} of {total} rooms, starting at {a.start}.')
