#!/usr/bin/env python3
"""Convert the pinned Linux 8x16 font source into a deterministic PSF2 file."""
from __future__ import annotations

import argparse
import re
import struct
from pathlib import Path

MAGIC = 0x864AB572


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit("GKD_UI_FONT=BLOCKED output-exists")
    text = args.source.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    match = re.search(
        r"static const struct font_data\s+\w+\s*=\s*"
        r"\{\s*\{[^}]*\}\s*,\s*\{(.*?)\}\s*\}\s*;",
        text,
        flags=re.S,
    )
    if not match:
        raise SystemExit("GKD_UI_FONT=BLOCKED font-data")
    data = bytes(
        int(value, 16)
        for value in re.findall(r"0x([0-9a-fA-F]{2})", match.group(1))
    )
    if len(data) != 256 * 16:
        raise SystemExit("GKD_UI_FONT=BLOCKED glyph-bytes")
    header = struct.pack("<8I", MAGIC, 0, 32, 0, 256, 16, 16, 8)
    args.output.write_bytes(header + data)
    print(f"GKD_UI_FONT=PASS bytes={len(header) + len(data)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
