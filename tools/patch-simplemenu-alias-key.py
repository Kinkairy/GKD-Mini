#!/usr/bin/env python3
"""Patch the verified RC3.7 SimpleMenu ELF to use full ROM alias keys.

The accepted MIPS binary inlines getRomRealName into loadGameList. Its
stripGameName call at VMA 0x414ee4 has ``move s1,v0`` in its delay slot, where
v0 points to an allocated duplicate. Suppress the call, leaving the duplicate
and its existing cleanup intact. Changing s1 to the original ROM pointer would
make the later free(s1) release memory it does not own.
"""
import argparse
import hashlib
import struct
from pathlib import Path

BASE_SHA256 = "e4373a1d0ebe4e4474b5b9aa4a753425eb3ec106e05c38b870a5e726608a17e8"
PATCHED_SHA256 = "ed4bfea0f7c9e65e471a551015783b2c6b652a0415444d3a4f1ee5705471b1b7"
OFFSET = 0x14EE4
OLD_WORD = 0x0320F809  # jalr t9: stripGameName(duplicate)
DELAY_SLOT_OFFSET = 0x14EE8
DELAY_SLOT_WORD = 0x00408825  # move s1,v0: retain allocated duplicate
NEW_WORD = 0x00000000  # nop: keep the full key


def patch(source: Path, target: Path) -> None:
    if source.is_symlink() or not source.is_file() or target.exists():
        raise ValueError("source must be a regular file and target must be new")
    before = source.read_bytes()
    if hashlib.sha256(before).hexdigest() != BASE_SHA256:
        raise ValueError("SimpleMenu source ELF does not match accepted RC3.7")
    if struct.unpack_from("<I", before, OFFSET)[0] != OLD_WORD:
        raise ValueError("MIPS alias-key instruction mismatch")
    if struct.unpack_from("<I", before, DELAY_SLOT_OFFSET)[0] != DELAY_SLOT_WORD:
        raise ValueError("MIPS alias-key delay slot mismatch")
    after = bytearray(before)
    struct.pack_into("<I", after, OFFSET, NEW_WORD)
    if hashlib.sha256(after).hexdigest() != PATCHED_SHA256:
        raise ValueError("patched ELF checksum mismatch")
    target.write_bytes(after)
    if target.read_bytes() != after:
        target.unlink()
        raise OSError("patched ELF readback mismatch")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("target", type=Path)
    args = parser.parse_args()
    patch(args.source, args.target)
    print("GKD_SIMPLEMENU_ALIAS_PATCH=PASS sha256=" + PATCHED_SHA256)
