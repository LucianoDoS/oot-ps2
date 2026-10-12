#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Inspect the exact ROM-derived initializers linked into an EE ELF.

This module reads an unstripped local ELF and the source include declarations.
It neither downloads nor copies a ROM. Reports use repository-relative names.
"""
from dataclasses import dataclass
from pathlib import Path
import re
import struct


@dataclass(frozen=True)
class Symbol:
    name: str
    source: str
    address: int
    size: int
    file_offset: int
    local: bool


@dataclass(frozen=True)
class NativeSymbol:
    name: str
    source: str
    address: int
    size: int
    local: bool
    kind: int
    section: int
    table_offset: int
    file_offset: int | None


class Elf:
    def __init__(self, data):
        self.data = data
        if data[:7] != b"\x7fELF\x01\x01\x01" or len(data) < 52:
            raise ValueError("Expected an unstripped little-endian ELF32")
        header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
        _, kind, machine, _, self.entry, phoff, shoff, _, _, phsize, phnum, shsize, shnum, _ = header
        if kind != 2 or machine != 8 or phsize != 32 or shsize != 40:
            raise ValueError("Expected an EE MIPS executable")
        if phoff + phnum * phsize > len(data) or shoff + shnum * shsize > len(data):
            raise ValueError("ELF table outside the file")
        self.segments = []
        for index in range(phnum):
            typ, offset, address, physical, size, memory, flags, align = struct.unpack_from("<8I", data, phoff + index * phsize)
            if typ != 1:
                continue
            if offset + size > len(data) or size > memory or address != physical:
                raise ValueError("Invalid EE load range")
            self.segments.append((address, offset, size, memory))
        sections = [struct.unpack_from("<10I", data, shoff + index * shsize) for index in range(shnum)]
        self.symbols = []
        self.all_symbols = []
        self.sources = set()
        for section in sections:
            _, typ, _, _, offset, size, link, _, _, entry_size = section
            if typ != 2:
                continue
            if entry_size != 16 or size % 16 or offset + size > len(data) or link >= len(sections):
                raise ValueError("Invalid ELF symbol table")
            strings = sections[link]
            if strings[1] != 3 or strings[4] + strings[5] > len(data):
                raise ValueError("Invalid ELF symbol string table")
            table = data[strings[4]:strings[4] + strings[5]]
            source = ""
            for position in range(offset, offset + size, entry_size):
                name_offset, address, count, info, _, shindex = struct.unpack_from("<IIIBBH", data, position)
                if name_offset >= len(table):
                    raise ValueError("Invalid symbol name")
                end = table.find(b"\0", name_offset)
                if end < 0:
                    raise ValueError("Unterminated symbol name")
                name = table[name_offset:end].decode("utf-8")
                if info & 15 == 4:  # STT_FILE
                    source = Path(name).name
                    self.sources.add(source)
                elif shindex:  # Keep BSS, functions and absolute linker symbols for relocations.
                    file_offset = None
                    try:
                        if shindex < len(sections) and sections[shindex][1] != 8:
                            file_offset = self.file_offset(address, max(count, 1))
                    except ValueError:
                        pass
                    local = info >> 4 == 0
                    self.all_symbols.append(NativeSymbol(name, source if local else "", address, count,
                                                         local, info & 15, shindex, position, file_offset))
                    # STT_FILE scopes local symbols. Global symbols live in
                    # the final symtab partition and have no such provenance.
                    if info & 15 == 1 and count and file_offset is not None:
                        self.symbols.append(Symbol(name, source, address, count, file_offset, local))
        if not self.symbols:
            raise ValueError("No object symbols; audit the unstripped build ELF")

    def file_offset(self, address, size):
        for start, offset, count, _ in self.segments:
            if start <= address and address + size <= start + count:
                return offset + address - start
        raise ValueError("Symbol does not have file-backed load bytes")

    def find(self, name, source=None):
        matches = [symbol for symbol in self.symbols if symbol.name == name and (source is None or symbol.source == source)]
        if len(matches) != 1:
            raise ValueError(f"Expected one symbol {source or '*'}:{name}, found {len(matches)}")
        return matches[0]

    def find_native(self, name, source=None, local=False):
        matches = [symbol for symbol in self.all_symbols if symbol.name == name and symbol.local == local
                   and (not local or symbol.source == Path(source or "").name)]
        if len(matches) != 1:
            raise ValueError(f"Expected one native symbol {source or '*'}:{name}, found {len(matches)}")
        return matches[0]


def source_arrays(project):
    """Return arrays that contain an extracted assets/*.inc.c initializer.

    Includes in inactive version branches may not be linked. Every include
    must nevertheless have an identifiable surrounding declaration.
    """
    arrays = []
    for path in sorted((project / "src").rglob("*.c")):
        text = path.read_text(encoding="utf-8")
        for include in re.finditer(r'#include\s+"(assets/[^"\n]+\.inc\.c)"', text):
            declaration = re.search(r'\b(\w+)\s*(?:\[[^;{}]*\])?\s*=\s*\{\s*$', text[:include.start()])
            if declaration is None:
                raise ValueError(f"Unclassified asset include: {path.relative_to(project)}:{include.group(1)}")
            arrays.append(dict(source=path.relative_to(project).as_posix(),
                               symbol=declaration.group(1), include=include.group(1)))
    return arrays


def native_literal_arrays(project):
    """Resolve literal geometry/cinematic arrays to their actual C units.

    An .inc.c's local symbols belong to the including translation unit in the
    ELF, not to the included file's basename. Follow real local include edges;
    never guess an actor's name or accept an unrelated same-named static array.
    """
    files = {path.resolve(): path.read_text(encoding="utf-8")
             for path in (project / "src").rglob("*.c")}
    parents = {}
    for path, text in files.items():
        for name in re.findall(r'#include\s+"([^"\n]+\.c)"', text):
            candidates = ((path.parent / name).resolve(), (project / name).resolve())
            matches = {candidate for candidate in candidates if candidate in files}
            if len(matches) > 1:
                raise ValueError(f"Ambiguous local C include: {path}:{name}")
            if matches:
                parents.setdefault(matches.pop(), set()).add(path)

    def owners(path, visiting=frozenset()):
        if path in visiting:
            raise ValueError(f"C include cycle: {path}")
        if not path.name.endswith(".inc.c"):
            return {path}
        result = set()
        for parent in parents.get(path, ()):
            result.update(owners(parent, visiting | {path}))
        if not result:
            raise ValueError(f"No translation unit for literal asset include: {path}")
        return result

    declaration = re.compile(r"\b(Vtx|CutsceneData)\s+(\w+)\s*\[[^;{}]*\]\s*=\s*\{")
    arrays = []
    for path, text in sorted(files.items()):
        if "platform" in path.relative_to(project).parts:
            continue  # The platform's own generated geometry is not game data.
        for match in declaration.finditer(text):
            kind, name = match.groups()
            if text[match.end():].lstrip().startswith("#include"):
                continue  # Already classified through extracted assets/*.inc.c.
            if name == "languageVertices" and path.name == "z_file_nameset.c":
                continue  # Explicitly authored PS2 locale-menu rectangle.
            for owner in sorted(owners(path)):
                arrays.append(dict(kind="native_vertex_geometry" if kind == "Vtx" else "native_cutscene_data",
                                   source=path.relative_to(project).as_posix(), symbol=name,
                                   translation_unit=owner.relative_to(project).as_posix()))
    return arrays


def inventory(project, elf, generated, include_private_icon=True):
    linked = []
    absent = []
    seen = set()

    def add(item, required=False):
        owner = Path(item.get("translation_unit", item["source"])).name
        matches = [symbol for symbol in elf.symbols
                   if symbol.name == item["symbol"] and
                   ((symbol.local and symbol.source == owner) or
                    (not symbol.local and owner in elf.sources))]
        if len(matches) > 1:
            raise ValueError(f"Ambiguous asset symbol {item['source']}:{item['symbol']}")
        if not matches:
            if required:
                raise ValueError(f"Missing generated private initializer {item['symbol']}")
            absent.append(item)
            return
        symbol = matches[0]
        key = symbol.address, symbol.size
        if key not in seen:
            linked.append((symbol, item))
            seen.add(key)

    for item in source_arrays(project):
        add(dict(kind="extracted_array", **item))
    metadata = (generated / "rom_metadata.c").read_text(encoding="utf-8")
    microcode = re.findall(r'\bu64\s+((?:rspboot|aspMain|gspF3|gspS2|njpgdsp)\w*Start)\s*\[', metadata)
    if not microcode:
        raise ValueError("No microcode inventory in generated rom_metadata.c")
    for name in microcode:
        add(dict(kind="microcode", source="generated/rom_metadata.c", symbol=name), required=True)

    # Native-endian descriptors are still ROM-derived initializers, including
    # the sequence-to-font map. Audio's weak public aliases share these exact
    # native arrays: deduplicate by their linked address/size, not by spelling.
    for name in ("gDmaDataTable", "ps2_gSoundFontTable", "ps2_gSequenceTable",
                 "ps2_gSampleBankTable", "gSequenceFontTable"):
        add(dict(kind="generated_rom_table", source="generated/rom_metadata.c", symbol=name), required=True)

    # Some original graphics are literal C arrays rather than extracted
    # includes. The sign's include has the STT_FILE identity of its parent
    # translation unit, which matters for its locally scoped symbol.
    inline = [
        ("src/libu64/gfxprint.c", "sGfxPrintFontData", None),
        ("src/libu64/gfxprint.c", "sGfxPrintFontTLUT", None),
        ("src/libu64/gfxprint.c", "sGfxPrintRainbowTLUT", None),
        ("src/libu64/gfxprint.c", "sGfxPrintRainbowData", None),
        ("src/code/fault_gc_drawer.c", "sFaultDrawerFont", None),
        ("src/code/fault_n64.c", "sFaultDrawerFont", None),
        ("src/overlays/actors/ovl_En_Kanban/z_en_kanban_gfx.inc.c", "sShadowTexFlags",
         "src/overlays/actors/ovl_En_Kanban/z_en_kanban.c"),
        ("src/overlays/actors/ovl_Bg_Jya_Cobra/z_bg_jya_cobra.c", "D_8089731C", None),
        ("src/overlays/actors/ovl_Bg_Jya_Cobra/z_bg_jya_cobra.c", "D_80897398", None),
        ("src/overlays/actors/ovl_Boss_Goma/z_boss_goma.c", "sClearPixelTableFirstPass", None),
        ("src/overlays/actors/ovl_Boss_Goma/z_boss_goma.c", "sClearPixelTableSecondPass", None),
    ]
    for source, name, owner in inline:
        text = (project / source).read_text(encoding="utf-8")
        if not re.search(r"\b" + re.escape(name) + r"\s*\[[^;{}]*\]\s*=\s*\{", text):
            raise ValueError(f"Inline private graphics declaration changed: {source}:{name}")
        item = dict(kind="inline_original_graphics", source=source, symbol=name)
        if owner:
            item["translation_unit"] = owner
        add(item)
    for item in native_literal_arrays(project):
        add(item)
    if len(linked) > 512:
        raise ValueError(f"External asset inventory has {len(linked)} records, exceeding catalogue capacity 512")
    if include_private_icon:
        symbol = elf.find("ps2_save_icon_model")
        linked.append((symbol, dict(kind="private_memory_card_mesh", source="generated/save_icon_data.c", symbol=symbol.name)))
    linked.sort(key=lambda pair: pair[0].address)
    for (left, _), (right, _) in zip(linked, linked[1:]):
        if left.address + left.size > right.address:
            raise ValueError(f"Overlapping assets: {left.name}, {right.name}")
    return linked, absent
