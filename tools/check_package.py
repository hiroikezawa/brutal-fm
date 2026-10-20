#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Hiro Ikezawa
"""Check an FM-1 package (.fwsc) offline, before it goes near a device:

  tools/check_package.py build/brutal.fwsc [build/brutal.bin build/loader/ota.bin] [--ref other.fwsc]

  - the identity the installers read (one marker byte after each of the first 20 blocks) is FM-1_7XY, BRUTAL FM's
    own range (stock FM-1_0XY, Felucca FM-1_9XY are never reused)
  - the package's file table decodes (JieLi UFW header and entries, CRC-16 of each file)
  - flash.bin is exactly the size the update entry accepts (ota.c OTA_FL_LEN 0x93000)
  - its head (SPL, the key blob, the JLFS entries) decodes; the app area decrypts (SFC, the chip key) to the app
    image byte for byte, and its CRC matches the app entry
  - ota.bin is the update loader built here and carries the loader marker the installers look for
  - with --ref: the parts that must not differ from a known-good package (the SDK SPL and the head layout) do not
Exit status 0 when every check passes."""
import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fm1_install  # noqa: E402
import fm1pkg_make as pk  # noqa: E402

fails = 0


def check(ok, msg):
    global fails
    print(("  ok    " if ok else "  FAIL  ") + msg)
    if not ok:
        fails += 1


def ufw_files(logical):
    hdr = bytearray(logical[:0x40])
    pk.enc(hdr, 0, 0x40)
    total, nfiles = struct.unpack_from("<IH", hdr, 4)
    chip = bytes(hdr[16:16 + 6])
    files = []
    for i in range(nfiles):
        e = bytearray(logical[0x40 + i * 0x50:0x40 + (i + 1) * 0x50])
        pk.enc(e, 0, 0x50)
        typ, idx, crc, _, off, ln, _ = struct.unpack_from("<HHHHIII", e, 0)
        name = bytes(e[0x40:0x50]).split(b"\0")[0].decode()
        files.append((name, typ, crc, logical[off:off + ln]))
    return total, chip, files


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("package")
    ap.add_argument("app", nargs="?", default="build/brutal.bin")
    ap.add_argument("ota", nargs="?", default="build/loader/ota.bin")
    ap.add_argument("--ref", help="a known-good package to compare the head with (e.g. Felucca's)")
    a = ap.parse_args()
    raw = Path(a.package).read_bytes()
    print(f"== {a.package} ({len(raw)} B)")
    product = fm1_install.product_of(raw)
    check(product.startswith("FM-1_7") and len(product) == 8, f"identity {product} (BRUTAL FM range FM-1_7XY)")
    check(fm1_install.model_of(product) == "FM-1", "model FM-1: the installers and M-VAVE's updater talk to it")
    logical = fm1_install.logical_image(raw)
    total, chip, files = ufw_files(logical)
    check(chip == b"AC791N", f"UFW header: chip {chip.decode(errors='replace')}, {len(files)} files, {total} B")
    names = {f[0]: f for f in files}
    check(set(names) == {"flash.bin", "ota.bin"}, f"files: {', '.join(names)}")
    for name, typ, crc, data in files:
        check(pk.crc16(data) == crc, f"{name}: CRC-16 {crc:#06x} matches its {len(data)} bytes")
    flash = names["flash.bin"][3]
    check(len(flash) == 0x93000, f"flash.bin is {len(flash):#x} B (the update entry takes exactly 0x93000)")
    # the app area: SFC-decrypt and compare
    app = Path(a.app).read_bytes()
    region = bytearray(flash[0x4000:0x4000 + 0x120 + pk.APP_SLOT])
    pk.sfc(region, 0, len(region), 0, pk.KEY)
    got = bytes(region[0x120:0x120 + len(app)])
    check(got == app, f"the app area decrypts to the app image ({len(app)} B of a {pk.APP_SLOT} B slot)")
    check(all(b == 0xFF for b in region[0x120 + len(app):0x120 + pk.APP_SLOT]), "the rest of the slot is erased (0xFF)")
    ent = bytearray(region[32:64])
    crc_app = struct.unpack_from("<H", ent, 2)[0]
    padded = app + b"\xFF" * (pk.APP_SLOT - len(app))
    check(pk.crc16(padded) == crc_app, f"the app entry's CRC ({crc_app:#06x}) is the app's")
    check(got[:4] == bytes.fromhex("04818000"), "the image starts with the entry stub (as every FM-1 app)")
    ota = names["ota.bin"][3]
    check(ota == Path(a.ota).read_bytes(), "ota.bin is the update loader built here")
    check(fm1_install.LOADER_MARK in raw, f"the loader marker {fm1_install.LOADER_MARK.decode()} is in the package "
          "(the installers accept it)")
    plain = Path(a.ota).with_name("loader.bin")             # (ota.bin holds it LZ4-compressed)
    check(plain.exists() and b"ota-FM-1_700" in plain.read_bytes(), "the loader identifies itself as ota-FM-1_700")
    if a.ref:
        ref = Path(a.ref).read_bytes()
        rlog = fm1_install.logical_image(ref)
        _, _, rfiles = ufw_files(rlog)
        rflash = {f[0]: f for f in rfiles}["flash.bin"][3]
        check(rflash[:0x4000] == flash[:0x4000], "the flash head (SPL, key blob, JLFS entries) equals the reference")
        check(len(ref) == len(raw), f"the same package size as the reference ({len(ref)} B)")
    print(f"package: {'all checks passed' if not fails else f'{fails} FAILED'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
