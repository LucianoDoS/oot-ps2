#!/usr/bin/env python3
"""Build private PS2 browser assets from the user's extracted Triforce mesh.

No game vertices, textures or generated icons belong in the source repository.
Format references: ps2dev/ps2sdk's mcIcon and ticky/ps2iconsys's PS2Icon writer.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct


def mesh_from_extraction(folder):
    vertices = []
    for row in re.findall(r"VTX\(([^)]*)\)", (folder / "gTriforceVtx.inc.c").read_text()):
        values = [int(v.strip(), 0) for v in row.split(",")]
        if len(values) != 9:
            raise ValueError("Unexpected VTX layout")
        vertices.append(tuple(values[:3]))
    slots, triangles = {}, []
    dl = (folder / "gTriforceDL.inc.c").read_text()
    commands = re.finditer(r"gsSP(Vertex|2Triangles|1Triangle)\(([^)]*)\)", dl)
    for command in commands:
        kind, args = command.groups()
        if kind == "Vertex":
            match = re.fullmatch(r"\s*&gTriforceVtx\[(\d+)\],\s*(\d+),\s*(\d+)\s*", args)
            if not match:
                raise ValueError("Unexpected vertex load")
            start, count, dest = map(int, match.groups())
            if start + count > len(vertices) or dest + count > 32:
                raise ValueError("Vertex load outside mesh/cache")
            for i in range(count):
                slots[dest + i] = start + i
        else:
            values = [int(v.strip(), 0) for v in args.split(",")]
            if len(values) != (8 if kind == "2Triangles" else 4):
                raise ValueError("Unexpected triangle layout")
            for start in range(0, len(values), 4):
                triangles.append(tuple(vertices[slots[v]] for v in values[start:start + 3]))
    if not vertices or not triangles:
        raise ValueError("Empty Triforce geometry")
    return triangles


def make_icon(triangles):
    points = [v for triangle in triangles for v in triangle]
    low = [min(v[a] for v in points) for a in range(3)]
    high = [max(v[a] for v in points) for a in range(3)]
    center = [(lo + hi) / 2 for lo, hi in zip(low, high)]
    scale = 3.0 / (high[1] - low[1])
    shapes, step_frames = 8, 20
    period = shapes * step_frames
    result = bytearray(struct.pack("<5I", 0x10000, shapes, 7, 0x3F800000, len(points)))
    for triangle in triangles:
        # OSD coordinates use Y down and Z towards the screen: rotate about X.
        # The browser's icon camera places Y=0 near the bottom of the panel.
        # An intermediate height fits both the card list and the action panel.
        positions = [((v[0] - center[0]) * scale, -(v[1] - center[1]) * scale - 1.5,
                      -(v[2] - center[2]) * scale) for v in triangle]
        ab = [positions[1][a] - positions[0][a] for a in range(3)]
        ac = [positions[2][a] - positions[0][a] for a in range(3)]
        normal = [ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2],
                  ab[0] * ac[1] - ab[1] * ac[0]]
        length = math.sqrt(sum(n * n for n in normal))
        if length == 0:
            raise ValueError("Degenerate triangle")
        packed_normal = [round(n / length * 4096) for n in normal]
        for position in positions:
            for shape in range(shapes):
                angle = shape * 2 * math.pi / shapes
                x, y, z = position
                rotated = (x * math.cos(angle) + z * math.sin(angle), y,
                           -x * math.sin(angle) + z * math.cos(angle))
                result += struct.pack("<3hH", *(round(p * 4096) for p in rotated), 0)
            result += struct.pack("<3hH2h4B", *packed_normal, 0, 0, 0, 255, 202, 48, 255)
    result += struct.pack("<IIfII", 1, period + 1, 0.35, 0, shapes)
    # One weight curve per absolute morph pose. Adjacent weights sum to one;
    # the final interval blends back to pose zero without moving the center.
    for shape in range(shapes):
        keys = {0: float(shape == 0), period: float(shape == 0)}
        if shape == 0:
            keys[step_frames] = keys[period - step_frames] = 0.0
        else:
            keys[(shape - 1) * step_frames] = 0.0
            keys[shape * step_frames] = 1.0
            keys[(shape + 1) * step_frames] = 0.0
        result += struct.pack("<II", shape, len(keys))
        for frame, weight in sorted(keys.items()):
            result += struct.pack("<2f", frame, weight)
    result += struct.pack("<H", 0x7FFF) * (128 * 128)
    return bytes(result)


def make_icon_sys():
    # Full-width Shift-JIS is what the PS2 browser's save-title font expects.
    def sjis(text):
        return "".join("\u3000" if c == " " else chr(ord(c) + 0xFEE0) for c in text).encode("shift_jis")
    first, second = sjis("Legend of Zelda"), sjis("Ocarina of Time")
    title = first + second + b"\0\0"
    if len(title) > 68:
        raise ValueError("Save title exceeds mcIcon title field")
    # Gold gradient; keep the upper-right corner dark behind the yellow title.
    backgrounds = [176, 132, 20, 0, 96, 60, 4, 0,
                   232, 188, 56, 0, 128, 80, 8, 0]
    directions = [-0.5, -0.7, -1, 0, 0.5, 0.2, -1, 0, 0, -0.5, 1, 0]
    lights = [0.40, 0.40, 0.40, 0, 0.18, 0.18, 0.18, 0, 0.12, 0.12, 0.12, 0]
    ambient = [0.22, 0.22, 0.22, 0]
    return struct.pack("<4s2xH4xI16I28f68s64s64s64s512x", b"PS2D", len(first), 0x60,
                       *backgrounds, *directions, *lights, *ambient, title,
                       b"triforce.icn", b"triforce.icn", b"triforce.icn")


def c_array(name, data):
    rows = ["    " + ",".join(f"0x{b:02x}" for b in data[i:i + 16]) + ","
            for i in range(0, len(data), 16)]
    return (f"const unsigned char {name}[] __attribute__((aligned(64))) = {{\n" +
            "\n".join(rows) + f"\n}};\nconst unsigned int {name}_size = sizeof({name});\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--version", default="ntsc-1.2")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    folder = args.upstream / "extracted" / args.version / "assets/objects/object_triforce_spot"
    triangles = mesh_from_extraction(folder)
    icon, metadata = make_icon(triangles), make_icon_sys()
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "triforce.icn").write_bytes(icon)
    (args.output / "icon.sys").write_bytes(metadata)
    (args.output / "save_icon_data.c").write_text(
        "/* Private generated game asset. Do not redistribute. */\n" +
        c_array("ps2_save_icon_sys", metadata) + c_array("ps2_save_icon_model", icon))
    report = {"title": ["Legend of Zelda", "Ocarina of Time"], "triangles": len(triangles),
              "browser_center_y": -1.5, "background": "gold gradient",
              "animation": {"shapes": 8, "period_frames": 160, "speed": 0.35,
                            "motion": "full yaw turn"},
              "icon_sys_bytes": len(metadata), "icon_bytes": len(icon),
              "icon_sha256": hashlib.sha256(icon).hexdigest(), "source": str(folder)}
    (args.output / "save_icon_report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
