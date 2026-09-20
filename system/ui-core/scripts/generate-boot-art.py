#!/usr/bin/env python3
"""Compile the approved art to a small device-palette, 4-bit boot resource.

FFmpeg only resamples to native LCD geometry. Palette quantization is an
offline device-format conversion, not a runtime PNG decoder or new UI stack.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess

SOURCE_SHA256 = "47954e07027addb0057363c39d568f716feab2eee916c2a1b352c9746bf1fe1e"


def rgb565(color):
    r, g, b = bytes.fromhex(color.lstrip("#"))
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def channels(color):
    return ((color >> 11) * 255 // 31, ((color >> 5) & 63) * 255 // 63,
            (color & 31) * 255 // 31)


def blend(dark, color, alpha):
    return sum((((((dark >> shift) & mask) * (255 - alpha) +
                  ((color >> shift) & mask) * alpha + 127) // 255) << shift)
               for shift, mask in ((11, 31), (5, 63), (0, 31)))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--preview-raw", type=Path)
    args = parser.parse_args()
    if args.output.exists() or hashlib.sha256(args.source.read_bytes()).hexdigest() != SOURCE_SHA256:
        raise SystemExit("GKD_BOOT_ART=BLOCKED identity-or-output")
    config = dict(line.split("=", 1) for line in args.config.read_text().splitlines()
                  if line and not line.startswith("#"))
    dark, normal, highlight, white, accent = (rgb565(config[key]) for key in
                                             ("dark", "normal", "highlight", "white", "accent"))
    palette = [dark, normal, highlight, white, accent]
    palette += [blend(dark, color, alpha) for color in (normal, accent)
                for alpha in (24, 48, 80, 128, 192)]
    palette.append(rgb565(config["button_b"]))
    raw = subprocess.run(["ffmpeg", "-v", "error", "-nostdin", "-i", str(args.source),
                          "-vf", "scale=320:240:flags=area", "-pix_fmt", "rgb565le",
                          "-frames:v", "1", "-f", "rawvideo", "pipe:1"],
                         check=True, capture_output=True).stdout
    if len(raw) != 320 * 240 * 2:
        raise SystemExit("GKD_BOOT_ART=BLOCKED geometry")
    rgb = [channels(color) for color in palette]
    lookup = {}
    for color in set(struct.unpack("<76800H", raw)):
        source = channels(color)
        lookup[color] = min(range(16), key=lambda i: sum((source[c] - rgb[i][c]) ** 2 for c in range(3)))
    indices = [lookup[color] for color in struct.unpack("<76800H", raw)]
    packed = bytes(indices[i] | (indices[i + 1] << 4) for i in range(0, len(indices), 2))
    lines = ["/* Generated only in the build directory; approved source " + SOURCE_SHA256 + " */",
             "static const unsigned short gkd_boot_palette[16] = {" +
             ",".join(f"0x{color:04x}" for color in palette) + "};",
             "static const unsigned char gkd_boot_pixels[38400] = {"]
    lines += [",".join(str(value) for value in packed[i:i + 32]) + ","
              for i in range(0, len(packed), 32)]
    lines += ["};"]
    args.output.write_text("\n".join(lines) + "\n", encoding="ascii")
    if args.preview_raw:
        args.preview_raw.write_bytes(struct.pack("<76800H", *(palette[i] for i in indices)))
    print("GKD_BOOT_ART=PASS pixels=76800 palette=16 source=" + SOURCE_SHA256)


if __name__ == "__main__":
    main()
