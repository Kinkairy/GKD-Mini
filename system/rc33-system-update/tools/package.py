#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import re
import struct
import subprocess
import tempfile
from pathlib import Path

MAGIC = b"GKDSU1\0\0"
HEADER_BYTES = 12
SIGNATURE_BYTES = 256
SCHEMA = "gkd-mini-system-update-v1"
HEX64 = re.compile(r"[0-9a-f]{64}")
VERSION = re.compile(r"[0-9]+(?:\.[0-9]+){1,3}(?:-[a-z0-9.-]+)?")
GEOMETRY = {
    "disk_bytes": "31457280000",
    "logical_block_bytes": "512",
    "p1": {"offset": "20971520", "bytes": "805306880"},
    "kernel": {"offset": "9437184", "bytes": "6291456"},
    "recovery": {"offset": "30383538176", "bytes": "1073741824", "type": "swap-temporary"},
}


class PackageError(RuntimeError):
    pass


def canonical(value: object) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True).encode("ascii")


def sha256_file(path: Path, offset: int = 0, length: int | None = None) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        stream.seek(offset)
        remaining = length
        while remaining is None or remaining:
            wanted = 8 << 20 if remaining is None else min(8 << 20, remaining)
            block = stream.read(wanted)
            if not block:
                break
            digest.update(block)
            if remaining is not None:
                remaining -= len(block)
        if remaining:
            raise PackageError("short payload")
    return digest.hexdigest()


def exact(value: object, keys: tuple[str, ...], label: str) -> dict:
    if not isinstance(value, dict) or set(value) != set(keys):
        raise PackageError(label + " keys")
    return value


def decimal(value: object, label: str) -> int:
    if not isinstance(value, str) or not re.fullmatch(r"0|[1-9][0-9]*", value):
        raise PackageError(label + " decimal")
    return int(value)


def digest(value: object, label: str) -> str:
    if not isinstance(value, str) or not HEX64.fullmatch(value):
        raise PackageError(label + " hash")
    return value


def version_key(value: str) -> tuple[int, int, int, int]:
    core = value.split("-", 1)[0]
    parts = tuple(int(part) for part in core.split("."))
    return parts + (0,) * (4 - len(parts))


def validate_manifest(manifest: object) -> dict:
    m = exact(manifest, ("schema", "version", "compatible", "release", "disk",
                         "payloads", "protocol"), "manifest")
    if m["schema"] != SCHEMA or m["version"] != 1:
        raise PackageError("schema")
    compatible = exact(m["compatible"], ("device", "board"), "compatible")
    if compatible != {"device": "gkd-mini", "board": "x1830-gkd-mini"}:
        raise PackageError("compatible")
    release = exact(m["release"], ("from_version", "to_version",
                                    "source_runtime_id", "target_runtime_id"),
                    "release")
    if (not isinstance(release["from_version"], str) or
        not VERSION.fullmatch(release["from_version"]) or
        not isinstance(release["to_version"], str) or
        not VERSION.fullmatch(release["to_version"]) or
        version_key(release["to_version"]) <= version_key(release["from_version"])):
        raise PackageError("release version")
    digest(release["source_runtime_id"], "source runtime")
    digest(release["target_runtime_id"], "target runtime")
    disk = exact(m["disk"], ("disk_bytes", "logical_block_bytes", "mbr_sha256",
                              "p1", "kernel", "recovery"), "disk")
    if disk["disk_bytes"] != GEOMETRY["disk_bytes"] or disk["logical_block_bytes"] != "512":
        raise PackageError("disk geometry")
    digest(disk["mbr_sha256"], "mbr")
    for name in ("p1", "kernel"):
        region = exact(disk[name], ("offset", "bytes", "source_sha256",
                                    "target_sha256"), name)
        if {key: region[key] for key in ("offset", "bytes")} != GEOMETRY[name]:
            raise PackageError(name + " geometry")
        digest(region["source_sha256"], name + " source")
        digest(region["target_sha256"], name + " target")
    recovery = exact(disk["recovery"], ("offset", "bytes", "type"), "recovery")
    if recovery != GEOMETRY["recovery"]:
        raise PackageError("recovery geometry")
    payloads = exact(m["payloads"], ("p1", "kernel"), "payloads")
    p1 = exact(payloads["p1"], ("encoding", "bytes", "sha256", "raw_bytes",
                                                "raw_sha256"), "p1 payload")
    kernel = exact(payloads["kernel"], ("encoding", "bytes", "sha256", "raw_bytes",
                                              "raw_sha256"), "kernel payload")
    if p1["encoding"] != "raw" or kernel["encoding"] != "raw":
        raise PackageError("payload encoding")
    for label, item in (("p1", p1), ("kernel", kernel)):
        if decimal(item["bytes"], label + " bytes") <= 0:
            raise PackageError(label + " empty")
        digest(item["sha256"], label + " payload")
        decimal(item["raw_bytes"], label + " raw bytes")
        digest(item["raw_sha256"], label + " raw")
    if p1["raw_bytes"] != GEOMETRY["p1"]["bytes"] or kernel["raw_bytes"] != GEOMETRY["kernel"]["bytes"]:
        raise PackageError("payload raw size")
    if (p1["raw_sha256"] != disk["p1"]["target_sha256"] or
        kernel["raw_sha256"] != disk["kernel"]["target_sha256"] or
        kernel["sha256"] != kernel["raw_sha256"]):
        raise PackageError("target binding")
    protocol = exact(m["protocol"], ("apply_order", "commit_last", "rollback_order",
                                      "min_battery_percent", "request_slot_offset",
                                      "request_slot_bytes"), "protocol")
    if (protocol["apply_order"] != ["p1", "kernel"] or
        protocol["commit_last"] != "kernel" or
        protocol["rollback_order"] != ["p1", "kernel"] or
        protocol["min_battery_percent"] != 30 or
        protocol["request_slot_offset"] != "20967424" or
        protocol["request_slot_bytes"] != "4096"):
        raise PackageError("protocol")
    return m


def load(path: Path) -> tuple[dict, bytes, bytes, int, int]:
    size = path.stat().st_size
    with path.open("rb") as stream:
        head = stream.read(HEADER_BYTES)
        if len(head) != HEADER_BYTES or head[:8] != MAGIC:
            raise PackageError("magic")
        manifest_size = struct.unpack("<I", head[8:])[0]
        if manifest_size < 2 or manifest_size > 1 << 20:
            raise PackageError("manifest bound")
        manifest_bytes = stream.read(manifest_size)
        signature = stream.read(SIGNATURE_BYTES)
        if len(manifest_bytes) != manifest_size or len(signature) != SIGNATURE_BYTES:
            raise PackageError("container short")
    try:
        manifest = validate_manifest(json.loads(manifest_bytes.decode("ascii")))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise PackageError("manifest json") from error
    if canonical(manifest) != manifest_bytes:
        raise PackageError("manifest canonical")
    p1_bytes = decimal(manifest["payloads"]["p1"]["bytes"], "p1 bytes")
    kernel_bytes = decimal(manifest["payloads"]["kernel"]["bytes"], "kernel bytes")
    payload_offset = HEADER_BYTES + manifest_size + SIGNATURE_BYTES
    if size != payload_offset + p1_bytes + kernel_bytes:
        raise PackageError("container length")
    if sha256_file(path, payload_offset, p1_bytes) != manifest["payloads"]["p1"]["sha256"]:
        raise PackageError("p1 payload hash")
    if sha256_file(path, payload_offset + p1_bytes, kernel_bytes) != manifest["payloads"]["kernel"]["sha256"]:
        raise PackageError("kernel payload hash")
    return manifest, manifest_bytes, signature, payload_offset, p1_bytes


def verify(path: Path, public_key: Path) -> dict:
    manifest, manifest_bytes, signature, _, _ = load(path)
    with tempfile.TemporaryDirectory(prefix="gkd-system-update-verify-") as directory:
        message = Path(directory) / "manifest"
        signed = Path(directory) / "signature"
        message.write_bytes(manifest_bytes)
        signed.write_bytes(signature)
        result = subprocess.run(
            ["openssl", "dgst", "-sha256", "-verify", str(public_key),
             "-signature", str(signed), str(message)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        if result.returncode:
            raise PackageError("signature")
    return manifest
