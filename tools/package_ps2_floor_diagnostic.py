#!/usr/bin/env python3
"""Prepare the optional native floor comparison without replacing the game."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
from package_ps2_usb import inspect_elf


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build/PS2-ntsc12'))
    parser.add_argument('--output', type=Path, default=Path('build/PS2/floor-ready'))
    args = parser.parse_args()
    source = args.build_dir / 'oot_ps2_usb_floor.elf'
    memory = inspect_elf(source)
    assert b'FLOOR_LAB v1' in source.read_bytes(), 'Expected the floor comparison ELF'
    expected = json.loads((args.build_dir / 'generated/usb_data_expected.json').read_text())
    data_sha = hashlib.sha256((args.build_dir / 'oot-full-next.z64').read_bytes()).hexdigest()
    assert data_sha == expected['image_sha256'], 'USB data/header mismatch'
    args.output.mkdir(parents=True, exist_ok=True)
    files = {'FLOOR.ELF': source, 'SUELO.txt': Path('PS2_FLOOR_DIAGNOSTIC.md')}
    hashes = {}
    for name, path in files.items():
        shutil.copyfile(path, args.output / name)
        hashes[name] = hashlib.sha256((args.output / name).read_bytes()).hexdigest()
        assert hashes[name] == hashlib.sha256(path.read_bytes()).hexdigest()
    report = dict(status='PACKAGED', realPS2Validated=False, dataSha256=data_sha,
                  elfMemory=memory, files=hashes)
    (args.output / 'floor-package.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    (args.output / 'FLOOR.sha256').write_text(''.join(f'{sha}  {name}\n' for name, sha in hashes.items()), encoding='ascii')
    print(f'Optional floor diagnostic: {args.output.resolve()}')


if __name__ == '__main__':
    main()
