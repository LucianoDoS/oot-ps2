#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Describe native-data relocations and reconstruct data for a fixed EE ELF.

Only locations marked R_MIPS_32 by the user's compiler are rewritten. Public
metadata contains symbol identities, offsets, sizes, addresses and checksums;
it contains no texture, vertex, cinematic or microcode initializer bytes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import zlib

from ps2_release_assets import Elf, inventory


CATALOGUE_MAGIC = b"OOTPS2RELASSET01"
DATA_MAGIC = b"OOTPS2DATA01".ljust(16, b"\0")
CATALOGUE_BYTES = 16384


class ObjectElf:
    def __init__(self, path):
        self.path, self.data = path, path.read_bytes()
        if len(self.data) < 52 or self.data[:7] != b"\x7fELF\x01\x01\x01":
            raise ValueError(f"Invalid ELF32 object: {path}")
        header = struct.unpack_from("<16sHHIIIIIHHHHHH", self.data)
        if header[1:3] != (1, 8) or header[11] != 40 or header[6]+header[11]*header[12] > len(self.data):
            raise ValueError(f"Expected an EE relocatable object: {path}")
        self.sections = [struct.unpack_from("<10I", self.data, header[6]+40*index)
                         for index in range(header[12])]
        self.tables, self.relocations = {}, []
        for index, section in enumerate(self.sections):
            if section[1] == 2:
                if section[9] != 16 or section[5] % 16 or section[4]+section[5] > len(self.data) or section[6] >= len(self.sections):
                    raise ValueError(f"Invalid object symbol table: {path}")
                strings = self.sections[section[6]]
                if strings[1] != 3 or strings[4]+strings[5] > len(self.data):
                    raise ValueError(f"Invalid object string table: {path}")
                table = self.data[strings[4]:strings[4]+strings[5]]
                symbols = []
                for offset in range(section[4], section[4]+section[5], 16):
                    name, address, size, info, _, target_section = struct.unpack_from("<IIIBBH", self.data, offset)
                    end = table.find(b"\0", name)
                    if name >= len(table) or end < 0:
                        raise ValueError(f"Invalid object symbol name: {path}")
                    symbols.append(dict(name=table[name:end].decode("utf-8"), address=address, bytes=size,
                                        local=info >> 4 == 0, kind=info & 15, section=target_section))
                self.tables[index] = symbols
        if len(self.tables) != 1:
            raise ValueError(f"Expected one object symbol table: {path}")
        self.symbols = next(iter(self.tables.values()))
        for section in self.sections:
            if section[1] not in (4, 9):
                continue
            stride = 12 if section[1] == 4 else 8
            if section[9] != stride or section[5] % stride or section[4]+section[5] > len(self.data) or section[7] >= len(self.sections) or section[6] not in self.tables:
                raise ValueError(f"Invalid object relocation table: {path}")
            for at in range(section[4], section[4]+section[5], stride):
                offset, info = struct.unpack_from("<II", self.data, at)
                symbol_index = info >> 8
                if symbol_index >= len(self.tables[section[6]]):
                    raise ValueError(f"Invalid relocation symbol index: {path}")
                if stride == 12:
                    addend = struct.unpack_from("<i", self.data, at+8)[0]
                else:
                    target = self.sections[section[7]]
                    if target[1] == 8 or offset+4 > target[5] or target[4]+offset+4 > len(self.data):
                        raise ValueError(f"Invalid in-place relocation addend: {path}")
                    addend = struct.unpack_from("<i", self.data, target[4]+offset)[0]
                self.relocations.append(dict(section=section[7], offset=offset, type=info & 255,
                                             addend=addend, target=self.tables[section[6]][symbol_index]))

    def find(self, name):
        matches = [symbol for symbol in self.symbols if symbol["name"] == name and symbol["kind"] == 1 and symbol["section"]]
        if len(matches) != 1:
            raise ValueError(f"Expected one object initializer {self.path}:{name}, found {len(matches)}")
        return matches[0]


def target_for_relocation(obj, relocation, elf, owner):
    target, addend = relocation["target"], relocation["addend"]
    if target["kind"] == 3:  # STT_SECTION: explicit addend points within a named object.
        address = target["address"]+addend
        candidates = [symbol for symbol in obj.symbols if symbol["section"] == target["section"]
                      and symbol["kind"] in (0, 1, 2) and symbol["bytes"]
                      and symbol["address"] <= address < symbol["address"]+symbol["bytes"]]
        if len(candidates) != 1:
            raise ValueError(f"Section relocation has {len(candidates)} target spans: {obj.path}:{relocation['offset']:x}")
        target = candidates[0]
        addend = address-target["address"]
    if not target["name"] or target["kind"] not in (0, 1, 2):
        raise ValueError(f"Unsupported native relocation target: {obj.path}:{target}")
    native = elf.find_native(target["name"], owner, local=target["local"])
    if native.size and not 0 <= addend <= native.size:
        raise ValueError(f"Relocation addend outside native target: {target['name']}+{addend}")
    value = native.address+addend
    if not 0 <= value <= 0xffffffff:
        raise ValueError(f"Relocation target outside 32-bit ABI: {target['name']}+{addend}")
    return dict(symbol=native.name, translation_unit=owner if native.local else None,
                local=native.local, address=native.address, bytes=native.size, kind=native.kind), addend


def descriptor(project, reference, generated, object_root=None):
    original = reference.read_bytes()
    elf = Elf(original)
    selected, _ = inventory(project, elf, generated, include_private_icon=False)
    object_root = object_root or generated.parent
    objects, records, payload = {}, [], bytearray()
    for symbol, item in selected:
        owner = item.get("translation_unit", item["source"])
        path = (object_root / owner).with_suffix(".o")
        if path not in objects:
            objects[path] = ObjectElf(path)
        obj = objects[path]
        native = obj.find(symbol.name)
        if native["bytes"] != symbol.size:
            raise ValueError(f"Object/reference initializer sizes differ: {owner}:{symbol.name}")
        relocated = []
        for relocation in obj.relocations:
            if relocation["section"] != native["section"] or not native["address"] <= relocation["offset"] < native["address"]+native["bytes"]:
                continue
            offset = relocation["offset"]-native["address"]
            if relocation["type"] != 2 or offset+4 > symbol.size or (symbol.address+offset) % 4:
                raise ValueError(f"Unsupported or unaligned asset relocation: {owner}:{symbol.name}+{offset}, type {relocation['type']}")
            target, addend = target_for_relocation(obj, relocation, elf, owner)
            linked = struct.unpack_from("<I", original, symbol.file_offset+offset)[0]
            if linked != target["address"]+addend:
                raise ValueError(f"Object relocation does not explain linked initializer: {owner}:{symbol.name}+{offset}")
            relocated.append(dict(offset=offset, type="R_MIPS_32", target=target, addend=addend))
        relocated.sort(key=lambda row: row["offset"])
        if len({row["offset"] for row in relocated}) != len(relocated):
            raise ValueError(f"Repeated asset relocation: {owner}:{symbol.name}")
        data = original[symbol.file_offset:symbol.file_offset+symbol.size]
        records.append(dict(symbol=symbol.name, source=item["source"], translation_unit=owner,
                            local=symbol.local, kind=item["kind"], address=symbol.address, bytes=symbol.size,
                            offset=len(payload), crc32=zlib.crc32(data), relocations=relocated))
        payload.extend(data)
    if not 0 < len(records) <= 512 or not 0 < len(payload) <= 4*1024*1024:
        raise ValueError("Native descriptor exceeds startup catalogue bounds")
    return dict(schema=1, format="OOTPS2_NATIVE_ASSETS", version=1, records=records,
                native_data_bytes=len(payload), payload_crc32=zlib.crc32(payload),
                relocation_count=sum(len(row["relocations"]) for row in records),
                metadata_only=True, public_distribution_cleared=False)


def identity(row):
    return row["symbol"], row["translation_unit"] if row["local"] else None, row["local"]


def relocation_identity(row):
    return row["offset"], row["type"], identity(row["target"]), row["addend"]


def validate_release(description, release):
    data = release.read_bytes()
    if description.get("schema") != 1 or description.get("format") != "OOTPS2_NATIVE_ASSETS" or description.get("version") != 1 or not description.get("metadata_only"):
        raise ValueError("Unsupported native asset descriptor")
    if hashlib.sha256(data).hexdigest() != description.get("elf_sha256"):
        raise ValueError("Release ELF hash differs from native asset descriptor")
    elf = Elf(data)
    catalogue = elf.find("gPS2ReleaseAssets")
    if catalogue.size != CATALOGUE_BYTES:
        raise ValueError("Release catalogue has wrong allocation")
    records = description["records"]
    if not 0 < len(records) <= 512 or len({identity(row) for row in records}) != len(records):
        raise ValueError("Invalid or duplicate native asset identities")
    header = CATALOGUE_MAGIC+struct.pack("<4I", 1, len(records), description["native_data_bytes"], description["payload_crc32"])
    expected = bytearray(header)
    offset, end = 0, 0
    for row in records:
        if row["offset"] != offset or not 0 < row["bytes"] <= 4*1024*1024 or not 0x100000 <= row["address"] < row["address"]+row["bytes"] <= 0x2000000 or row["address"] < end:
            raise ValueError("Invalid native asset record layout")
        native = elf.find_native(row["symbol"], row["translation_unit"], row["local"])
        if native.address != row["address"] or native.size != row["bytes"] or native.file_offset is None:
            raise ValueError("Release symbol disagrees with native asset descriptor")
        if any(data[native.file_offset:native.file_offset+native.size]):
            raise ValueError("Release ELF contains a native initializer")
        for relocation in row["relocations"]:
            target = relocation["target"]
            symbol = elf.find_native(target["symbol"], target["translation_unit"], target["local"])
            if (symbol.address, symbol.size, symbol.kind) != (target["address"], target["bytes"], target["kind"]):
                raise ValueError("Release relocation target disagrees with symbol table")
        expected.extend(struct.pack("<4I", row["address"], row["bytes"], row["offset"], row["crc32"]))
        offset += row["bytes"]
        end = row["address"]+row["bytes"]
    if offset != description["native_data_bytes"] or offset > 4*1024*1024 or data[catalogue.file_offset:catalogue.file_offset+catalogue.size] != expected.ljust(catalogue.size, b"\0"):
        raise ValueError("Release catalogue differs from native asset descriptor")


def reconstruct(project, description, reference, generated, release, object_root=None):
    validate_release(description, release)
    local = descriptor(project, reference, generated, object_root)
    local_records = {identity(row): row for row in local["records"]}
    if set(local_records) != {identity(row) for row in description["records"]}:
        raise ValueError("Local build and release have different native asset inventories")
    image = reference.read_bytes()
    elf = Elf(image)
    payload, rewrites = bytearray(), 0
    for public in description["records"]:
        record = local_records[identity(public)]
        if public["bytes"] != record["bytes"] or [relocation_identity(row) for row in public["relocations"]] != [relocation_identity(row) for row in record["relocations"]]:
            raise ValueError(f"Local native initializer layout differs: {public['symbol']}")
        symbol = elf.find_native(record["symbol"], record["translation_unit"], record["local"])
        data = bytearray(image[symbol.file_offset:symbol.file_offset+symbol.size])
        for relocation in public["relocations"]:
            offset, value = relocation["offset"], relocation["target"]["address"]+relocation["addend"]
            if relocation["type"] != "R_MIPS_32" or not 0 <= offset <= len(data)-4 or not 0 <= value <= 0xffffffff:
                raise ValueError("Invalid native relocation descriptor")
            struct.pack_into("<I", data, offset, value)
            rewrites += 1
        if zlib.crc32(data) != public["crc32"]:
            raise ValueError(f"Local resource does not match release after relocation: {public['symbol']}")
        payload.extend(data)
    if len(payload) != description["native_data_bytes"] or zlib.crc32(payload) != description["payload_crc32"]:
        raise ValueError("Reconstructed native payload differs from release catalogue")
    result = DATA_MAGIC+struct.pack("<4I", 1, len(description["records"]), len(payload), zlib.crc32(payload))+payload
    return result, dict(schema=1, status="PASS", records=len(description["records"]), native_data_bytes=len(payload),
                        relocated_words=rewrites, dat_sha256=hashlib.sha256(result).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--release-elf", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True, help="Your locally built private ELF")
    parser.add_argument("--generated", type=Path, default=Path("build/PS2-ntsc12/generated"))
    parser.add_argument("--object-root", type=Path, help="Local build objects; defaults to generated.parent")
    parser.add_argument("--output", type=Path, required=True, help="Private PS2DATA.DAT output")
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    description = json.loads(args.metadata.read_text(encoding="utf-8"))
    private, result = reconstruct(project, description, args.reference, args.generated, args.release_elf, args.object_root)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix=".ps2data-", suffix=".tmp", dir=args.output.parent, delete=False) as file:
            temporary = Path(file.name)
            file.write(private)
        temporary.replace(args.output)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    print(json.dumps(result))


if __name__ == "__main__":
    main()
