#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


PATTERNS = (
    re.compile(r"(^|[^A-Za-z0-9_.-])archive/", re.IGNORECASE),
    re.compile(r"/opt/gkd-build/artifacts/gkd-mini-system-rebuild/sealed/round", re.IGNORECASE),
)


def selected(path: Path, root: Path) -> bool:
    relative = path.relative_to(root)
    if any(part in {".git", "__pycache__", "docs", "tests"} for part in relative.parts):
        return False
    if path.suffix.lower() in {".md", ".txt"}:
        return False
    if relative.as_posix() == "tools/gkd-source-closure.py":
        return False  # This file defines the forbidden-pattern vocabulary.
    if relative.parts[0] in {"build", "kernel", "tools"}:
        return True
    if relative.parts[0] != "system":
        return False
    return (
        any(part in {"device", "scripts", "source", "tools"} for part in relative.parts)
        or relative.name.startswith("gate-")
    )


def audit(root: Path) -> list[tuple[Path, int, str]]:
    findings: list[tuple[Path, int, str]] = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or not selected(path, root):
            continue
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except (UnicodeDecodeError, OSError):
            continue
        for number, line in enumerate(lines, 1):
            if line.lstrip().startswith("#") and not re.match(r"\s*#\s*include\b", line):
                continue
            build_line = line.replace("docs/archive/", "docs/history/")
            if any(pattern.search(build_line) for pattern in PATTERNS):
                findings.append((path.relative_to(root), number, line.strip()))
    return findings


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    arguments = parser.parse_args()
    root = arguments.root.resolve()
    findings = audit(root)
    if findings:
        print(f"GKD_SOURCE_CLOSURE=BLOCKED findings={len(findings)}")
        for path, number, line in findings:
            print(f"{path}:{number}: {line}")
        return 1
    print("GKD_SOURCE_CLOSURE=PASS historical_inputs=0")
    return 0


if __name__ == "__main__":
    sys.exit(main())
