#!/usr/bin/env python3
"""Shared uImage and embedded recovery-capsule layout contracts."""
from __future__ import annotations

import hashlib
import re
import struct
from pathlib import Path


SLOT_BYTES = 0x600000
RAW_LIMIT = 0x800000
COMPANION_ALIGNMENT = 0x10000
HEADER = struct.Struct(">7I4B32s")


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_capsule_contract(header: Path, slot_offset: int) -> tuple[int, int, str]:
    text = header.read_text(encoding="ascii")
    offset_match = re.search(
        r"^#define R84_RECOVERY_CAPSULE_OFFSET \(\(off_t\)0x([0-9a-f]+)\)$",
        text,
        re.MULTILINE,
    )
    bytes_match = re.search(
        r"^#define R84_RECOVERY_CAPSULE_BYTES 0x([0-9a-f]+)UL$",
        text,
        re.MULTILINE,
    )
    hash_match = re.search(
        r"static const unsigned char r84_recovery_capsule_sha256\[32\] =\s*"
        r"\{([^}]+)\};",
        text,
        re.MULTILINE,
    )
    if not offset_match or not bytes_match or not hash_match:
        raise ValueError("capsule-header")
    hash_octets = re.findall(r"0x([0-9a-f]{2})", hash_match.group(1))
    if len(hash_octets) != 32:
        raise ValueError("capsule-hash")
    absolute_offset = int(offset_match.group(1), 16)
    capsule_bytes = int(bytes_match.group(1), 16)
    relative_offset = absolute_offset - slot_offset
    if (
        capsule_bytes <= 0
        or relative_offset < HEADER.size
        or relative_offset % COMPANION_ALIGNMENT
        or relative_offset + capsule_bytes > SLOT_BYTES
    ):
        raise ValueError("capsule-layout")
    return relative_offset, capsule_bytes, "".join(hash_octets)
