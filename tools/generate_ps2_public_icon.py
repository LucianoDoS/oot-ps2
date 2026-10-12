#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate three gold triangular prisms from arithmetic, without ROM data.

The PS2 file writer is shared with the private generator, but this generator
never calls its extraction function or reads its ROM vertex/display lists.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
from generate_ps2_save_icon import c_array, make_icon, make_icon_sys


def procedural_mesh():
    height = math.sqrt(3)
    faces = [((0, 2 * height), (-1, height), (1, height)),
             ((-1, height), (-2, 0), (0, 0)),
             ((1, height), (0, 0), (2, 0))]
    result = []
    for face in faces:
        front = [(x, y, 0.2) for x, y in face]
        back = [(x, y, -0.2) for x, y in face]
        result.append(tuple(front))
        result.append(tuple(reversed(back)))
        for index in range(3):
            following = (index + 1) % 3
            result.append((front[index], back[index], back[following]))
            result.append((front[index], back[following], front[following]))
    return result


def generate(output):
    mesh = procedural_mesh()
    icon, metadata = make_icon(mesh), make_icon_sys()
    output.mkdir(parents=True, exist_ok=True)
    (output / "triforce.icn").write_bytes(icon)
    (output / "icon.sys").write_bytes(metadata)
    (output / "save_icon_data.c").write_text(
        "/* SPDX-License-Identifier: GPL-2.0-or-later\n"
        " * Procedural triangular prisms; no extracted game mesh. */\n"
        + c_array("ps2_save_icon_sys", metadata) + c_array("ps2_save_icon_model", icon), encoding="utf-8")
    report = dict(schema=1, source="procedural_three_triangular_prisms", rom_required=False,
                  triangles=len(mesh), icon_bytes=len(icon), icon_sha256=hashlib.sha256(icon).hexdigest(),
                  animation_shapes=8, animation_speed=0.35, browser_center_y=-1.5)
    (output / "save_icon_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(generate(args.output)))


if __name__ == "__main__":
    main()
