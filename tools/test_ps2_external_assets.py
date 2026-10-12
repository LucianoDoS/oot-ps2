#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check the native-data split and exercise the actual startup loader on a host.

The optional host test uses Linux mmap to represent EE RAM and a cache stub;
it does not access a PS2, emulator, USB drive, or memory card. The structural
test needs the private, unstripped reference ELF and its prepared output.
Neither test establishes distribution rights for the combined game program.
"""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import zlib

from ps2_release_assets import Elf, inventory
from audit_ps2_release_assets import audit
from ps2_release_relocations import descriptor, identity, reconstruct, validate_release


PROJECT = Path(__file__).resolve().parents[1]
CATALOGUE_MAGIC = b"OOTPS2RELASSET01"
DATA_MAGIC = b"OOTPS2DATA01".ljust(16, b"\0")
CATALOGUE_BYTES = 16384


KERNEL_STUB = """
#ifndef EXTERNAL_ASSETS_TEST_KERNEL_H
#define EXTERNAL_ASSETS_TEST_KERNEL_H
void SyncDCache(void*, void*);
#endif
"""

HOST_DRIVER = r'''
#define _GNU_SOURCE
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void* test_malloc(size_t);
#define malloc test_malloc
#include "LOADER_SOURCE"
#undef malloc

static void* test_malloc(size_t size) {
    const char* fail = getenv("PS2_TEST_FAIL_ALLOC");
    return fail && !strcmp(fail, "1") ? NULL : malloc(size);
}

static unsigned cache_calls;
void SyncDCache(void* start, void* end) {
    if ((uintptr_t)start < 0x100000 || (uintptr_t)end > 0x120000 || end <= start) abort();
    cache_calls++;
}

int main(int argc, char** argv) {
    if (argc == 2 && !strcmp(argv[1], "--catalogue-address")) {
        printf("%lu\n", (unsigned long)(uintptr_t)&gPS2ReleaseAssets);
        return 0;
    }
    if (argc != 5) return 90;
    FILE* input = fopen(argv[1], "rb");
    if (!input || fread(&gPS2ReleaseAssets, 1, sizeof(gPS2ReleaseAssets), input) != sizeof(gPS2ReleaseAssets)) return 91;
    fclose(input);
    size_t bytes = 0x20000;
    unsigned char* ram = mmap((void*)0x100000, bytes, PROT_READ|PROT_WRITE,
                             MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE, -1, 0);
    if (ram == MAP_FAILED || ram != (void*)0x100000) { perror("mmap EE fixture"); return 92; }
    memset(ram, 0xa5, bytes);
    int expected = atoi(argv[3]);
    int result = ps2_release_data_load(strcmp(argv[2], "NULL") ? argv[2] : NULL);
    if (result != expected) { fprintf(stderr, "unexpected loader result %d, expected %d\n", result, expected); return 93; }
    if (!expected) {
        if (cache_calls) { fprintf(stderr, "cache flushed after rejected input\n"); return 94; }
        for (size_t i = 0; i < bytes; i++) if (ram[i] != 0xa5) {
            fprintf(stderr, "partial RAM write after rejected input at %zu\n", i); return 95;
        }
    } else {
        input = fopen(argv[4], "rb");
        unsigned char* payload = malloc(gPS2ReleaseAssets.dataBytes);
        if (!input || !payload || fread(payload, 1, gPS2ReleaseAssets.dataBytes, input) != gPS2ReleaseAssets.dataBytes) return 96;
        fclose(input);
        if (cache_calls != gPS2ReleaseAssets.count) return 97;
        for (unsigned i = 0; i < gPS2ReleaseAssets.count; i++) {
            const Record* item = &gPS2ReleaseAssets.records[i];
            if (memcmp((void*)(uintptr_t)item->address, payload+item->offset, item->size)) return 98;
        }
        /* Check gaps as well: restoration must touch only catalogue spans. */
        for (size_t i = 0; i < bytes; i++) {
            int covered = 0;
            for (unsigned j = 0; j < gPS2ReleaseAssets.count; j++) {
                const Record* item = &gPS2ReleaseAssets.records[j];
                uintptr_t address = (uintptr_t)ram+i;
                if (address >= item->address && address < item->address+item->size) covered = 1;
            }
            if (!covered && ram[i] != 0xa5) return 99;
        }
        free(payload);
    }
    munmap(ram, bytes);
    return 0;
}
'''


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def structural_test(reference, prepared, generated):
    original = reference.read_bytes()
    stripped = (prepared / "OOTPS2.ELF").read_bytes()
    private = (prepared / "PS2DATA.DAT").read_bytes()
    check(len(original) == len(stripped), "Split changed ELF length/layout")
    ref, cleaned = Elf(original), Elf(stripped)
    check(ref.entry == cleaned.entry and ref.segments == cleaned.segments, "ELF load segments or entry point changed")
    catalogue = ref.find("gPS2ReleaseAssets")
    check(catalogue.size == CATALOGUE_BYTES, "Unexpected catalogue allocation")
    start = catalogue.file_offset
    table = stripped[start:start+catalogue.size]
    check(table[:16] == CATALOGUE_MAGIC and private[:16] == DATA_MAGIC, "Wrong split magic")
    version, count, size, crc = struct.unpack_from("<4I", table, 16)
    check(private[16:32] == table[16:32], "DAT header is not bound to internal catalogue")
    check(version == 1 and 0 < count <= 512 and size <= 4*1024*1024, "Catalogue bounds invalid")
    check(len(private) == 32+size and zlib.crc32(private[32:]) == crc, "DAT length/CRC invalid")
    selected, absent = inventory(PROJECT, ref, generated, include_private_icon=False)
    check(count == len(selected), "Catalogue does not cover the inventory")
    cursor = 0
    expected = bytearray(original)
    for index, (symbol, _) in enumerate(selected):
        address, length, offset, checksum = struct.unpack_from("<4I", table, 32+16*index)
        check((address, length, offset) == (symbol.address, symbol.size, cursor), f"Bad record for {symbol.name}")
        source = original[symbol.file_offset:symbol.file_offset+symbol.size]
        check(zlib.crc32(source) == checksum, f"Wrong native checksum for {symbol.name}")
        check(private[32+offset:32+offset+length] == source, f"Linked bytes not preserved for {symbol.name}")
        check(not any(stripped[symbol.file_offset:symbol.file_offset+symbol.size]), f"Private initializer remains: {symbol.name}")
        expected[symbol.file_offset:symbol.file_offset+symbol.size] = bytes(symbol.size)
        cursor += symbol.size
    check(cursor == size, "Catalogue payload extent differs")
    check(not any(table[32+16*count:]), "Unexpected uninitialized catalogue tail")
    expected[start:start+catalogue.size] = table
    check(bytes(expected) == stripped, "ELF bytes outside the exact asset spans/catalogue were modified")
    icon = ref.find("ps2_save_icon_model")
    check(original[icon.file_offset:icon.file_offset+icon.size] == (generated / "public/triforce.icn").read_bytes(), "Memory-card mesh is not the procedural output")
    report = audit(PROJECT, prepared / "OOTPS2.ELF", generated, procedural_icon=True)
    check(report["status"] == "PASS", "Audit rejects prepared ELF")
    return dict(status="PASS", records=count, payload_bytes=size, absent_arrays=len(absent),
                exact_bytes_outside_split=True, linked_native_data_restorable=True,
                elf_sha256=hashlib.sha256(stripped).hexdigest())


def portability_test(reference, prepared, generated, alternative):
    description = json.loads((prepared / "native-assets.json").read_text(encoding="utf-8"))
    release = prepared / "OOTPS2.ELF"
    expected = (prepared / "PS2DATA.DAT").read_bytes()
    same, same_report = reconstruct(PROJECT, description, reference, generated, release)
    other, other_report = reconstruct(PROJECT, description, alternative, generated, release)
    check(same == expected and other == expected, "Reconstructed DAT is not byte-identical to frozen release data")
    local = {identity(row): row for row in descriptor(PROJECT, alternative, generated)["records"]}
    changed = sum(a["target"]["address"] != b["target"]["address"]
                  for public in description["records"]
                  for a, b in zip(public["relocations"], local[identity(public)]["relocations"]))

    def rejected(value):
        try:
            validate_release(value, release)
        except ValueError:
            return
        raise AssertionError("Corrupt public metadata was accepted")

    changed_hash = copy.deepcopy(description)
    changed_hash["elf_sha256"] = "0"*64
    rejected(changed_hash)
    changed_crc = copy.deepcopy(description)
    changed_crc["records"][0]["crc32"] ^= 1
    rejected(changed_crc)
    changed_target = copy.deepcopy(description)
    next(row for row in changed_target["records"] if row["relocations"])["relocations"][0]["target"]["address"] ^= 4
    rejected(changed_target)
    return dict(status="PASS", same_layout=same_report, alternative_layout=other_report,
                changed_relocation_targets=changed, byte_identical_private_data=True,
                corrupted_metadata_cases_rejected=3)


def make_catalogue(payload, records):
    header = struct.pack("<4I", 1, len(records), len(payload), zlib.crc32(payload))
    table = CATALOGUE_MAGIC + header + b"".join(struct.pack("<4I", *row) for row in records)
    return table.ljust(CATALOGUE_BYTES, b"\0"), DATA_MAGIC + header + payload


def host_loader_test(work):
    if os.name == "nt":
        raise ValueError("Host loader test requires Linux mmap; run this optional part in WSL")
    compiler = shutil.which(os.environ.get("CC", "gcc"))
    if not compiler:
        raise ValueError("Host C compiler unavailable")
    work.mkdir(parents=True, exist_ok=True)
    (work / "kernel.h").write_text(KERNEL_STUB, encoding="utf-8")
    driver = HOST_DRIVER.replace("LOADER_SOURCE", (PROJECT / "src/platform/ps2/ps2_release_data.c").as_posix())
    (work / "driver.c").write_text(driver, encoding="utf-8")
    executable = work / "loader-test"
    compiled = subprocess.run([compiler, "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
                               "-fno-pie", "-no-pie",
                               "-I", str(work), "-I", str(PROJECT / "include/ps2"),
                               str(work / "driver.c"), "-o", str(executable)], capture_output=True, text=True)
    check(compiled.returncode == 0, "Host loader compile failed:\n" + compiled.stdout + compiled.stderr)
    address_result = subprocess.run([str(executable.resolve()), "--catalogue-address"],
                                    capture_output=True, text=True, check=True)
    catalogue_address = int(address_result.stdout.strip())
    check(0x100000 <= catalogue_address < 0x2000000-CATALOGUE_BYTES,
          "Host catalogue must also have an EE-range address to exercise overlap rejection")
    payload = bytes((index*31+index//251) & 255 for index in range(17013)) + b"second initializer; linked bytes\0"
    first = 17013
    rows = [(0x101000, first, 0, zlib.crc32(payload[:first])),
            (0x109000, len(payload)-first, first, zlib.crc32(payload[first:]))]
    catalogue, data = make_catalogue(payload, rows)
    cases = []

    def run(name, table=catalogue, dat=data, success=False, image="image/OOT.Z64", missing=False, fail_alloc=False):
        folder = work / name
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "catalogue.bin").write_bytes(table)
        (folder / "expected.bin").write_bytes(payload)
        if image.startswith("image/"):
            destination = folder / "image/PS2DATA.DAT"
        elif image in ("mass:OOT.Z64", "host:OOT.Z64"):
            destination = folder / (image.split(":")[0] + ":PS2DATA.DAT")
        else:
            destination = folder / "PS2DATA.DAT"
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not missing:
            destination.write_bytes(dat)
        elif destination.exists():
            destination.unlink()
        result = subprocess.run([str(executable.resolve()), str((folder / "catalogue.bin").resolve()), image,
                                 str(int(success)), str((folder / "expected.bin").resolve())],
                                cwd=folder, capture_output=True, text=True,
                                env=dict(os.environ, PS2_TEST_FAIL_ALLOC=str(int(fail_alloc))))
        check(result.returncode == 0, f"{name}: return {result.returncode}\n{result.stdout}{result.stderr}")
        cases.append(name)

    def patched(table, offset, value):
        result = bytearray(table)
        struct.pack_into("<I", result, offset, value)
        return bytes(result)

    run("valid_chunked_restoration", success=True)
    run("valid_mass_colon_path", success=True, image="mass:OOT.Z64")
    run("valid_host_colon_path", success=True, image="host:OOT.Z64")
    run("missing_dat", missing=True)
    run("allocation_failure", fail_alloc=True)
    run("null_image_path", image="NULL")
    run("no_path_separator", image="OOT.Z64")
    run("oversized_image_path", image="x"*221)
    run("wrong_dat_magic", dat=b"X"+data[1:])
    run("short_dat_header", dat=data[:31])
    run("wrong_build_version", dat=patched(data, 16, 2))
    run("wrong_build_count", dat=patched(data, 20, 1))
    run("wrong_build_bytes", dat=patched(data, 24, len(payload)+1))
    run("wrong_build_crc", dat=patched(data, 28, zlib.crc32(payload)^1))
    run("truncated_payload", dat=data[:-1])
    run("trailing_payload", dat=data+b"extra")
    run("damaged_payload", dat=data[:-1]+bytes([data[-1]^1]))
    # Global checksum valid, second per-record checksum wrong: no first-record write may escape.
    run("per_record_crc_rejection_without_partial_write", table=patched(catalogue, 32+16+12, rows[1][3]^1))
    run("wrong_catalogue_magic", table=b"X"+catalogue[1:])
    run("wrong_catalogue_version", table=patched(catalogue, 16, 2))
    run("empty_catalogue", table=patched(catalogue, 20, 0))
    run("catalogue_count_over_capacity", table=patched(catalogue, 20, 513))
    run("catalogue_bytes_over_limit", table=patched(catalogue, 24, 4*1024*1024+1))
    run("catalogue_empty_record", table=patched(catalogue, 32+4, 0))
    run("catalogue_gap", table=patched(catalogue, 32+16+8, first+1))
    run("catalogue_overlap", table=patched(catalogue, 32+16, rows[0][0]+first-1))
    run("catalogue_unsorted_destinations", table=patched(catalogue, 32+16, 0x100000))
    run("catalogue_destination_below_ee", table=patched(catalogue, 32, 0xfffff))
    run("catalogue_destination_crosses_ee_end", table=patched(catalogue, 32+16, 0x2000000-rows[1][1]+1))
    run("catalogue_cannot_overwrite_itself", table=patched(catalogue, 32+16, catalogue_address))
    run("catalogue_record_exceeds_payload", table=patched(catalogue, 32+16+4, rows[1][1]+1))
    run("catalogue_unconsumed_payload", table=patched(catalogue, 32+16+4, rows[1][1]-1))
    return dict(status="PASS", cases=len(cases), actual_loader_source=True,
                bounded_ee_ram_fixture=True, failures_preserve_ram=True, case_names=cases)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--prepared", type=Path)
    parser.add_argument("--local-reference", type=Path, help="Also reconstruct from this private ELF with another layout")
    parser.add_argument("--generated", type=Path, default=PROJECT / "build/PS2-ntsc12/generated")
    parser.add_argument("--host-loader", action="store_true")
    parser.add_argument("--work", type=Path, default=PROJECT / "build/PS2/external-assets-host-test")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if bool(args.reference) != bool(args.prepared) or not (args.reference or args.host_loader):
        parser.error("Provide both --reference/--prepared, --host-loader, or both test types")
    if args.local_reference and not args.reference:
        parser.error("--local-reference also requires --reference/--prepared")
    result = dict(schema=1, status="PASS", distribution_rights_certified=False)
    if args.reference:
        result["split_structure"] = structural_test(args.reference, args.prepared, args.generated)
    if args.local_reference:
        result["portable_data_generation"] = portability_test(args.reference, args.prepared, args.generated,
                                                              args.local_reference)
    if args.host_loader:
        result["host_loader"] = host_loader_test(args.work.resolve())
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
