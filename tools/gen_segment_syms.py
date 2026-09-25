#!/usr/bin/env python3
"""
gen_segment_syms.py — Genera los símbolos de segmento para el linker

── PARA QUÉ SIRVE ─────────────────────────────────────────────────────

OoT referencia unos 144 símbolos con el patrón:

    _xxxSegmentStart, _xxxSegmentEnd,
    _xxxSegmentRomStart, _xxxSegmentRomEnd,
    _xxxSegmentBssStart, _xxxSegmentBssEnd

En N64 los generaba el "spec" del ROM durante el build. En el port a PS2
no los generaba nadie, así que el enlace fallaba con una montaña de
símbolos indefinidos en cuanto se compilaba src/code.

linker/linkfile.ps2 define a mano los que necesita el arranque. Este
script genera TODOS los demás (los de overlays: uno por actor y uno por
escena), leyendo directamente los nombres que aparecen en el código.

── USO ────────────────────────────────────────────────────────────────

    python3 tools/gen_segment_syms.py > linker/segments.ld

y luego añade en linkfile.ps2, dentro de SECTIONS:

    INCLUDE linker/segments.ld

── POR QUÉ TODO VALE 0 (los *RomStart) ────────────────────────────────

En PS2 no hay ROM mapeada en memoria. Los datos se leen del fichero
.z64 por DMA (ver src/platform/ps2/libultra_stub/osRom.c), y la posición
real de cada cosa la aporta la tabla dmadata en tiempo de ejecución.
Estos símbolos solo tienen que EXISTIR para que el enlace cierre.
"""

import os
import re
import sys

# Sufijos que el juego usa (include/segment_symbols.h)
SUFFIXES = [
    "SegmentStart", "SegmentEnd",
    "SegmentRomStart", "SegmentRomEnd",
    "SegmentBssStart", "SegmentBssEnd",
    "SegmentVramStart", "SegmentVramEnd",
    "SegmentSize",
]

PATTERN = re.compile(r"\b_([A-Za-z0-9_]+?)(" + "|".join(SUFFIXES) + r")\b")

SEARCH_DIRS = ["src", "include"]


def find_symbols(root):
    """Recorre el código y recoge todos los símbolos de segmento usados."""
    found = set()
    for d in SEARCH_DIRS:
        base = os.path.join(root, d)
        if not os.path.isdir(base):
            continue
        for dirpath, _dirnames, filenames in os.walk(base):
            for fn in filenames:
                if not fn.endswith((".c", ".h", ".inc.c")):
                    continue
                path = os.path.join(dirpath, fn)
                try:
                    with open(path, "r", encoding="utf-8", errors="ignore") as f:
                        text = f.read()
                except OSError:
                    continue
                for m in PATTERN.finditer(text):
                    found.add("_" + m.group(1) + m.group(2))
    return found


# Los que ya define linkfile.ps2 a mano: no hay que duplicarlos, porque
# dos PROVIDE del mismo símbolo no es error pero sí ruido.
ALREADY_IN_LINKFILE = {
    "_bootSegmentEnd", "_bootSegmentRomStart",
    "_codeSegmentStart", "_codeSegmentEnd",
    "_codeSegmentBssStart", "_codeSegmentBssEnd",
    "_codeSegmentRomStart", "_codeSegmentRomEnd",
    "_buffersSegmentEnd",
    "_dmadataSegmentStart", "_dmadataSegmentRomStart", "_dmadataSegmentRomEnd",
    "_n64ddSegmentStart", "_n64ddSegmentEnd",
    "_n64ddSegmentBssStart", "_n64ddSegmentBssEnd",
    "_n64ddSegmentRomStart", "_n64ddSegmentRomEnd",
    "_link_animetionSegmentStart", "_link_animetionSegmentRomStart",
    "_kanjiSegmentRomStart", "_nes_font_staticSegmentRomStart",
    "_message_staticSegmentRomStart", "_message_texture_staticSegmentRomStart",
    "_do_action_staticSegmentRomStart",
    "_map_grand_staticSegmentRomStart", "_map_i_staticSegmentRomStart",
    "_jpn_message_data_staticSegmentStart",
    "_jpn_message_data_staticSegmentRomStart",
    "_fra_message_data_staticSegmentRomStart",
    "_ger_message_data_staticSegmentRomStart",
}


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    syms = find_symbols(root)
    pending = sorted(s for s in syms if s not in ALREADY_IN_LINKFILE)

    out = sys.stdout
    out.write("/* ==================================================\n")
    out.write(" *  segments.ld — GENERADO AUTOMÁTICAMENTE\n")
    out.write(" *  No editar a mano. Regenerar con:\n")
    out.write(" *      python3 tools/gen_segment_syms.py > linker/segments.ld\n")
    out.write(" *\n")
    out.write(f" *  Símbolos encontrados en el código : {len(syms)}\n")
    out.write(f" *  Ya definidos en linkfile.ps2      : {len(syms) - len(pending)}\n")
    out.write(f" *  Generados aquí                    : {len(pending)}\n")
    out.write(" *\n")
    out.write(" *  Todos a 0: en PS2 no hay ROM en memoria; la posición\n")
    out.write(" *  real la resuelve dmadata en tiempo de ejecución.\n")
    out.write(" * ================================================== */\n\n")

    for s in pending:
        out.write(f"PROVIDE({s} = 0x0);\n")

    sys.stderr.write(
        f"[gen_segment_syms] {len(pending)} símbolos generados "
        f"(de {len(syms)} encontrados en el código)\n"
    )


if __name__ == "__main__":
    main()
