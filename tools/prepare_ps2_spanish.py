#!/usr/bin/env python3
"""Extract private Spanish messages and described textures for the PS2 port.

Inputs are an original and translated decompressed NTSC 1.0 ROM. Only message
bytes and explicit texture resources are imported; N64 code and geometry are
never copied into the native port. The archive is generated build data.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import zlib


def menu_labels(font_path):
    """Rasterize whole words at menu size, with one consistent font.

    Labels are private build assets. The system font stays outside the repo.
    IA8 stores the white ink, dark outline and antialiased coverage separately.
    """
    from PIL import Image, ImageDraw, ImageFont
    scale = 4
    labels = []
    for i, text in enumerate(('LANGUAGE', 'IDIOMA', 'English', 'Español')):
        # The original option headings are one pixel smaller than the choices.
        font = ImageFont.truetype(str(font_path), (11 if i < 2 else 12)*scale)
        ink = Image.new('L', (96*scale, 16*scale))
        alpha = Image.new('L', ink.size)
        position = (4*scale, 11*scale)
        box = font.getbbox(text, anchor='ls', stroke_width=scale)
        if position[0]+box[2] > 94*scale or position[1]+box[3] > 16*scale:
            raise ValueError(f'Menu label does not fit: {text}')
        ImageDraw.Draw(ink).text(position, text, font=font, anchor='ls', fill=255)
        ImageDraw.Draw(alpha).text(position, text, font=font, anchor='ls',
                                  fill=255, stroke_width=scale, stroke_fill=255)
        ink = ink.resize((96,16), Image.Resampling.LANCZOS)
        alpha = alpha.resize((96,16), Image.Resampling.LANCZOS)
        pixels = bytearray()
        for value, coverage in zip(ink.tobytes(), alpha.tobytes()):
            intensity = min(15, round(value*15/coverage)) if coverage else 0
            pixels.append(intensity << 4 | round(coverage*15/255))
        labels.append(pixels)
    return labels



def build(original, translated, upstream, manifest_path, output, menu_font):
    sys.path.insert(0, str(upstream))
    from tools import version_config, dmadata
    old = version_config.load_version_config('ntsc-1.0')
    new = version_config.load_version_config('ntsc-1.2')
    from tools.assets.descriptor import base as asset_desc
    from tools.assets.descriptor.n64resources import TextureResourceDesc
    def texture_symbols(config):
        for asset in config.assets:
            asset.xml_path = upstream / asset.xml_path
        return {resource.symbol_name: (collection.backing_memory.name,resource.offset,
                resource.get_size(),resource.width,resource.height,resource.format.name)
            for pool in asset_desc.get_resources_desc(config) for collection in pool.collections
            if isinstance(collection.backing_memory,asset_desc.BaseromFileBackingMemory)
            for resource in collection.resources if isinstance(resource,TextureResourceDesc)}
    old_symbols,new_symbols=texture_symbols(old),texture_symbols(new)
    base = original.read_bytes()
    spanish = translated.read_bytes()
    target = (upstream / 'baseroms/ntsc-1.2/baserom-decompressed.z64').read_bytes()
    if len(base) != len(spanish) or base[:4] != bytes.fromhex('80371240'):
        raise ValueError('Expected paired decompressed NTSC 1.0 ROMs')
    old_entries = dict(zip(old.dmadata_segments, dmadata.read_dmadata(memoryview(base), old.dmadata_start)))
    new_entries = dict(zip(new.dmadata_segments, dmadata.read_dmadata(memoryview(target), new.dmadata_start)))
    code = old_entries['code']
    table = code.vrom_start + old.variables['sNesMessageEntryTable'] - old.dmadata_segments['code'].vram
    messages = []
    text = old_entries['nes_message_data_static']
    for i in range(4096):
        text_id, type_pos, _, address = struct.unpack_from('>HBBI', spanish, table + i * 8)
        if text_id in (0xFFFD, 0xFFFF):
            break
        next_address = struct.unpack_from('>I', spanish, table + (i + 1) * 8 + 4)[0]
        offset, length = address & 0xFFFFFF, next_address - address
        if not 0 < length <= 1280 or offset + length > text.vrom_end - text.vrom_start:
            raise ValueError(f'Invalid Spanish message {text_id:04x}')
        payload = spanish[text.vrom_start + offset:text.vrom_start + offset + length]
        messages.append((text_id, type_pos, payload))
    else:
        raise ValueError('Message table terminator missing')
    if len(messages) < 2000 or len({m[0] for m in messages}) != len(messages):
        raise ValueError('Incomplete or duplicate message table')
    textures = []
    imported = []
    native = json.loads(manifest_path.read_text())
    for name, resources in native['segments'].items():
        if name not in old_entries or name not in new_entries:
            continue
        a, b = old_entries[name], new_entries[name]
        if base[a.vrom_start:a.vrom_end] == spanish[a.vrom_start:a.vrom_end]:
            continue
        for r in resources:
            if r['kind'] not in ('TextureResource', 'TLUTResource', 'TextureSplitTlutResource'):
                continue
            offset, size = r['offset'], r['size']
            if offset + size > min(a.vrom_end - a.vrom_start, b.vrom_end - b.vrom_start):
                raise ValueError(f'Texture layout mismatch: {name}/{r["symbol"]}')
            before = base[a.vrom_start + offset:a.vrom_start + offset + size]
            after = spanish[a.vrom_start + offset:a.vrom_start + offset + size]
            if before != after:
                # Match named resource locations in both revision descriptors.
                # Their English pixels can differ between revisions.
                identity=old_symbols.get(r['symbol'])
                if not identity or identity[:3]!=(name,offset,size) or identity!=new_symbols.get(r['symbol']):
                    raise ValueError(f'Revision-specific texture layout: {name}/{r["symbol"]}')
                textures.append((b.vrom_start + offset, after))
                imported.append(dict(segment=name, symbol=r['symbol'], bytes=size))
    # Font is a byte-only DMA segment; extraction can describe it as a blob.
    font_old, font_new = old_entries['nes_font_static'], new_entries['nes_font_static']
    font = spanish[font_old.vrom_start:font_old.vrom_end]
    if len(font) != font_new.vrom_end - font_new.vrom_start:
        raise ValueError('Incompatible font extent')
    textures = [(v, p) for v, p in textures if not font_new.vrom_start <= v < font_new.vrom_end]
    textures.append((font_new.vrom_start, font))
    messages.sort()
    textures.sort()
    for (v, p), (next_v, _) in zip(textures, textures[1:]):
        if v + len(p) > next_v:
            raise ValueError('Overlapping translation texture resources')
    source = Path('src/code/z_message.c').read_text(encoding='utf-8')
    widths_text = source.split('f32 sFontWidths[144] = {', 1)[1].split('};', 1)[0]
    widths_text = re.sub(r'#if OOT_NTSC\s*(.*?)#else.*?#endif',r'\1',widths_text,flags=re.S)
    widths = [float(x) for x in re.findall(r'(\d+\.\d+)f', widths_text)]
    if len(widths) != 144:
        raise ValueError('Font width count mismatch')
    for char, width in [(0x3C, 6), (0x5C, 9), (0x82, 10), (0x8B, 6)]:
        widths[char - 32] = width

    message_offset = 48
    texture_offset = message_offset + len(messages) * 12
    width_offset = texture_offset + len(textures) * 12
    label_offset = width_offset + 144 * 4
    payload_offset = label_offset + 4 * 96 * 16
    archive = bytearray(payload_offset)
    struct.pack_into('<144f', archive, width_offset, *widths)
    for i, label in enumerate(menu_labels(menu_font)):
        archive[label_offset + i * 1536:label_offset + (i + 1) * 1536] = label
    for i, (text_id, type_pos, payload) in enumerate(messages):
        offset = len(archive)
        archive.extend(payload)
        archive.extend(bytes(-len(archive) % 4))
        struct.pack_into('<HBBII', archive, message_offset + i * 12, text_id, type_pos, 0, offset, len(payload))
    for i, (vrom, payload) in enumerate(textures):
        offset = len(archive)
        archive.extend(payload)
        archive.extend(bytes(-len(archive) % 4))
        struct.pack_into('<III', archive, texture_offset + i * 12, vrom, offset, len(payload))
    struct.pack_into('<8s10I', archive, 0, b'OOTESP01', 2, len(archive), len(messages), len(textures),
                     message_offset, texture_offset, width_offset, label_offset, payload_offset, zlib.crc32(archive[48:]))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(archive)
    report = dict(messages=len(messages), textures=len(textures), bytes=len(archive),
                  native_image_sha256=native['output_sha256'],
                  original_sha256=hashlib.sha256(base).hexdigest(), translated_sha256=hashlib.sha256(spanish).hexdigest(),
                  sha256=hashlib.sha256(archive).hexdigest(), imported=imported,
                  source='eduardo_aj2 v2.2 via Patcher64Plus-Tool es.ppf',
                  menu_font_sha256=hashlib.sha256(menu_font.read_bytes()).hexdigest(),
                  limitation='Native hardcoded text and credits require separate translation')
    output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Spanish archive: {len(messages)} messages, {len(textures)} textures, {len(archive)} bytes')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--original', type=Path, required=True)
    p.add_argument('--translated', type=Path, required=True)
    p.add_argument('--upstream', type=Path, required=True)
    p.add_argument('--manifest', type=Path, default=Path('build/PS2-ntsc12/oot-full-next.json'))
    p.add_argument('--output', type=Path, default=Path('build/PS2-ntsc12/ESP.DAT'))
    p.add_argument('--menu-font', type=Path, required=True,
                   help='System sans serif bold font for menu-size IA8 labels (requires Pillow)')
    a = p.parse_args()
    build(a.original, a.translated, a.upstream.resolve(), a.manifest, a.output, a.menu_font)
