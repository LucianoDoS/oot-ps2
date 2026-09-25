#!/usr/bin/env python3
"""Identify local N64 images without modifying them or trusting extensions."""
import argparse
import hashlib
from pathlib import Path


def inspect(path):
    data = bytearray(path.read_bytes())
    magic = bytes(data[:4])
    if magic == bytes.fromhex('37804012'):
        data[0::2], data[1::2] = data[1::2], data[0::2]
        order = 'byte-swapped'
    elif magic == bytes.fromhex('40123780'):
        for i in range(0, len(data), 4):
            data[i:i+4] = data[i:i+4][::-1]
        order = 'little-endian'
    elif magic == bytes.fromhex('80371240'):
        order = 'big-endian'
    else:
        raise ValueError(f'{path}: unrecognized N64 header {magic.hex()}')
    print(f'{path.name}\n  size={len(data)} order={order}')
    print(f'  title={data[0x20:0x34].decode("ascii", errors="replace").strip()} revision={data[0x3f]}')
    print(f'  normalized_sha1={hashlib.sha1(data).hexdigest()}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom', nargs='+', type=Path)
    for path in parser.parse_args().rom:
        inspect(path)
