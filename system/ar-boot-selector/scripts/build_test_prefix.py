#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

PREFIX_BYTES = 20 * 1024 * 1024
SECTOR_BYTES = 512
ENV = slice(0x1000 * SECTOR_BYTES, 0x1008 * SECTOR_BYTES)
SELECTOR = slice(0x1008 * SECTOR_BYTES, 0x1010 * SECTOR_BYTES)
ZERO_GAP = slice(0x1008 * SECTOR_BYTES, 0x1800 * SECTOR_BYTES)
R_SLOT = slice(0x1800 * SECTOR_BYTES, 0x4800 * SECTOR_BYTES)
A_SLOT = slice(0x4800 * SECTOR_BYTES, 0x7800 * SECTOR_BYTES)
CAPSULE = slice(0x7800 * SECTOR_BYTES, 0x9E80 * SECTOR_BYTES)
GUARD = slice(0x9E80 * SECTOR_BYTES, 0x9F00 * SECTOR_BYTES)
TRACE = slice(0x9F00 * SECTOR_BYTES, 0x9FF8 * SECTOR_BYTES)
REQUEST = slice(0x9FF8 * SECTOR_BYTES, 0xA000 * SECTOR_BYTES)
HEADER = struct.Struct(">7I4B32s")
OLD_ENV = (
    b"bootargs=console=ttyS1,115200n8 mem=127M rdinit=/init rootdelay=1\n"
    b"gkdboot=mmc read 80600000 4800 3000;bootm 80600000;"
    b"mmc read 80600000 1800 3000;bootm 80600000\n"
)


def sha(data: bytes | bytearray) -> str:
    return hashlib.sha256(data).hexdigest()


def environment(mode: str) -> bytes:
    test = "testboth" if mode == "both" else "testmenu"
    lines = (
        "bootargs=console=ttyS1,115200n8 mem=127M rdinit=/init rootdelay=1",
        "autostart=no",
        "loadsel=mmc read 80500000 1008 8",
        "verifysel=bootm 80500000",
        "testboth=go 80600000 both",
        "testmenu=go 80600000 menu",
        "bootr=mmc read 80600000 1800 3000;bootm 80600000",
        "boota=mmc read 80600000 4800 3000;bootm 80600000",
        f"gkdboot=run loadsel verifysel {test} bootr;run boota;run bootr",
    )
    raw = ("\n".join(lines) + "\n").encode("ascii")
    if len(raw) >= ENV.stop - ENV.start or b"\0" in raw:
        raise ValueError("environment overflow")
    return raw + bytes(ENV.stop - ENV.start - len(raw))


def validate_selector(block: bytes) -> dict[str, object]:
    if len(block) != SELECTOR.stop - SELECTOR.start:
        raise ValueError("selector block size")
    values = HEADER.unpack(block[: HEADER.size])
    magic, header_crc, timestamp, size, load, entry, data_crc = values[:7]
    os_id, arch, image_type, compression = values[7:11]
    header = bytearray(block[: HEADER.size])
    header[4:8] = bytes(4)
    payload = block[HEADER.size : HEADER.size + size]
    if (
        magic != 0x27051956
        or zlib.crc32(header) & 0xFFFFFFFF != header_crc
        or zlib.crc32(payload) & 0xFFFFFFFF != data_crc
        or load != 0x80600000
        or entry != 0x80600000
        or arch != 5
        or image_type != 1
        or compression != 0
        or any(block[HEADER.size + size :])
    ):
        raise ValueError("selector uImage contract")
    return {
        "timestamp": timestamp,
        "payload_bytes": size,
        "payload_sha256": sha(payload),
        "os_id": os_id,
        "arch": arch,
        "type": image_type,
        "compression": compression,
        "load": load,
        "entry": entry,
    }


def validate_mbr(prefix: bytes) -> list[dict[str, int]]:
    if prefix[510:512] != b"\x55\xaa":
        raise ValueError("MBR signature")
    result = []
    for index in range(4):
        offset = 446 + index * 16
        ptype = prefix[offset + 4]
        start, count = struct.unpack_from("<II", prefix, offset + 8)
        if ptype or start or count:
            result.append({"type": ptype, "start": start, "count": count})
    expected = [
        {"type": 0x83, "start": 40960, "count": 1572865},
        {"type": 0x83, "start": 1615872, "count": 57726976},
        {"type": 0x82, "start": 59342848, "count": 2097152},
    ]
    if result != expected:
        raise ValueError("partition geometry")
    return result


def build(args: argparse.Namespace) -> dict[str, object]:
    source = args.source_prefix.read_bytes()
    r_slot = args.recovery_slot.read_bytes()
    selector = args.selector_block.read_bytes()
    if len(source) != PREFIX_BYTES or sha(source) != args.expected_source_sha256:
        raise ValueError("source prefix")
    if len(r_slot) != R_SLOT.stop - R_SLOT.start:
        raise ValueError("R slot size")
    if not source[ENV].startswith(OLD_ENV) or any(source[ENV][len(OLD_ENV) :]):
        raise ValueError("source environment")
    if any(source[ZERO_GAP]):
        raise ValueError("selector gap is not zero")
    partitions = validate_mbr(source)
    selector_contract = validate_selector(selector)
    identities = {
        "source_prefix": sha(source),
        "source_environment": sha(source[ENV]),
        "source_r_slot": sha(source[R_SLOT]),
        "source_a_slot": sha(source[A_SLOT]),
        "source_capsule": sha(source[CAPSULE]),
        "source_guard": sha(source[GUARD]),
        "source_trace": sha(source[TRACE]),
        "source_request": sha(source[REQUEST]),
        "selector_block": sha(selector),
        "target_r_slot": sha(r_slot),
    }
    for name, expected in (
        ("source_a_slot", args.expected_a_sha256),
        ("source_r_slot", args.expected_old_r_sha256),
        ("source_capsule", args.expected_capsule_sha256),
    ):
        if identities[name] != expected:
            raise ValueError(name)

    args.output.mkdir(mode=0o700)
    outputs = {}
    for mode, filename in (("both", "ar-key-test-pre-p1.bin"),
                           ("menu", "ar-menu-final-pre-p1.bin")):
        target = bytearray(source)
        target[ENV] = environment(mode)
        target[SELECTOR] = selector
        target[R_SLOT] = r_slot
        projected = bytearray(source)
        projected[ENV] = target[ENV]
        projected[SELECTOR] = selector
        projected[R_SLOT] = r_slot
        if target != projected or target[A_SLOT] != source[A_SLOT]:
            raise ValueError("out-of-range change")
        path = args.output / filename
        path.write_bytes(target)
        outputs[mode] = {"path": str(path), "sha256": sha(target)}

    manifest = {
        "schema": "gkd-mini-ar-key-proof-v1",
        "partitions": partitions,
        "selector": selector_contract,
        "identities": identities,
        "outputs": outputs,
        "changed_byte_ranges": [
            [ENV.start, ENV.stop, "external-environment"],
            [SELECTOR.start, SELECTOR.stop, "crc-verified-selector"],
            [R_SLOT.start, R_SLOT.stop, "dedicated-recovery-r"],
        ],
        "unchanged_byte_ranges": [
            [A_SLOT.start, A_SLOT.stop, "primary-a"],
            [CAPSULE.start, REQUEST.stop, "capsule-guard-trace-request"],
        ],
        "finalization_delta": [[ENV.start, ENV.stop, "both-to-menu-only"]],
    }
    (args.output / "MANIFEST.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return manifest


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-prefix", type=Path, required=True)
    parser.add_argument("--expected-source-sha256", required=True)
    parser.add_argument("--expected-a-sha256", required=True)
    parser.add_argument("--expected-old-r-sha256", required=True)
    parser.add_argument("--expected-capsule-sha256", required=True)
    parser.add_argument("--recovery-slot", type=Path, required=True)
    parser.add_argument("--selector-block", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


if __name__ == "__main__":
    result = build(parse_args())
    print("GKD_AR_PREFIX=PASS")
    print("test_sha256=" + result["outputs"]["both"]["sha256"])
    print("final_sha256=" + result["outputs"]["menu"]["sha256"])
