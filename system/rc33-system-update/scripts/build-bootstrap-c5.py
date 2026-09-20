#!/usr/bin/env python3
"""Derive an immutable kernel-only C5 bundle from accepted RC3.2.1 state."""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
from pathlib import Path

SECTOR = 512
SLOT_START = 18432
SLOT_SECTORS = 12288
STABLE_SECTORS = 40832
HEX40 = re.compile(r"[0-9a-f]{40}")


def blocked(reason: str) -> None:
    raise SystemExit("GKDSU_BOOTSTRAP_C5=BLOCKED reason=" + reason)


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(8 << 20), b""):
            value.update(chunk)
    return value.hexdigest()


def read_table(path: Path, fields: int) -> list[list[str]]:
    rows = [line.split("\t") for line in path.read_text(encoding="ascii").splitlines()]
    if not rows or any(len(row) != fields or any(not value for value in row) for row in rows):
        blocked("table:" + path.name)
    return rows


def write_table(path: Path, rows: list[list[str]]) -> None:
    path.write_text("".join("\t".join(row) + "\n" for row in rows), encoding="ascii")


def verify_accepted(root: Path) -> None:
    sums = [line.split(None, 1) for line in
            (root / "SHA256SUMS").read_text(encoding="ascii").splitlines()]
    if not sums or any(len(row) != 2 for row in sums):
        blocked("table:SHA256SUMS")
    actual = {str(path.relative_to(root)) for path in root.rglob("*") if path.is_file()}
    expected = {row[1] for row in sums} | {"SHA256SUMS"}
    if actual != expected:
        blocked("accepted-pathset")
    for expected_hash, relative in sums:
        if digest(root / relative) != expected_hash:
            blocked("accepted-hash:" + relative)


def keyed(rows: list[list[str]]) -> tuple[list[str], dict[str, str]]:
    order: list[str] = []
    values: dict[str, str] = {}
    for key, value in rows:
        if key in values:
            blocked("duplicate-lock-key:" + key)
        order.append(key)
        values[key] = value
    return order, values


def slot_hashes(path: Path) -> tuple[str, str, str]:
    data = path.read_bytes()
    if len(data) != SLOT_SECTORS * SECTOR:
        blocked("slot-size:" + path.name)
    return hashlib.sha256(data).hexdigest(), hashlib.sha256(data[:SECTOR]).hexdigest(), hashlib.sha256(data[SECTOR:]).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--accepted-bundle", type=Path, required=True)
    parser.add_argument("--stable-prefix", type=Path, required=True)
    parser.add_argument("--target-slot", type=Path, required=True)
    parser.add_argument("--live-chooser", type=Path, required=True)
    parser.add_argument("--live-config", type=Path, required=True)
    parser.add_argument("--candidate-commit", required=True)
    parser.add_argument("--candidate-tree", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    if (args.output.exists() or not args.accepted_bundle.is_dir() or
            not HEX40.fullmatch(args.candidate_commit) or
            not HEX40.fullmatch(args.candidate_tree)):
        blocked("arguments")
    for path in (args.stable_prefix, args.target_slot, args.live_chooser, args.live_config):
        if not path.is_file() or path.is_symlink():
            blocked("input:" + path.name)
    verify_accepted(args.accepted_bundle)

    lock_order, old = keyed(read_table(args.accepted_bundle / "LOCK.tsv", 2))
    if old.get("schema") != "gkd-mini-rc30-c5-v3":
        blocked("schema")
    stable = args.stable_prefix.read_bytes()
    if (len(stable) != STABLE_SECTORS * SECTOR or
            hashlib.sha256(stable).hexdigest() != old.get("target_stable_prefix_sha256")):
        blocked("source-stable-prefix")
    outside = stable[:SLOT_START * SECTOR] + stable[(SLOT_START + SLOT_SECTORS) * SECTOR:]
    outside_hash = hashlib.sha256(outside).hexdigest()
    if outside_hash != old.get("target_outside_slot_sha256"):
        blocked("source-outside-slot")
    source_slot = args.accepted_bundle / "target-slot.bin"
    source_slot_hash, source_header_hash, source_body_hash = slot_hashes(source_slot)
    if source_slot_hash != old.get("target_slot_sha256"):
        blocked("source-slot")
    target_slot_hash, target_header_hash, target_body_hash = slot_hashes(args.target_slot)
    target_stable = bytearray(stable)
    start = SLOT_START * SECTOR
    target_stable[start:start + SLOT_SECTORS * SECTOR] = args.target_slot.read_bytes()
    target_stable_hash = hashlib.sha256(target_stable).hexdigest()

    stage = args.output
    complete = False
    try:
        shutil.copytree(args.accepted_bundle, stage)
        for path in [stage, *stage.rglob("*")]:
            os.chmod(path, 0o755 if path.is_dir() else 0o644)
        providers = read_table(stage / "PROVIDERS.tsv", 5)
        new_providers: list[list[str]] = []
        chooser_hash = digest(args.live_chooser)
        for name, _source_hash, accepted_target_hash, r13_hash, destination in providers:
            source_payload = stage / "source" / name
            target_payload = stage / "target" / name
            if name == "gkd-usb-chooser":
                shutil.copyfile(args.live_chooser, source_payload)
                shutil.copyfile(args.live_chooser, target_payload)
                current_hash = chooser_hash
            else:
                accepted_payload = args.accepted_bundle / "target" / name
                if accepted_target_hash == "absent":
                    source_payload.unlink(missing_ok=True)
                    target_payload.unlink(missing_ok=True)
                    current_hash = "absent"
                else:
                    if digest(accepted_payload) != accepted_target_hash:
                        blocked("accepted-provider:" + name)
                    shutil.copyfile(accepted_payload, source_payload)
                    shutil.copyfile(accepted_payload, target_payload)
                    current_hash = accepted_target_hash
            new_providers.append([name, current_hash, current_hash, r13_hash, destination])
        write_table(stage / "PROVIDERS.tsv", new_providers)

        static_rows = read_table(stage / "STATIC.tsv", 5)
        changed = 0
        for row in static_rows:
            if row[0] == "/etc/gkd-mini/gdkmini.conf":
                row[3] = str(args.live_config.stat().st_size)
                row[4] = digest(args.live_config)
                changed += 1
        if changed != 1:
            blocked("static-config")
        write_table(stage / "STATIC.tsv", static_rows)

        shutil.copyfile(source_slot, stage / "source-slot.bin")
        shutil.copyfile(args.target_slot, stage / "target-slot.bin")
        new = dict(old)
        new.update({
            "candidate_source_commit": args.candidate_commit,
            "candidate_git_tree": args.candidate_tree,
            "source_kernel_release": old["target_kernel_release"],
            "target_kernel_release": old["target_kernel_release"],
            "source_runtime_id": old["target_runtime_id"],
            "source_slot_sha256": source_slot_hash,
            "source_header_sha256": source_header_hash,
            "source_body_sha256": source_body_hash,
            "source_stable_prefix_sha256": hashlib.sha256(stable).hexdigest(),
            "source_outside_slot_sha256": outside_hash,
            "target_slot_sha256": target_slot_hash,
            "target_header_sha256": target_header_hash,
            "target_body_sha256": target_body_hash,
            "target_stable_prefix_sha256": target_stable_hash,
            "target_outside_slot_sha256": outside_hash,
        })
        write_table(stage / "LOCK.tsv", [[key, new[key]] for key in lock_order])

        files = (stage / "BUNDLE-FILES").read_text(encoding="ascii").splitlines()
        if "SHA256SUMS" not in files or len(files) != len(set(files)):
            blocked("bundle-files")
        sum_lines = []
        for relative in files:
            path = stage / relative
            if relative == "SHA256SUMS":
                continue
            if not path.is_file() or path.is_symlink():
                blocked("output-path:" + relative)
            sum_lines.append(f"{digest(path)}  {relative}\n")
        (stage / "SHA256SUMS").write_text("".join(sum_lines), encoding="ascii")

        for path in stage.rglob("*"):
            if path.is_dir():
                os.chmod(path, 0o555)
            elif path.name == "deploy-transaction.sh":
                os.chmod(path, 0o555)
            else:
                os.chmod(path, 0o444)
        os.chmod(stage, 0o555)
        verify_accepted(stage)
        complete = True
    finally:
        if not complete and stage.exists():
            for path in [stage, *stage.rglob("*")]:
                try:
                    os.chmod(path, 0o755 if path.is_dir() else 0o644)
                except OSError:
                    pass
            shutil.rmtree(stage, ignore_errors=True)
    print("GKDSU_BOOTSTRAP_C5=PASS output=" + str(args.output) +
          " source_slot=" + source_slot_hash + " target_slot=" + target_slot_hash +
          " target_stable=" + target_stable_hash)


if __name__ == "__main__":
    main()
