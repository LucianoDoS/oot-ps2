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
    parser.add_argument('--locale', type=Path)
    parser.add_argument('--native-assets', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    # Include optional language data beside the converted image in a full
    # native USB fixture, including when the existing runner is used.
    if not args.locale and args.data and (args.data.parent/'ESP.DAT').is_file():
        args.locale=args.data.parent/'ESP.DAT'
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
            if args.locale:
                image.writebytes('OOTPS2/ESP.DAT',args.locale.read_bytes())
            if args.native_assets:
                image.writebytes('OOTPS2/PS2DATA.DAT', args.native_assets.read_bytes())
        print('Created private FAT32 image; no physical drive formatted.')
    else:
        if not args.output:
            parser.error('extraction requires --output')
        args.output.mkdir(parents=True, exist_ok=True)
        report = {'data_sha256': None, 'data_unchanged': None}
        with PyFatFS(str(args.image), read_only=True) as image:
            for name in ('BOOT.LOG', 'DIAG.LOG', 'locale-capture-0.ppm', 'locale-capture-1.ppm'):
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
            if args.locale:
                report['locale_unchanged']=image.readbytes('OOTPS2/ESP.DAT')==args.locale.read_bytes()
            if args.native_assets:
                report['native_assets_unchanged'] = image.readbytes('OOTPS2/PS2DATA.DAT') == args.native_assets.read_bytes()
        (args.output / 'image-check.json').write_text(json.dumps(report, indent=2) + '\n')
        if args.data and not report['data_unchanged']:
            raise RuntimeError('The USB driver changed the test data image.')
        if args.locale and not report['locale_unchanged']:
            raise RuntimeError('The USB driver changed the language archive.')
        if args.native_assets and not report['native_assets_unchanged']:
            raise RuntimeError('The USB driver changed the native asset archive.')


if __name__ == '__main__':
    main()
