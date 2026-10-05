#!/usr/bin/env python3
"""Private FAT32 fixture for PCSX2's USB device (requires pyfatfs)."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

from pyfatfs.PyFat import PyFat
from pyfatfs.PyFatFS import PyFatFS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['create', 'extract'])
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--data', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.mode == 'create':
        if not args.data or args.image.exists():
            parser.error('creation requires --data and a new image path')
        args.image.parent.mkdir(parents=True, exist_ok=True)
        with args.image.open('xb') as image:
            image.truncate(128 * 1024 * 1024)
        fat = PyFat()
        fat.mkfs(str(args.image), fat_type=PyFat.FAT_TYPE_FAT32,
                 size=128 * 1024 * 1024, label='OOTTEST')
        fat.close()
        with PyFatFS(str(args.image)) as image:
            image.makedir('OOTPS2')
            with args.data.open('rb') as source, image.openbin('OOTPS2/OOT.Z64', 'w') as dest:
                shutil.copyfileobj(source, dest)
        print('Created private FAT32 image; no physical drive formatted.')
    else:
        if not args.output:
            parser.error('extraction requires --output')
        args.output.mkdir(parents=True, exist_ok=True)
        report = {'data_sha256': None, 'data_unchanged': None}
        with PyFatFS(str(args.image), read_only=True) as image:
            for name in ('BOOT.LOG', 'DIAG.LOG'):
                path = 'OOTPS2/' + name
                if image.exists(path):
                    data = image.readbytes(path)
                    (args.output / name).write_bytes(data)
                    print(f'{name}: {len(data)} bytes')
            if args.data:
                digest = hashlib.sha256()
                with image.openbin('OOTPS2/OOT.Z64', 'r') as source:
                    for chunk in iter(lambda: source.read(1024 * 1024), b''):
                        digest.update(chunk)
                with args.data.open('rb') as source:
                    expected = hashlib.file_digest(source, 'sha256').hexdigest()
                report['data_sha256'] = digest.hexdigest()
                report['data_unchanged'] = digest.hexdigest() == expected
        (args.output / 'image-check.json').write_text(json.dumps(report, indent=2) + '\n')
        if args.data and not report['data_unchanged']:
            raise RuntimeError('The USB driver changed the test data image.')


if __name__ == '__main__':
    main()
