#!/usr/bin/env python3
"""Rebase HakiOS integration hooks onto the pinned U-Boot tree.

The original hakios.patch was authored against older file contexts and does
not apply to v2026.07. This script ports the required Kconfig/Makefile/EFI
hooks and small Windows-host fixes for U-Boot's fixdep utility. The DWC3 and
fastboot behavior changes remain deferred.
"""
from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: rebase_hakios_patch.py U_BOOT_SOURCE")

root = Path(sys.argv[1])

def insert_before(rel: str, anchor: str, block: str, marker: str) -> None:
    path = root / rel
    text = path.read_text(encoding="utf-8")
    if marker in text:
        return
    if anchor not in text:
        raise SystemExit(f"{rel}: anchor not found: {anchor!r}")
    path.write_text(text.replace(anchor, block + anchor, 1), encoding="utf-8", newline="\n")

insert_before(
    "cmd/Kconfig",
    "config CMD_BOOTEFI\n",
    '''config CMD_HAKIOS
\tbool "hakios: start the HakiOS loader from memory"
\tdepends on CMD_BOOTEFI && LMB
\thelp
\t  Take the HakiOS loader (loader.efi) and its XNUP payload from the
\t  ramdisk the previous bootloader loaded, or from anywhere in RAM, put
\t  the payload where the loader looks for it and start the loader.

''',
    "config CMD_HAKIOS",
)

insert_before(
    "cmd/Makefile",
    "obj-$(CONFIG_CMD_BOOTEFI) += bootefi.o\n",
    "obj-$(CONFIG_CMD_HAKIOS) += hakios.o hakios_sum.o\n",
    "obj-$(CONFIG_CMD_HAKIOS)",
)

insert_before(
    "include/efi_loader.h",
    "efi_status_t efi_disks_register(void);\n",
    "/* HakiOS USB transport protocol registration. */\nefi_status_t efi_hakios_usb_register(void);\n",
    "efi_hakios_usb_register",
)

insert_before(
    "lib/efi_loader/Kconfig",
    "config EFI_ECPT\n",
    '''config EFI_HAKIOS_USB_DEVICE
\tbool "HakiOS EFI USB device protocol"
\tdepends on USB_GADGET && DM_USB_GADGET
\thelp
\t  Provide the HakiOS USB transport on U-Boot's generic gadget stack.
\t  Enable it for boards with a working USB device controller driver.

''',
    "config EFI_HAKIOS_USB_DEVICE",
)

insert_before(
    "lib/efi_loader/Makefile",
    "obj-$(CONFIG_EFI_ECPT) += efi_conformance.o\n",
    "obj-$(CONFIG_EFI_HAKIOS_USB_DEVICE) += efi_hakios_usb.o\n",
    "obj-$(CONFIG_EFI_HAKIOS_USB_DEVICE)",
)

insert_before(
    "lib/efi_loader/efi_setup.c",
    "if (IS_ENABLED(CONFIG_EFI_RISCV_BOOT_PROTOCOL)) {\n",
    '''if (IS_ENABLED(CONFIG_EFI_HAKIOS_USB_DEVICE)) {
\t\tret = efi_hakios_usb_register();
\t\tif (ret != EFI_SUCCESS)
\t\t\tgoto out;
\t}

''',
    "efi_hakios_usb_register();",
)

# MSYS2/UCRT opens files in text mode unless O_BINARY is requested. fixdep
# compares read()'s byte count with stat().st_size, so CRLF translation made
# the host build fail with "fixdep: read: No error".
fixdep_path = root / "scripts/basic/fixdep.c"
fixdep = fixdep_path.read_text(encoding="utf-8")

if "open(filename, O_RDONLY | O_BINARY)" not in fixdep:
    anchor = "#include <ctype.h>\n"
    if anchor not in fixdep:
        raise SystemExit("scripts/basic/fixdep.c: include anchor not found")
    if "#ifndef O_BINARY" not in fixdep:
        fixdep = fixdep.replace(
            anchor,
            anchor + "\n#ifndef O_BINARY\n#define O_BINARY 0\n#endif\n",
            1,
        )
    old = "fd = open(filename, O_RDONLY);"
    if old not in fixdep:
        raise SystemExit("scripts/basic/fixdep.c: open() anchor not found")
    fixdep = fixdep.replace(old, "fd = open(filename, O_RDONLY | O_BINARY);", 1)

# The binary-mode read leaves CRLF intact. Treat CR as whitespace too, so
# dependency paths are not mistaken for a filename consisting of '\\r'.
skip_old = r"while (*m == ' ' || *m == '\\' || *m == '\n')"
skip_new = r"while (*m == ' ' || *m == '\\' || *m == '\n' || *m == '\r')"
token_old = r"while (*p && *p != ' ' && *p != '\\' && *p != '\n')"
token_new = r"while (*p && *p != ' ' && *p != '\\' && *p != '\n' && *p != '\r')"
if skip_old in fixdep:
    fixdep = fixdep.replace(skip_old, skip_new, 1)
if token_old in fixdep:
    fixdep = fixdep.replace(token_old, token_new, 1)
if skip_new not in fixdep or token_new not in fixdep:
    raise SystemExit("scripts/basic/fixdep.c: CRLF whitespace patch did not apply")

fixdep_path.write_text(fixdep, encoding="utf-8", newline="\n")
print(f"HakiOS integration hooks applied to {root}")
