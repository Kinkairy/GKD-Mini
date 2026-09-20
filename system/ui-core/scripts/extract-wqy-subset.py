#!/usr/bin/env python3
"""Reproduce the two native CJK subsets from the pinned upstream PCF files.
Requires Pillow (the PCF decoder); runtime and normal firmware builds do not.
"""
import argparse, hashlib, json, struct
from pathlib import Path
from PIL import PcfFontFile, Image

EXPECTED = {12: "923c0836c9ae367ce422717c851a5fc7ba2326438f4fa88740e4105cfdfbe22f",
            14: "4fdd52dd970801b0989ce1c30a894455af25be8230fda06b49930233b4ce71df"}

def extract(path, size, codepoints):
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != EXPECTED[size]:
        raise ValueError("upstream PCF hash mismatch")
    with path.open("rb") as fp:
        font = PcfFontFile.PcfFontFile(fp)
        metrics = font._load_metrics()
        bitmaps = font._load_bitmaps(metrics)
        fmt, _, offset = font.toc[PcfFontFile.PCF_BDF_ENCODINGS]
        endian = ">" if fmt & 4 else "<"
        low, high, first, last, _ = struct.unpack_from(endian + "5H", raw, offset + 4)
        count = (high - low + 1) * (last - first + 1)
        indices = struct.unpack_from(endian + str(count) + "H", raw, offset + 14)
        rows = []
        for cp in codepoints:
            if not (low <= cp & 255 <= high and first <= cp >> 8 <= last):
                raise ValueError("missing codepoint")
            index = indices[((cp >> 8) - first) * (high - low + 1) + (cp & 255) - low]
            if index == 65535:
                raise ValueError("missing glyph")
            width, height, left, _, advance, ascent, descent, _ = metrics[index]
            # Preserve the CJK design grid and baseline; do not resize/retrace.
            top = (13 if size == 14 else 10) - ascent
            if advance != size or left < 0 or top < 0 or left + width > size or top + height > size:
                raise ValueError("glyph exceeds native CJK cell")
            cell = Image.new("1", (size, size))
            cell.paste(bitmaps[index], (left, top))
            rows.append(f"{cp:04X}:{cell.tobytes().hex().upper()}")
        return ("\n".join(rows) + "\n").encode("ascii")

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--pcf14", type=Path, required=True)
    p.add_argument("--pcf12", type=Path, required=True)
    p.add_argument("--output-dir", type=Path, required=True)
    args = p.parse_args()
    root = Path(__file__).resolve().parent.parent
    codepoints = sorted({ord(c) for c in (root / "include/gkd-ui-language.def").read_text() if ord(c) > 255})
    for size, path in [(12, args.pcf12), (14, args.pcf14)]:
        data = extract(path, size, codepoints)
        with (args.output_dir / f"native-cn-{size}.hex").open("xb") as f:
            f.write(data)
        print(f"WQY_SUBSET_PASS size={size} glyphs={len(codepoints)} sha256={hashlib.sha256(data).hexdigest()}")

if __name__ == "__main__":
    main()
