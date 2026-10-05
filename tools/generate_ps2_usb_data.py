#!/usr/bin/env python3
"""Generate private fingerprints of the converted data, never of a raw ROM."""
import argparse
import hashlib
import json
from pathlib import Path
import zlib


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    data = args.image.read_bytes()
    manifest = json.loads(args.image.with_suffix('.json').read_text(encoding='utf-8'))
    digest = hashlib.sha256(data).hexdigest()
    if manifest.get('output_sha256') != digest or manifest.get('profile') != 'full':
        raise SystemExit('Expected the verified full converted image and its manifest.')
    # Distributed reads catch a wrong/raw/truncated image without reading the
    # entire cartridge over USB 1.1 on every boot. The package also has full SHA256.
    offsets = sorted({0, len(data) - 4096, *(
        ((len(data) - 4096) * i // 11) & ~4095 for i in range(1, 11))})
    samples = [{'offset': offset, 'bytes': 4096,
                'crc32': zlib.crc32(data[offset:offset + 4096])}
               for offset in offsets]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    body = '\n'.join(f'    {{ {s["offset"]}u, {s["bytes"]}u, 0x{s["crc32"]:08x}u }},'
                     for s in samples)
    args.output.write_text(
        '/* Generated from private converted data; do not distribute this header. */\n'
        f'#define PS2_USB_DATA_SIZE {len(data)}u\n'
        f'#define PS2_USB_DATA_SHA256 "{digest}"\n'
        'static const struct { unsigned int offset, bytes, crc32; } sUsbSamples[] = {\n'
        + body + '\n};\n', encoding='utf-8')
    args.output.with_suffix('.json').write_text(json.dumps(
        {'image_sha256': digest, 'bytes': len(data), 'samples': samples,
         'validation': 'size and distributed CRC32 samples; not a full runtime hash'},
        indent=2) + '\n', encoding='utf-8')
    print(f'USB fingerprints: {len(samples)} blocks, {len(data)} bytes')


if __name__ == '__main__':
    main()
