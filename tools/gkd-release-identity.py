#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
BASE_COMMIT = "ea9681d8afb44a65dde4fb7a116999c350bd12c4"
BASE_TAG = "gkd-rc3.4-locked-20260902"


def sha(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(8 << 20), b""):
            value.update(block)
    return value.hexdigest()


def source_files() -> list[Path]:
    selected = []
    for top in ("build", "kernel", "system", "tools"):
        for path in (ROOT / top).rglob("*"):
            if (path.is_file() and ".git" not in path.parts and
                    "__pycache__" not in path.parts and path.suffix != ".pyc"):
                selected.append(path)
    return sorted(selected)


def source_digest() -> tuple[str, int]:
    value = hashlib.sha256()
    files = source_files()
    for path in files:
        relative = path.relative_to(ROOT).as_posix().encode("utf-8")
        value.update(len(relative).to_bytes(4, "big"))
        value.update(relative)
        value.update(bytes.fromhex(sha(path)))
    return value.hexdigest(), len(files)


def build_manifest(args: argparse.Namespace) -> dict:
    tree, count = source_digest()
    head = subprocess.check_output(
        ["git", "-C", str(ROOT), "log", "-1", "--format=%H", "--", "build", "kernel", "system", "tools"],
        text=True,
    ).strip()
    return {
        "format": "gkd-mini-release-identity-v1",
        "release": "GKD Mini RC3.7",
        "approved_base": {"commit": BASE_COMMIT, "tag": BASE_TAG},
        "functional_commit": head,
        "formal_source": {"sha256": tree, "files": count},
        "schema_sha256": sha(ROOT / "system/config-core/schema/gdkmini.schema"),
        "kernel": {
            name: sha(args.kernel / name)
            for name in ("vmlinux.bin", "vmlinux.bin.gz", "uinput.ko", "round27-init", "gkd350.config")
        },
        "source_p1_sha256": sha(args.source_p1),
        "stable_prefix_sha256": sha(args.stable_prefix),
        "host_test_receipt_sha256": sha(args.test_receipt),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    create = sub.add_parser("create")
    create.add_argument("--kernel", type=Path, required=True)
    create.add_argument("--source-p1", type=Path, required=True)
    create.add_argument("--stable-prefix", type=Path, required=True)
    create.add_argument("--test-receipt", type=Path, required=True)
    create.add_argument("--output", type=Path, required=True)
    verify = sub.add_parser("verify")
    verify.add_argument("--manifest", type=Path, required=True)
    verify.add_argument("--kernel", type=Path, required=True)
    verify.add_argument("--source-p1", type=Path, required=True)
    verify.add_argument("--stable-prefix", type=Path, required=True)
    verify.add_argument("--test-receipt", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "create":
        if args.output.exists():
            raise SystemExit("GKD_RELEASE_IDENTITY=BLOCKED output-exists")
        manifest = build_manifest(args)
        args.output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
                               encoding="utf-8")
    else:
        expected = json.loads(args.manifest.read_text(encoding="utf-8"))
        actual = build_manifest(args)
        if expected != actual:
            raise SystemExit("GKD_RELEASE_IDENTITY=BLOCKED mismatch")
    print("GKD_RELEASE_IDENTITY=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
