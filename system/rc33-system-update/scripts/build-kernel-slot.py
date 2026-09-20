#!/usr/bin/env python3
import argparse, gzip, re, struct, zlib
from pathlib import Path

from kernel_slot_common import (
    HEADER,
    RAW_LIMIT,
    SLOT_BYTES,
    parse_capsule_contract,
    sha256_bytes,
)

def integer(value: str) -> int:
    return int(value, 0)

p = argparse.ArgumentParser()
p.add_argument("--source-slot", type=Path, required=True)
p.add_argument("--kernel-build", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
p.add_argument("--name", default="gkd-rc3.7-current")
p.add_argument("--companion", type=Path)
p.add_argument("--companion-header", type=Path)
p.add_argument("--slot-offset", type=integer, default=0)
a = p.parse_args()
if a.output.exists() or a.source_slot.stat().st_size != SLOT_BYTES:
    raise SystemExit("GKDSU_SLOT=BLOCKED input")
if (a.companion is None) != (a.companion_header is None):
    raise SystemExit("GKDSU_SLOT=BLOCKED companion-arguments")
source = a.source_slot.read_bytes()
values = HEADER.unpack(source[:HEADER.size])
if values[0] != 0x27051956 or values[9] != 2:
    raise SystemExit("GKDSU_SLOT=BLOCKED source-header")
raw = (a.kernel_build / "vmlinux.bin").read_bytes()
packed = (a.kernel_build / "vmlinux.bin.gz").read_bytes()
if len(raw) >= RAW_LIMIT or len(packed) + HEADER.size > SLOT_BYTES or gzip.decompress(packed) != raw:
    raise SystemExit("GKDSU_SLOT=BLOCKED size-or-gzip")
header_text = (a.kernel_build / "readelf-header.txt").read_text(encoding="ascii")
entries = re.findall(r"^\s*Entry point address:\s*(0x[0-9a-fA-F]+)\s*$", header_text, re.M)
if len(entries) != 1:
    raise SystemExit("GKDSU_SLOT=BLOCKED entry")
entry = int(entries[0], 16)
try:
    encoded_name = a.name.encode("ascii")
except UnicodeEncodeError as error:
    raise SystemExit("GKDSU_SLOT=BLOCKED name") from error
if not encoded_name or len(encoded_name) > 32 or b"\0" in encoded_name:
    raise SystemExit("GKDSU_SLOT=BLOCKED name")
name = encoded_name.ljust(32, b"\0")
header = HEADER.pack(0x27051956, 0, values[2], len(packed), values[4], entry,
                     zlib.crc32(packed) & 0xffffffff, values[7], values[8], 2, 1, name)
header = header[:4] + struct.pack(">I", zlib.crc32(header) & 0xffffffff) + header[8:]
kernel_image = header + packed
companion_summary = "none"
if a.companion is None:
    slot = kernel_image + bytes(SLOT_BYTES - len(kernel_image))
else:
    companion = a.companion.read_bytes()
    try:
        relative_offset, capsule_bytes, capsule_hash = parse_capsule_contract(
            a.companion_header, a.slot_offset
        )
    except (OSError, UnicodeError, ValueError) as error:
        raise SystemExit("GKDSU_SLOT=BLOCKED companion-header") from error
    if (
        len(kernel_image) > relative_offset
        or len(companion) != capsule_bytes
        or sha256_bytes(companion) != capsule_hash
    ):
        raise SystemExit("GKDSU_SLOT=BLOCKED companion-integrity")
    slot = (
        kernel_image
        + bytes(relative_offset - len(kernel_image))
        + companion
        + bytes(SLOT_BYTES - relative_offset - len(companion))
    )
    companion_summary = "0x%x/%d/%s" % (
        relative_offset, capsule_bytes, capsule_hash
    )
check = HEADER.unpack(slot[:HEADER.size])
if check[5] != entry or check[3] != len(packed):
    raise SystemExit("GKDSU_SLOT=BLOCKED readback")
a.output.write_bytes(slot)
print(
    "GKDSU_SLOT=PASS sha256=%s raw_bytes=%d gzip_bytes=%d entry=0x%08x "
    "companion=%s" % (
        sha256_bytes(slot), len(raw), len(packed), entry, companion_summary
    )
)
