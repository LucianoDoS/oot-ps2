"""Check the native USB locale fixture and that its option row was rendered."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from PIL import Image


def verify(root):
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
        real_ps2_validated=False,physical_language_persistence_validated=False)
    (root/'language-game-check.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root',type=Path)
    verify(parser.parse_args().root)
