#!/usr/bin/env python3
"""Build a private USB folder and check the ELF load ranges/data pairing."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct


def inspect_elf(path):
    data = path.read_bytes()
    assert data[:7] == b'\x7fELF\x01\x01\x01', 'Expected a 32-bit little-endian ELF'
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    _, kind, machine, _, entry, phoff, _, _, _, phsize, phnum, *_ = header
    assert kind == 2 and machine == 8 and phsize == 32 and phnum > 0
    assert phoff + phnum * phsize <= len(data)
    segments = []
    for i in range(phnum):
        typ, off, virtual, physical, file_size, memory_size, flags, align = struct.unpack_from('<8I', data, phoff + i * phsize)
        if typ != 1:
            continue
        assert file_size <= memory_size and off + file_size <= len(data)
        assert virtual == physical and 0x100000 <= virtual < virtual + memory_size <= 0x2000000, 'ELF outside EE RAM'
        segments.append(dict(address=virtual, end=virtual + memory_size, file_bytes=file_size,
                             memory_bytes=memory_size, executable=bool(flags & 1)))
    assert segments and any(s['executable'] and s['address'] <= entry < s['end'] for s in segments)
    assert b'HOST TEST: physical USB is not exercised' not in data, 'Host-only test ELF cannot be packaged'
    assert b'[PS2 USB MODULE]' in data, 'Missing embedded USB boot support'
    end = max(s['end'] for s in segments)
    return dict(entry=entry, segments=segments, static_end=end, bytes_above_static_end=0x2000000 - end,
                limitation='Static ELF ranges only; dynamic heap, stacks and IOP/GS usage need console testing')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir', type=Path, default=Path('build/PS2-ntsc12'))
    p.add_argument('--output', type=Path, default=Path('build/PS2/usb-ready/OOTPS2'))
    a = p.parse_args()
    image = a.build_dir / 'oot-full-next.z64'
    manifest = json.loads(image.with_suffix('.json').read_text())
    expected = json.loads((a.build_dir / 'generated/usb_data_expected.json').read_text())
    image_hash = hashlib.sha256(image.read_bytes()).hexdigest()
    assert image_hash == manifest['output_sha256'] == expected['image_sha256']
    files = {'OOTPS2.ELF': a.build_dir / 'oot_ps2_usb.elf',
             'BOOTTEST.ELF': a.build_dir / 'oot_ps2_usb_diagnostic.elf', 'OOT.Z64': image}
    memory = {name: inspect_elf(path) for name, path in files.items() if name.endswith('.ELF')}
    a.output.mkdir(parents=True, exist_ok=True)
    hashes = {}
    for name, source in files.items():
        shutil.copyfile(source, a.output / name)
        hashes[name] = hashlib.sha256((a.output / name).read_bytes()).hexdigest()
        assert hashes[name] == hashlib.sha256(source.read_bytes()).hexdigest()
    report = dict(status='PACKAGED', real_ps2_validated=False, data=expected, elf_memory=memory, files=hashes)
    (a.output / 'package.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    (a.output / 'HASHES.sha256').write_text(''.join(f'{digest}  {name}\n' for name, digest in hashes.items()), encoding='ascii')
    shutil.copyfile('PS2_HARDWARE.md', a.output / 'LEEME.txt')
    shutil.copyfile('BUGS_PS2.md', a.output / 'BUGS.txt')
    print(f'USB package: {a.output.resolve()}')
    print(f'Game static memory end: 0x{memory["OOTPS2.ELF"]["static_end"]:08x}')


if __name__ == '__main__':
    main()
