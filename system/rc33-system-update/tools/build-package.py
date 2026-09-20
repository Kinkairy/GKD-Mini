#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

from package import (GEOMETRY, MAGIC, SIGNATURE_BYTES, canonical,
                     sha256_file, verify)


def exact_size(path: Path, expected: int, label: str) -> None:
    if not path.is_file() or path.is_symlink() or path.stat().st_size != expected:
        raise SystemExit("GKDSU_BUILD_BLOCKED=" + label)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-p1", type=Path, required=True)
    parser.add_argument("--target-p1", type=Path, required=True)
    parser.add_argument("--source-kernel", type=Path, required=True)
    parser.add_argument("--target-kernel", type=Path, required=True)
    parser.add_argument("--mbr", type=Path, required=True)
    parser.add_argument("--from-version", required=True)
    parser.add_argument("--to-version", required=True)
    parser.add_argument("--source-runtime-id", required=True)
    parser.add_argument("--target-runtime-id", required=True)
    parser.add_argument("--private-key", type=Path, required=True)
    parser.add_argument("--public-key", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit("GKDSU_BUILD_BLOCKED=output-exists")
    p1_size = int(GEOMETRY["p1"]["bytes"])
    kernel_size = int(GEOMETRY["kernel"]["bytes"])
    for path, size, label in ((args.source_p1, p1_size, "source-p1"),
                              (args.target_p1, p1_size, "target-p1"),
                              (args.source_kernel, kernel_size, "source-kernel"),
                              (args.target_kernel, kernel_size, "target-kernel"),
                              (args.mbr, 512, "mbr")):
        exact_size(path, size, label)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="gkd-system-update-build-",
                                     dir=str(args.output.parent)) as directory:
        source_p1_hash = sha256_file(args.source_p1)
        target_p1_hash = sha256_file(args.target_p1)
        source_kernel_hash = sha256_file(args.source_kernel)
        target_kernel_hash = sha256_file(args.target_kernel)
        manifest = {
            "schema": "gkd-mini-system-update-v1", "version": 1,
            "compatible": {"device": "gkd-mini", "board": "x1830-gkd-mini"},
            "release": {"from_version": args.from_version,
                        "to_version": args.to_version,
                        "source_runtime_id": args.source_runtime_id,
                        "target_runtime_id": args.target_runtime_id},
            "disk": {"disk_bytes": GEOMETRY["disk_bytes"],
                     "logical_block_bytes": "512",
                     "mbr_sha256": sha256_file(args.mbr),
                     "p1": {**GEOMETRY["p1"], "source_sha256": source_p1_hash,
                            "target_sha256": target_p1_hash},
                     "kernel": {**GEOMETRY["kernel"],
                                "source_sha256": source_kernel_hash,
                                "target_sha256": target_kernel_hash},
                     "recovery": GEOMETRY["recovery"]},
            "payloads": {
                "p1": {"encoding": "raw", "bytes": str(p1_size),
                            "sha256": target_p1_hash, "raw_bytes": str(p1_size),
                            "raw_sha256": target_p1_hash},
                "kernel": {"encoding": "raw", "bytes": str(kernel_size),
                           "sha256": target_kernel_hash, "raw_bytes": str(kernel_size),
                           "raw_sha256": target_kernel_hash}},
            "protocol": {"apply_order": ["p1", "kernel"], "commit_last": "kernel",
                         "rollback_order": ["p1", "kernel"],
                         "min_battery_percent": 30,
                         "request_slot_offset": "20967424",
                         "request_slot_bytes": "4096"}}
        manifest_bytes = canonical(manifest)
        message = Path(directory) / "manifest"
        signature = Path(directory) / "signature"
        message.write_bytes(manifest_bytes)
        subprocess.run(["openssl", "dgst", "-sha256", "-sign", str(args.private_key),
                        "-out", str(signature), str(message)], check=True)
        if signature.stat().st_size != SIGNATURE_BYTES:
            raise SystemExit("GKDSU_BUILD_BLOCKED=signature-size")
        with args.output.open("xb") as output:
            output.write(MAGIC)
            output.write(struct.pack("<I", len(manifest_bytes)))
            output.write(manifest_bytes)
            output.write(signature.read_bytes())
            with args.target_p1.open("rb") as stream:
                shutil.copyfileobj(stream, output, length=8 << 20)
            with args.target_kernel.open("rb") as stream:
                shutil.copyfileobj(stream, output, length=8 << 20)
        verify(args.output, args.public_key)
    print("GKDSU_BUILD=PASS sha256=" + sha256_file(args.output))


if __name__ == "__main__":
    main()
