#!/usr/bin/env python3
"""Generate private pose expectations from original extracted object_st text."""
import argparse
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    folder = args.upstream / "extracted/ntsc-1.2/assets/objects/object_st"
    rows = ["/* Private generated reference values; do not redistribute. */"]
    cases = []
    for suffix in ("000304", "0055A8", "005B98"):
        name = "object_st_Anim_" + suffix
        text = (folder / (name + ".inc.c")).read_text()
        count = int(re.search(r"(\d+),\s*// frameCount", text)[1])
        limit = int(re.search(r"(\d+),\s*// staticIndexMax", text)[1])
        data_name = re.search(r"(\w+),\s*// frameData", text)[1]
        joints_name = re.search(r"(\w+),\s*// jointIndices", text)[1]
        def integers(filename):
            source = (folder / (filename + ".inc.c")).read_text()
            source = re.sub(r"//[^\n]*", "", source)
            return [int(v, 0) for v in re.findall(r"-?0x[0-9a-fA-F]+|-?\d+", source)]
        data, joints = integers(data_name), integers(joints_name)
        assert len(joints) == 90
        rows.append(f"static const Vec3s sPose{suffix}[{count}][30] = {{")
        for frame in range(count):
            values = []
            for index in joints:
                value = data[index + (frame if index >= limit else 0)]
                values.append((value + 32768) % 65536 - 32768)
            rows.append("    {" + ",".join("{%d,%d,%d}" % tuple(values[i:i + 3]) for i in range(0, 90, 3)) + "},")
        rows.append("};")
        cases.append(f"{{0x06{suffix}, {count}, sPose{suffix}}}")
    rows.append("static const struct { u32 address; s16 frames; const Vec3s (*pose)[30]; } sPoseCases[] = {")
    rows.extend("    " + case + "," for case in cases)
    rows.append("};")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(rows) + "\n")
    print("Original Skulltula reference: 3 animations, 111 poses, 9990 channels")


if __name__ == "__main__":
    main()
