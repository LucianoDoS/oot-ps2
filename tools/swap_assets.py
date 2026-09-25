#!/usr/bin/env python3
"""
swap_assets.py — Convierte los assets de OoT a little-endian para PS2

═══════════════════════════════════════════════════════════════════════
 EL PROBLEMA QUE RESUELVE
═══════════════════════════════════════════════════════════════════════

Esta es, con diferencia, la trampa más traicionera de todo el port,
porque no da error: simplemente se dibuja basura.

En OoT hay DOS clases de display list:

  a) Las que vienen de los ASSETS (escenas, objetos, mapas). Son datos
     binarios sacados de la ROM de N64 → BIG-ENDIAN.

  b) Las que el juego CONSTRUYE en tiempo de ejecución con las macros
     gSPxxx (Gfx_SetupDL, el HUD, el menú de pausa, casi todo z_lib...).
     Las escribe el código C compilado para PS2 → LITTLE-ENDIAN.

NO EXISTE ningún modo de lectura que acierte con las dos a la vez.

El intérprete original leía siempre en big-endian (read_be32), con lo
que los menús y el HUD se decodificaban al revés. Si lo hubieras
cambiado a little-endian, se habrían roto las escenas y los objetos.

La solución correcta, y la que usan todos los ports serios, es
convertir los ASSETS a little-endian UNA VEZ al extraerlos, y a partir
de ahí leerlo todo en el endianness nativo de la consola.

═══════════════════════════════════════════════════════════════════════
 USO
═══════════════════════════════════════════════════════════════════════

    # 1. Extraer los assets con las herramientas del decomp
    python3 tools/extract_assets.py

    # 2. Convertirlos a little-endian
    python3 tools/swap_assets.py assets/

    # 3. Compilar
    make PLATFORM=PS2

Opciones:
    --dry-run    enseña qué haría sin tocar nada
    --restore    deshace la conversión (vuelve a big-endian)

═══════════════════════════════════════════════════════════════════════
 QUÉ SE CONVIERTE Y QUÉ NO
═══════════════════════════════════════════════════════════════════════

CUIDADO: no todo se voltea igual. El tamaño del dato importa.

    Display lists (.dlist)  → palabras de 32 bits
    Vértices (.vtx)         → campos de 16 bits (x,y,z,flag,s,t) + 4 bytes
                               de color que NO se tocan
    Matrices (.mtx)         → palabras de 16 bits
    Texturas RGBA16/IA16    → palabras de 16 bits
    Texturas RGBA32         → palabras de 32 bits
    Texturas I4/I8/IA8/CI4/CI8 → son bytes sueltos, NO SE TOCAN
    Paletas (TLUT)          → palabras de 16 bits

Voltear una textura I8 "por si acaso" la destroza. Por eso este script
decide por extensión y no a lo bruto.
"""

import argparse
import os
import struct
import sys

# ── Qué extensión se voltea y con qué tamaño de palabra ──────────────
#   4 = palabras de 32 bits
#   2 = palabras de 16 bits
#   0 = no tocar (datos de un solo byte)
SWAP_RULES = {
    ".dlist":     4,
    ".dl":        4,
    ".mtx":       2,
    ".rgba32":    4,
    ".rgba16":    2,
    ".ia16":      2,
    ".tlut":      2,
    ".jpg":       0,
    ".i4":        0,
    ".i8":        0,
    ".ia4":       0,
    ".ia8":       0,
    ".ci4":       0,
    ".ci8":       0,
    ".bin":       0,   # genérico: no adivinamos
}

# Los vértices necesitan trato especial (mezclan s16 y bytes)
VTX_EXTS = {".vtx"}

MARKER = ".ps2le"   # fichero testigo para no convertir dos veces


def swap_words(data: bytes, width: int) -> bytes:
    """Voltea el orden de bytes en palabras de `width` bytes."""
    if width <= 1:
        return data

    # Si la longitud no es múltiplo del ancho, la cola se deja tal cual
    n = (len(data) // width) * width
    body, tail = data[:n], data[n:]

    out = bytearray(n)
    for i in range(0, n, width):
        out[i:i + width] = body[i:i + width][::-1]
    return bytes(out) + tail


def swap_vtx(data: bytes) -> bytes:
    """
    Vtx de N64 = 16 bytes:
        s16 x, y, z;  u16 flag;  s16 s, t;  u8 r, g, b, a

    Los 12 primeros bytes son seis s16 (hay que voltearlos de dos en
    dos) y los 4 últimos son bytes sueltos de color (NO se tocan).

    Voltear los 16 bytes enteros, que es el error fácil, deja los
    colores al revés y los vértices con valores absurdos.
    """
    if len(data) % 16 != 0:
        sys.stderr.write(
            f"  AVISO: tamaño de .vtx no múltiplo de 16 ({len(data)} bytes); "
            f"lo dejo sin tocar\n")
        return data

    out = bytearray(data)
    for base in range(0, len(data), 16):
        for off in range(0, 12, 2):           # los seis s16
            i = base + off
            out[i], out[i + 1] = out[i + 1], out[i]
        # bytes 12..15 (color/normal): intactos
    return bytes(out)


def process_file(path: str, dry_run: bool) -> str:
    ext = os.path.splitext(path)[1].lower()

    if ext in VTX_EXTS:
        if not dry_run:
            with open(path, "rb") as f:
                data = f.read()
            with open(path, "wb") as f:
                f.write(swap_vtx(data))
        return "vtx (s16 x6 + 4 bytes de color intactos)"

    if ext not in SWAP_RULES:
        return None

    width = SWAP_RULES[ext]
    if width == 0:
        return "omitido (datos de 1 byte)"

    if not dry_run:
        with open(path, "rb") as f:
            data = f.read()
        with open(path, "wb") as f:
            f.write(swap_words(data, width))
    return f"volteado en palabras de {width * 8} bits"


def main():
    ap = argparse.ArgumentParser(
        description="Convierte los assets de OoT a little-endian para PS2")
    ap.add_argument("directory", help="carpeta de assets (normalmente assets/)")
    ap.add_argument("--dry-run", action="store_true",
                    help="enseña qué haría sin modificar nada")
    ap.add_argument("--restore", action="store_true",
                    help="deshace la conversión (vuelve a big-endian)")
    args = ap.parse_args()

    root = args.directory
    if not os.path.isdir(root):
        sys.exit(f"ERROR: no existe la carpeta {root}")

    marker_path = os.path.join(root, MARKER)
    already = os.path.exists(marker_path)

    if args.restore:
        if not already:
            sys.exit("Los assets ya están en big-endian; no hay nada que deshacer.")
    else:
        if already:
            sys.exit(
                "Los assets YA están convertidos a little-endian.\n"
                "Voltearlos otra vez los dejaría como estaban.\n"
                "Si de verdad quieres rehacerlo: --restore y luego conviértelos.")

    counts = {}
    total = 0

    for dirpath, _dirnames, filenames in os.walk(root):
        for fn in sorted(filenames):
            if fn == MARKER:
                continue
            path = os.path.join(dirpath, fn)
            result = process_file(path, args.dry_run)
            if result is None:
                continue
            counts[result] = counts.get(result, 0) + 1
            total += 1
            if args.dry_run:
                rel = os.path.relpath(path, root)
                print(f"  {rel}: {result}")

    print()
    print(f"Ficheros procesados: {total}")
    for k, v in sorted(counts.items()):
        print(f"  {v:6d}  {k}")

    if not args.dry_run:
        if args.restore:
            if os.path.exists(marker_path):
                os.remove(marker_path)
            print("\nAssets devueltos a big-endian.")
        else:
            with open(marker_path, "w") as f:
                f.write("Assets convertidos a little-endian para PS2.\n"
                        "Este fichero evita que swap_assets.py los vuelva a "
                        "voltear.\n")
            print("\nAssets convertidos a little-endian.")
            print("Compila con el modo nativo (por defecto):  make PLATFORM=PS2")
    else:
        print("\n(--dry-run: no se ha modificado nada)")


if __name__ == "__main__":
    main()
