#!/usr/bin/env python3
"""Reverse a sealed kernel-only C5 bundle after rejected-kernel readback."""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
from pathlib import Path

HEX40 = re.compile(r"[0-9a-f]{40}")


def stop(reason: str) -> None:
    raise SystemExit("GKDSU_BOOTSTRAP_ROLLBACK_C5=BLOCKED reason=" + reason)


def sha(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(8 << 20), b""):
            value.update(chunk)
    return value.hexdigest()


def verify(root: Path) -> None:
    rows = [line.split(None, 1) for line in
            (root / "SHA256SUMS").read_text(encoding="ascii").splitlines()]
    if not rows or any(len(row) != 2 for row in rows):
        stop("sums")
    actual = {str(path.relative_to(root)) for path in root.rglob("*") if path.is_file()}
    if actual != {row[1] for row in rows} | {"SHA256SUMS"}:
        stop("pathset")
    for expected, relative in rows:
        if sha(root / relative) != expected:
            stop("hash:" + relative)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--forward-bundle", type=Path, required=True)
    parser.add_argument("--source-release", required=True)
    parser.add_argument("--candidate-commit", required=True)
    parser.add_argument("--candidate-tree", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if (not args.forward_bundle.is_dir() or args.output.exists() or
            not HEX40.fullmatch(args.candidate_commit) or
            not HEX40.fullmatch(args.candidate_tree) or
            not re.fullmatch(r"[A-Za-z0-9._+-]+", args.source_release)):
        stop("arguments")
    verify(args.forward_bundle)
    stage = args.output
    complete = False
    try:
        shutil.copytree(args.forward_bundle, stage)
        for path in [stage, *stage.rglob("*")]:
            os.chmod(path, 0o755 if path.is_dir() else 0o644)
        source_slot = (stage / "source-slot.bin").read_bytes()
        target_slot = (stage / "target-slot.bin").read_bytes()
        (stage / "source-slot.bin").write_bytes(target_slot)
        (stage / "target-slot.bin").write_bytes(source_slot)

        rows = [line.split("\t") for line in
                (stage / "LOCK.tsv").read_text(encoding="ascii").splitlines()]
        if any(len(row) != 2 for row in rows):
            stop("lock")
        order = [row[0] for row in rows]
        values = dict(rows)
        if len(values) != len(rows) or values.get("schema") != "gkd-mini-rc30-c5-v3":
            stop("lock-schema")
        old = dict(values)
        values["candidate_source_commit"] = args.candidate_commit
        values["candidate_git_tree"] = args.candidate_tree
        values["source_kernel_release"] = args.source_release
        values["target_kernel_release"] = old["source_kernel_release"]
        values["source_runtime_id"] = "absent"
        values["target_runtime_id"] = old["source_runtime_id"]
        for name in ("slot_sha256", "header_sha256", "body_sha256",
                     "stable_prefix_sha256", "outside_slot_sha256"):
            values["source_" + name] = old["target_" + name]
            values["target_" + name] = old["source_" + name]
        (stage / "LOCK.tsv").write_text(
            "".join(key + "\t" + values[key] + "\n" for key in order), encoding="ascii")

        files = (stage / "BUNDLE-FILES").read_text(encoding="ascii").splitlines()
        sums = []
        for relative in files:
            if relative == "SHA256SUMS":
                continue
            path = stage / relative
            if not path.is_file() or path.is_symlink():
                stop("output:" + relative)
            sums.append(f"{sha(path)}  {relative}\n")
        (stage / "SHA256SUMS").write_text("".join(sums), encoding="ascii")
        for path in stage.rglob("*"):
            os.chmod(path, 0o555 if path.is_dir() or path.name == "deploy-transaction.sh" else 0o444)
        os.chmod(stage, 0o555)
        verify(stage)
        complete = True
    finally:
        if not complete and stage.exists():
            for path in [stage, *stage.rglob("*")]:
                try:
                    os.chmod(path, 0o755 if path.is_dir() else 0o644)
                except OSError:
                    pass
            shutil.rmtree(stage, ignore_errors=True)
    print("GKDSU_BOOTSTRAP_ROLLBACK_C5=PASS output=" + str(stage))


if __name__ == "__main__":
    main()
