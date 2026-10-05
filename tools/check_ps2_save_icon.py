#!/usr/bin/env python3
"""Independently decode/validate generated OSD assets; optionally preview mesh.

References: PS2SDK mcIcon layout and caol64/ps2mc-browser icon parser.
The preview is a software mesh view, not a capture of the BIOS browser.
"""
import argparse
import json
import math
from pathlib import Path
import struct


def check(folder):
    metadata = (folder / "icon.sys").read_bytes()
    assert len(metadata) == 964 and metadata[:4] == b"PS2D", "Invalid icon.sys"
    split = struct.unpack_from("<H", metadata, 6)[0]
    title = metadata[192:260]
    end = next(i for i in range(0, 68, 2) if title[i:i + 2] == b"\0\0")
    assert 0 < split <= end and split % 2 == 0, "Invalid title line break"
    lines = [title[:split].decode("shift_jis"), title[split:end].decode("shift_jis")]
    normalized = ["".join(" " if c == "\u3000" else chr(ord(c) - 0xFEE0) for c in line) for line in lines]
    assert normalized == ["Legend of Zelda", "Ocarina of Time"], "Wrong game title"
    for start in (260, 324, 388):
        assert metadata[start:start + 64].split(b"\0", 1)[0] == b"triforce.icn", "Wrong icon reference"
    lights = struct.unpack_from("<28f", metadata, 80)
    assert all(math.isfinite(f) for f in lights), "Invalid lighting"
    assert metadata[452:] == bytes(512), "Nonzero reserved metadata"
    data = (folder / "triforce.icn").read_bytes()
    magic, shapes, flags, reserved, count = struct.unpack_from("<5I", data)
    assert (magic, shapes, flags, reserved) == (0x10000, 1, 7, 0x3F800000)
    assert count > 0 and count % 3 == 0 and count <= 10000
    assert len(data) == 20 + count * 24 + 36 + 32768, "Wrong model length"
    vertices, normals, colors = [], [], []
    for offset in range(20, 20 + count * 24, 24):
        x, y, z, zero, nx, ny, nz, nzero, u, v, r, g, b, a = struct.unpack_from("<3hH3hH2h4B", data, offset)
        vertices.append((x / 4096, y / 4096, z / 4096))
        normals.append((nx / 4096, ny / 4096, nz / 4096))
        colors.append((r, g, b, a))
        assert zero == nzero == 0 and (u, v) == (0, 0)
        assert abs(sum(n * n for n in normals[-1]) - 1) < 0.001, "Nonunit normal"
        assert r > g > b and a == 255, "Gold opaque material expected"
    offset = 20 + count * 24
    assert struct.unpack_from("<IIfII", data, offset) == (1, 31, 1.0, 0, 1)
    assert struct.unpack_from("<II2f", data, offset + 20) == (0, 1, 0.0, 1.0)
    assert data[offset + 36:] == b"\xff\x7f" * 16384
    for i in range(0, count, 3):
        ab = [vertices[i + 1][a] - vertices[i][a] for a in range(3)]
        ac = [vertices[i + 2][a] - vertices[i][a] for a in range(3)]
        cross = [ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0]]
        assert sum(c * n for c, n in zip(cross, normals[i])) > 0, "Winding/normal mismatch"
    bounds = [(min(p[a] for p in vertices), max(p[a] for p in vertices)) for a in range(3)]
    assert 0.2 < bounds[2][1] - bounds[2][0] < 1 and abs(bounds[1][1] - bounds[1][0] - 3) < 0.001
    report = {"result": "PASS", "title": normalized, "triangles": count // 3,
              "bytes": len(data), "bounds": bounds, "bios_display_validated": False}
    return report, vertices, normals, colors


def preview(path, vertices, normals, colors):
    from PIL import Image, ImageDraw
    size, scale = 512, 130
    background = (22, 38, 68)
    image = Image.new("RGB", (size, size), background)
    draw = ImageDraw.Draw(image)
    angle = math.radians(24)
    def rotate(v):
        return (v[0] * math.cos(angle) + v[2] * math.sin(angle), v[1],
                -v[0] * math.sin(angle) + v[2] * math.cos(angle))
    transformed = list(map(rotate, vertices))
    faces = list(range(0, len(vertices), 3))
    faces.sort(key=lambda i: sum(p[2] for p in transformed[i:i + 3]), reverse=True)
    for i in faces:
        normal = rotate(normals[i])
        if normal[2] >= 0:
            continue
        shade = 0.4 + 0.6 * max(0, sum(a * b for a, b in zip(normal, (-0.3, -0.4, -0.85))))
        color = tuple(min(255, round(c * shade)) for c in colors[i][:3])
        points = [(size / 2 + p[0] * scale, size / 2 + p[1] * scale) for p in transformed[i:i + 3]]
        draw.polygon(points, fill=color)
    image.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--folder", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()
    report, vertices, normals, colors = check(args.folder)
    if args.preview:
        preview(args.preview, vertices, normals, colors)
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
