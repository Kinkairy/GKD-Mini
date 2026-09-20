#!/usr/bin/env python3
import argparse
import gzip
import zlib
from pathlib import Path

from kernel_slot_common import (
    HEADER,
    SLOT_BYTES,
    parse_capsule_contract,
    sha256_bytes,
)


def integer(value: str) -> int:
    return int(value, 0)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--slot", type=Path, required=True)
    parser.add_argument("--raw", type=Path, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--companion", type=Path)
    parser.add_argument("--companion-header", type=Path)
    parser.add_argument("--slot-offset", type=integer, default=0)
    arguments = parser.parse_args()
    if (arguments.companion is None) != (arguments.companion_header is None):
        raise SystemExit("GKDSU_SLOT_VERIFY=BLOCKED companion-arguments")
    slot = arguments.slot.read_bytes()
    raw = arguments.raw.read_bytes()
    if len(slot) != SLOT_BYTES:
        raise SystemExit("GKDSU_SLOT_VERIFY=BLOCKED size")
    fields = HEADER.unpack(slot[:HEADER.size])
    magic, header_crc, timestamp, data_size, load, entry, data_crc = fields[:7]
    os_id, arch, image_type, compression, encoded_name = fields[7:]
    header_zero_crc = slot[:4] + bytes(4) + slot[8:HEADER.size]
    data = slot[HEADER.size:HEADER.size + data_size]
    kernel_end = HEADER.size + data_size
    tail = slot[kernel_end:]
    expected_name = arguments.name.encode("ascii")
    if (magic != 0x27051956 or
            zlib.crc32(header_zero_crc) & 0xFFFFFFFF != header_crc or
            zlib.crc32(data) & 0xFFFFFFFF != data_crc or
            image_type != 2 or compression != 1 or arch != 5 or os_id != 5 or
            encoded_name.rstrip(b"\0") != expected_name or
            gzip.decompress(data) != raw):
        raise SystemExit("GKDSU_SLOT_VERIFY=BLOCKED integrity")
    if arguments.companion is None:
        if any(tail):
            raise SystemExit("GKDSU_SLOT_VERIFY=BLOCKED zero-tail")
        companion_summary = "none"
        zero_bytes = len(tail)
    else:
        companion = arguments.companion.read_bytes()
        try:
            relative_offset, capsule_bytes, capsule_hash = parse_capsule_contract(
                arguments.companion_header, arguments.slot_offset
            )
        except (OSError, UnicodeError, ValueError) as error:
            raise SystemExit("GKDSU_SLOT_VERIFY=BLOCKED companion-header") from error
        if (
            kernel_end > relative_offset
            or len(companion) != capsule_bytes
            or sha256_bytes(companion) != capsule_hash
            or any(slot[kernel_end:relative_offset])
            or slot[relative_offset:relative_offset + capsule_bytes] != companion
            or any(slot[relative_offset + capsule_bytes:])
        ):
            raise SystemExit("GKDSU_SLOT_VERIFY=BLOCKED companion-integrity")
        zero_bytes = (
            relative_offset - kernel_end
            + SLOT_BYTES - relative_offset - capsule_bytes
        )
        companion_summary = "0x%x/%d/%s" % (
            relative_offset, capsule_bytes, capsule_hash
        )
    print(
        "GKDSU_SLOT_VERIFY=PASS sha256=%s raw_sha256=%s timestamp=%u "
        "load=0x%08x entry=0x%08x zero_bytes=%u companion=%s" % (
            sha256_bytes(slot), sha256_bytes(raw), timestamp, load, entry,
            zero_bytes, companion_summary,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
