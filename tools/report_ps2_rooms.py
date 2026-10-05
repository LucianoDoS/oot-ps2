#!/usr/bin/env python3
"""Make a local gallery and contact sheets from the diagnostic's GS captures."""
import argparse
import html
import json
from pathlib import Path
from PIL import Image, ImageDraw

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--report', type=Path, required=True)
a = p.parse_args()
report = json.loads(a.report.read_text())
folder = a.report.parent
cards = []
cases = report['cases']
for case in cases:
    path = Path(case['image'])
    assert path.parent.resolve() == folder.resolve(), 'Capture is outside its report directory'
    image = Image.open(path)
    assert image.size == (320, 240)
    png = path.with_suffix('.png')
    image.save(png)
    label = f'{case["case"]}: {case["segment"]} / {"niño" if case["age"] else "adulto"}'
    summary = f'{case["actors"]} actores, {case["objects"]} objetos, {case["frames"]} fotogramas'
    flag = 'review' if case['review_image'] else 'normal'
    cards.append(f'<article data-flag="{flag}"><a href="{html.escape(png.name)}">'
                 f'<img loading="lazy" src="{html.escape(png.name)}" alt="{html.escape(label)}"></a>'
                 f'<b>{html.escape(label)}</b><p>{html.escape(summary)}</p>'
                 f'<p>{"Revisar: pocos colores o imagen casi negra" if case["review_image"] else "Carga y comprobaciones correctas"}</p></article>')
for start in range(0, len(cases), 24):
    sheet = Image.new('RGB', (4 * 320, 6 * 264), '#17202b')
    draw = ImageDraw.Draw(sheet)
    for i, case in enumerate(cases[start:start + 24]):
        x, y = (i % 4) * 320, (i // 4) * 264
        sheet.paste(Image.open(case['image']), (x, y))
        draw.text((x + 4, y + 242), f'{case["case"]}: {case["segment"]}', fill='white')
    sheet.save(folder / f'contact-{start // 24:02}.jpg', quality=88)
page = '''<!doctype html><meta charset="utf-8"><title>OoT PS2 room captures</title>
<style>body{font:16px system-ui;background:#101720;color:#eee;margin:24px}header{max-width:950px}
#gallery{display:grid;grid-template-columns:repeat(auto-fill,minmax(320px,1fr));gap:16px}
article{background:#202c3b;padding:10px;overflow-wrap:anywhere}img{width:100%;image-rendering:pixelated}
b,p{display:block;margin:8px 0}input,select{font:inherit;padding:8px;margin:10px 5px 20px 0}</style>
<header><h1>OoT PS2 — salas y capturas</h1><p>Las pruebas verifican carga, actualización, actores, objetos y funciones de eventos.
No certifican puzles, victoria sobre jefes ni fidelidad visual. Las imágenes casi negras o con pocos colores se señalan para revisión.</p>
<input id="search" placeholder="Buscar sala o escena"><select id="filter"><option value="all">Todas</option>
<option value="review">Imágenes para revisar</option></select><p id="count"></p></header><main id="gallery">'''
status = html.escape(report['status'])
failed = html.escape(str(report.get('failed_cases', [])))
page += f'<p>Resultado del recorrido: <strong>{status}</strong>. Salas completas: {report["rooms"]}/{report["total_referenced_rooms"]}. Casos pendientes o fallidos: {failed}.</p>'
page += '\n'.join(cards)
page += '''</main><script>
const cards=[...document.querySelectorAll('article')],search=document.querySelector('#search'),filter=document.querySelector('#filter');
function update(){let n=0;for(const c of cards){const visible=c.textContent.toLowerCase().includes(search.value.toLowerCase())&&(filter.value==='all'||c.dataset.flag==='review');c.hidden=!visible;if(visible)n++;}document.querySelector('#count').textContent=n+' salas visibles / '+cards.length;}
search.addEventListener('input',update);filter.addEventListener('change',update);update();</script>'''
(folder / 'index.html').write_text(page, encoding='utf-8')
print(f'Gallery: {(folder / "index.html").resolve()}')
