#!/usr/bin/env python3
"""Pack a board's U-Boot image, EFI loader, and optional XNUP payload for RAM boot."""
import argparse
import hashlib
import struct
import zlib
from pathlib import Path

PAGE = 4096
KERNEL_ADDR = 0x80008000
TAGS_ADDR = 0x80000100
RAMDISK_ADDR = 0xA1000000
DTB_ADDR = 0x81F00000
HKUB_MAGIC = b"HKUB"
HKUB_VERSION = 1
PAYLOAD_MAGIC = b"XNUP"
PAYLOAD_LIMIT = 0x04000000 - 0x100000


def hkub(entries):
    """Build U-Boot's HKUB ramdisk container, with 4 KiB aligned entries and CRC-32."""
    header_size = 16 + 32 * len(entries)
    first = (header_size + 0xFFF) & ~0xFFF
    table = bytearray()
    body = bytearray()
    for name, data in entries:
        encoded = name.encode("ascii")
        if len(encoded) >= 16:
            raise ValueError(f"HKUB entry name is too long: {name}")
        offset = first + len(body)
        table += struct.pack("<16sIIII", encoded, offset, len(data), zlib.crc32(data), 0)
        body += data
        body += b"\0" * (-len(body) % 0x1000)
    head = struct.pack("<4sIII", HKUB_MAGIC, HKUB_VERSION, len(entries), header_size) + table
    return head + b"\0" * (first - len(head)) + body


def boot_image(kernel, ramdisk, dtb):
    """Build an Android boot-image v2: raw ARM64 Image, HKUB ramdisk, separate DTB."""
    sha = hashlib.sha1()
    for part in (kernel, ramdisk, b"", b"", dtb):
        sha.update(part)
        sha.update(struct.pack("<I", len(part)))

    header = struct.pack(
        "<8s10I16s512s32s1024s",
        b"ANDROID!",
        len(kernel), KERNEL_ADDR,
        len(ramdisk), RAMDISK_ADDR,
        0, 0, TAGS_ADDR,
        PAGE, 2, 0,
        b"fogos", b"", sha.digest(), b"",
    )
    # v2 tail: recovery_dtbo_size, recovery_dtbo_offset, header_size,
    # followed by dtb_size and the physical dtb_addr.
    header += struct.pack("<IQI", 0, 0, 1660)
    header += struct.pack("<IQ", len(dtb), DTB_ADDR)
    if len(header) != 1660:
        raise ValueError(f"internal v2 header size mismatch: {len(header)}")

    def pad(data):
        return data + b"\0" * (-len(data) % PAGE)

    return pad(header) + pad(kernel) + pad(ramdisk) + pad(dtb)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--u-boot", type=Path, required=True)
    p.add_argument("--loader", type=Path, required=True)
    p.add_argument("--dtb", type=Path, required=True)
    p.add_argument("--env", type=Path, help="optional board environment file")
    p.add_argument("--payload", type=Path, help="optional XNUP payload")
    p.add_argument("--hkub-output", type=Path, help="also write the standalone HKUB container for SD-card boot")
    p.add_argument("-o", "--output", type=Path, default=Path("build/hakios-u-boot.img"))
    a = p.parse_args()

    for path in (a.u_boot, a.loader, a.dtb, a.env):
        if path is None:
            continue
        if not path.is_file():
            p.error(f"missing {path}")
    ub = a.u_boot.read_bytes()
    if len(ub) < 64 or ub[0x38:0x3C] != b"ARM\x64":
        p.error(f"{a.u_boot} is not an AArch64 Linux image (expected ARMd header)")
    loader = a.loader.read_bytes()
    if not loader.startswith(b"MZ"):
        p.error(f"{a.loader} is not a UEFI application")
    dtb = a.dtb.read_bytes()
    if len(dtb) < 40 or dtb[:4] != b"\xd0\x0d\xfe\xed":
        p.error(f"{a.dtb} is not a flattened device tree")
    dtb_size = struct.unpack_from(">I", dtb, 4)[0]
    if dtb_size < 40 or dtb_size > len(dtb):
        p.error(f"{a.dtb} has an invalid FDT totalsize {dtb_size}")
    dtb = dtb[:dtb_size]

    entries = [("loader", loader)]
    if a.payload:
        payload = a.payload.read_bytes()
        if len(payload) < 8 or payload[:4] != PAYLOAD_MAGIC:
            p.error(f"{a.payload} is not an XNUP payload")
        if len(payload) > PAYLOAD_LIMIT:
            p.error(f"payload exceeds the 63 MiB UEFI reservation ({len(payload)} bytes)")
        entries.insert(0, ("payload", payload))
    if a.env:
        entries.append(("env", a.env.read_bytes()))
    ramdisk = hkub(entries)
    if len(ramdisk) > 0x04000000:
        p.error(f"HKUB ramdisk exceeds its 64 MiB load window ({len(ramdisk)} bytes)")
    if a.hkub_output:
        a.hkub_output.parent.mkdir(parents=True, exist_ok=True)
        a.hkub_output.write_bytes(ramdisk)
        print(f"{a.hkub_output}: {len(ramdisk)} bytes; standalone HKUB container")

    # U-Boot is a raw ARM64 Image, not gzip. ABL's boot-image v2 parser loads
    # the DTB as its own section; appending it to the kernel stream is invalid.
    image = boot_image(ub, ramdisk, dtb)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_bytes(image)
    print(f"{a.output}: {len(image)} bytes; boot v2, raw U-Boot {len(ub)}, "
          f"loader {len(loader)}, payload {len(entries[0][1]) if entries[0][0] == 'payload' else 0}, "
          f"HKUB ramdisk {len(ramdisk)}, DTB {len(dtb)}")
    print(f"SHA256 {hashlib.sha256(image).hexdigest()}")


if __name__ == "__main__":
    main()
