#!/usr/bin/env python3
"""Verify the exact public source snapshot, excluding Git metadata and this manifest."""
import hashlib
import json
from pathlib import Path
import sys

root = Path(__file__).resolve().parent
manifest = json.loads((root / "release-manifest.json").read_text())
expected = manifest["files"]
actual = {}
errors = []
if manifest.get("version") != (root / "VERSION").read_text().strip():
    errors.append("VERSION: release manifest mismatch")
for p in root.rglob("*"):
    relative = p.relative_to(root)
    if ".git" in relative.parts or "__pycache__" in relative.parts:
        continue
    if p.is_symlink():
        errors.append(str(relative) + ": symlink")
    elif p.is_file() and str(relative) != "release-manifest.json":
        if p.suffix == ".pyc":
            continue
        actual[relative.as_posix()] = p
for name in sorted(set(actual) | set(expected)):
    if name not in actual or name not in expected:
        errors.append(name + ": missing or extra file")
        continue
    p = actual[name]
    item = expected[name]
    if p.stat().st_size != item["bytes"] or hashlib.sha256(p.read_bytes()).hexdigest() != item["sha256"]:
        errors.append(name + ": content mismatch")
    if sys.platform != "win32" and bool(p.stat().st_mode & 0o111) != (item["mode"] == "100755"):
        errors.append(name + ": executable mode mismatch")
for error in errors:
    print(error)
print("PUBLIC_SOURCE_VERIFY=" + ("FAIL" if errors else "PASS") + " files=" + str(len(expected)))
sys.exit(bool(errors))
