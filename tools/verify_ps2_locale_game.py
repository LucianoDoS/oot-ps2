"""Check the native USB locale fixture and that its option row was rendered."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from PIL import Image


def verify(root, sparse_profile=False):
    usb=json.loads((root/'report.json').read_text(encoding='utf-8-sig'))
    image_check=json.loads((root/'image-check.json').read_text())
    log=(root/'runtime.log').read_text(encoding='utf-8',errors='replace')
    assert usb['status']=='PASS' and not usb['hostFs']
    assert image_check['data_unchanged'] and image_check['locale_unchanged']
    for marker in ('LOGO CADENCE','LANGUAGE MENU','LANGUAGE MESSAGE','LANGUAGE GAME RESULT'):
        assert f'[PS2 {marker}] PASS' in log, f'Missing {marker}'
    for i in range(3):
        assert re.search(rf'\[PS2 LANGUAGE TRANSITION\] PASS phase={i} .*frames=120',log)
    assert '[PS2 RUNTIME LOG] CLOSED final flush completed' in log
    runtime=(root/'DIAG.LOG').read_text(encoding='utf-8',errors='replace')
    automatic='AUTO-PLAY' in runtime.splitlines()[0]
    if sparse_profile:
        assert automatic, 'Sparse profiling must use the automatic logger without HUD'
    transfer=re.search(r'LOGGER_START .*\btransferBytes=(\d+)',runtime)
    if automatic:
        assert re.search(r'LOGGER_START .*batchSeconds=15 .*reserveRamBytes=',runtime)
        assert 'FPS-HUD' not in runtime.splitlines()[0]
        profiles=[int(frame) for frame in re.findall(r'\[PS2 GFX PROFILE\] frame=(\d+)',runtime)]
        if sparse_profile:
            assert '[PS2 PERF SAMPLING] every=300 no-HUD automatic logger' in runtime
            assert profiles and all(frame % 300 == 0 for frame in profiles)
        else:
            assert not profiles, 'Automatic logging must not profile individual triangle work'
            assert '[PS2 PERF SAMPLING]' not in runtime
    assert not re.search(r'\[PS2 (?:LOGO CADENCE|LANGUAGE[^\]]*)\] FAIL',log)
    counts=[]
    captures={}
    for i in (0,1):
        path=root/f'locale-capture-{i}.ppm'
        with Image.open(path) as screenshot:
            assert screenshot.size==(320,240)
            screenshot.save(path.with_suffix('.png'))
            # Caption and dialog white pixels distinguish a rendered label
            # from the blank row found during the first successful input test.
            box=(54,155,125,173) if i==0 else (70,172,260,224)
            count=sum(r>160 and g>160 for r,g,b in screenshot.convert('RGB').crop(box).getdata())
            assert count>(30 if i==0 else 100), 'Locale text absent from capture'
            counts.append(count)
        captures[path.name]=hashlib.sha256(path.read_bytes()).hexdigest()
    report=dict(status='PASS',usb_fixture=str(root),logo_fps10=int(re.search(
        r'\[PS2 LOGO CADENCE\] PASS .*fps10=(\d+)',log)[1]),
        english_save_slots_unchanged=True,locale_text_pixels=counts,captures=captures,
        automatic_runtime_log=automatic,log_flush_seconds=15 if automatic else 5,
        sparse_graphics_profile=sparse_profile,
        log_transfer_bytes=int(transfer[1]) if transfer else 512,
        real_ps2_validated=False,physical_language_persistence_validated=False)
    (root/'language-game-check.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root',type=Path)
    parser.add_argument('--sparse-profile',action='store_true')
    args=parser.parse_args()
    verify(args.root,args.sparse_profile)
