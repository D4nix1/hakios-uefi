#!/usr/bin/env python3
"""Static validator for HakiOS Android boot-image v2 packaging; never boots a device."""
import argparse
import struct
import zlib
from pathlib import Path

PAGE = 4096
KERNEL_ADDR = 0x80008000
TAGS_ADDR = 0x80000100
RAMDISK_ADDR = 0xA1000000
DTB_ADDR = 0x81F00000


def align(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1)


def inside_bank(start, end):
    banks = (
        (0x80000000, 0xBBB00000),
        (0xC0000000, 0x180000000),
        (0x180000000, 0x280000000),
    )
    return any(start >= low and end <= high for low, high in banks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--require-payload", action="store_true")
    args = parser.parse_args()

    blob = args.image.read_bytes()
    if len(blob) < PAGE or blob[:8] != b"ANDROID!":
        raise SystemExit("FAIL: missing Android boot-image magic")

    fields = struct.unpack_from("<10I", blob, 8)
    kernel_size, kernel_addr, ramdisk_size, ramdisk_addr = fields[:4]
    second_size, _second_addr, tags_addr, page_size, version, _os_version = fields[4:]
    recovery_size = struct.unpack_from("<I", blob, 1632)[0]
    header_size = struct.unpack_from("<I", blob, 1644)[0]
    dtb_size = struct.unpack_from("<I", blob, 1648)[0]
    dtb_addr = struct.unpack_from("<Q", blob, 1652)[0]

    if version != 2 or page_size != PAGE or header_size != 1660:
        raise SystemExit(
            f"FAIL: expected boot v2/page 4096/header 1660; got v{version}/{page_size}/{header_size}"
        )
    if (kernel_addr, ramdisk_addr, tags_addr, dtb_addr) != (
        KERNEL_ADDR, RAMDISK_ADDR, TAGS_ADDR, DTB_ADDR
    ):
        raise SystemExit("FAIL: load addresses differ from the Fogos memory profile")
    if not kernel_size or not ramdisk_size or not dtb_size:
        raise SystemExit("FAIL: kernel, ramdisk and DTB must be non-empty")

    kernel_off = PAGE
    ramdisk_off = kernel_off + align(kernel_size, PAGE)
    dtb_off = ramdisk_off + align(ramdisk_size, PAGE) + align(recovery_size, PAGE)
    if dtb_off + dtb_size > len(blob):
        raise SystemExit("FAIL: image ends before the declared DTB")
    if len(blob) != dtb_off + align(dtb_size, PAGE):
        raise SystemExit("FAIL: unexpected trailing or missing image data")

    kernel = blob[kernel_off:kernel_off + kernel_size]
    ramdisk = blob[ramdisk_off:ramdisk_off + ramdisk_size]
    dtb = blob[dtb_off:dtb_off + dtb_size]
    if len(kernel) < 64 or kernel[0x38:0x3c] != b"ARM\x64":
        raise SystemExit("FAIL: kernel section is not a raw AArch64 Image")
    if ramdisk[:4] != b"HKUB":
        raise SystemExit("FAIL: ramdisk is not an HKUB container")
    if dtb[:4] != b"\xd0\x0d\xfe\xed" or struct.unpack_from(">I", dtb, 4)[0] != dtb_size:
        raise SystemExit("FAIL: DTB magic/totalsize mismatch")

    magic, hkub_version, count, table_size = struct.unpack_from("<4sIII", ramdisk, 0)
    if magic != b"HKUB" or hkub_version != 1 or table_size < 16 + 32 * count:
        raise SystemExit("FAIL: malformed HKUB header")
    payload_found = False
    loader_found = False
    for index in range(count):
        name, offset, size, crc, _reserved = struct.unpack_from("<16sIIII", ramdisk, 16 + 32 * index)
        name = name.split(b"\0", 1)[0].decode("ascii")
        if offset < align(table_size, 0x1000) or offset + size > len(ramdisk):
            raise SystemExit(f"FAIL: HKUB entry {name!r} is out of bounds")
        data = ramdisk[offset:offset + size]
        if zlib.crc32(data) != crc:
            raise SystemExit(f"FAIL: HKUB CRC mismatch for {name!r}")
        if name == "payload":
            payload_found = data[:4] == b"XNUP" and len(data) >= 8
            if not payload_found:
                raise SystemExit("FAIL: payload entry does not contain XNUP")
        if name == "loader":
            loader_found = data[:2] == b"MZ"
            if not loader_found:
                raise SystemExit("FAIL: loader entry is not a PE/UEFI image")
    if not loader_found:
        raise SystemExit("FAIL: HKUB loader entry is missing")
    if args.require_payload and not payload_found:
        raise SystemExit("FAIL: HKUB XNUP payload entry is missing")

    ranges = [
        ("kernel", kernel_addr, kernel_addr + kernel_size),
        ("ramdisk", ramdisk_addr, ramdisk_addr + ramdisk_size),
        ("dtb", dtb_addr, dtb_addr + dtb_size),
    ]
    for name, start, end in ranges:
        if not inside_bank(start, end):
            raise SystemExit(f"FAIL: {name} range is outside known DRAM banks")
    for i, (name_a, start_a, end_a) in enumerate(ranges):
        for name_b, start_b, end_b in ranges[i + 1:]:
            if start_a < end_b and start_b < end_a:
                raise SystemExit(f"FAIL: {name_a} overlaps {name_b}")

    print(
        f"PASS: Android boot v2; kernel={kernel_size} @ {kernel_addr:#x}; "
        f"HKUB={ramdisk_size} @ {ramdisk_addr:#x}; DTB={dtb_size} @ {dtb_addr:#x}; "
        f"entries={count}; payload={'yes' if payload_found else 'no'}"
    )


if __name__ == "__main__":
    main()
