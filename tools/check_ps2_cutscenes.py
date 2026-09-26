#!/usr/bin/env python3
"""Check mixed-width cutscene conversion against native C field layouts."""
import argparse
from pathlib import Path
import struct
import sys

from ps2_cutscene_layout import cutscene_groups

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--upstream', type=Path, required=True)
args = parser.parse_args()
sys.path.insert(0, str(args.upstream.resolve()))
from tools import csdis

def encode(order):
    pack = lambda layout, *values: struct.pack(order + layout, *values)
    return b''.join((
        pack('II', 6, 600),
        pack('I4H', 1, 0, 10, 100, 0),
        pack('bbHf4h', -1, -7, 32, 45.5, -123, 456, -789, 0),
        pack('II', 10, 1),
        pack('6H6i3f', 2, 10, 40, 0x8000, 5, 0xFFFF, -10, 20, -30,
             40, -50, 60, 0.25, -0.5, 1.0),
        pack('II', 86, 1),
        pack('BB3H10I', 0, 0x62, 10, 100, 0, *([0] * 10)),
        pack('II', 140, 1),
        pack('3HBBI', 0, 20, 30, 18, 45, 0),
        pack('II6H', 19, 1, 0x1234, 10, 20, 1, 0x5678, 0x9ABC),
        pack('II4H', 45, 1, 3, 50, 80, 0),
        pack('II', 0xFFFFFFFF, 0),
    ))

original = encode('>')
converted = bytearray(original)
for start, end, width in cutscene_groups(original, 0, len(original), csdis):
    for offset in range(start, end, width):
        converted[offset:offset + width] = original[offset:offset + width][::-1]
assert converted == encode('<'), 'Native field mismatch in mixed-width cutscene'
try:
    cutscene_groups(original, 0, len(original) - 4, csdis)
except ValueError:
    pass
else:
    raise AssertionError('Truncated cutscene was accepted')
print('PASS: camera, actor cue, sequence, time, text, transition and truncation')
