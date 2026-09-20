#!/usr/bin/env python3
"""Native regression for non-recursive profile directory binds."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

def namespace(binary, *args):
    return subprocess.run(["unshare", "--user", "--map-root-user", "--mount", str(binary),
                           *map(str, args)], text=True, capture_output=True, timeout=15)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--refuse-fixture", type=Path, required=True)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--target", type=Path, required=True)
    parser.add_argument("--evidence", type=Path)
    options = parser.parse_args()
    for path in (options.fixture, options.refuse_fixture, options.library, options.target):
        assert path.is_file(), path
    with tempfile.TemporaryDirectory(prefix="gkd-profile-mount.", dir="/tmp/gkd-mini-public") as temp:
        work = Path(temp)
        normal = namespace(options.fixture, "--run", work, options.library, options.target)
        assert normal.returncode == 0 and "GKD_PROFILE_MOUNT=PASS" in normal.stdout, (normal.returncode, normal.stdout, normal.stderr)
        legacy_work = work / "legacy"
        legacy_work.mkdir()
        legacy = namespace(options.fixture, "--legacy", legacy_work, options.library, options.target)
        assert (legacy.returncode == 0 and "GKD_PROFILE_LEGACY_DEFECT=REPRODUCED" in legacy.stdout and
                "file too short" in legacy.stderr), legacy.stderr
        negative_work = work / "negative"
        negative_work.mkdir()
        negative = namespace(options.fixture, "--negative", negative_work)
        assert negative.returncode == 0 and "GKD_PROFILE_NEGATIVE=PASS" in negative.stdout, negative.stderr
        refused = []
        for position in range(1, 13):
            refused_work = work / ("refused-" + str(position))
            refused_work.mkdir()
            result = namespace(options.refuse_fixture, "--refuse-" + str(position), refused_work,
                               options.library, options.target)
            assert (result.returncode == 0 and
                    "GKD_PROFILE_REFUSAL=PASS position=" + str(position) in result.stdout), result.stderr
            refused.append(result)
        digest = hashlib.sha256(options.library.read_bytes()).hexdigest()
        assert options.library.read_bytes()[:4] == b"\x7fELF"
        report = {"result": "PASS", "library_sha256": digest,
            "cases": ["explicit-child-bind-and-preload", "legacy-zero-byte-loader-defect",
                      "missing-source-target-fail-closed", "all-twelve-bind-remount-refusals",
                      "unrelated-nested-mount-not-propagated"],
            "raw": {"normal": {"stdout": normal.stdout, "stderr": normal.stderr},
                    "legacy": {"stdout": legacy.stdout, "stderr": legacy.stderr},
                    "negative": {"stdout": negative.stdout, "stderr": negative.stderr},
                    "refusal": [{"stdout": item.stdout, "stderr": item.stderr} for item in refused]}}
        if options.evidence:
            options.evidence.parent.mkdir(parents=True, exist_ok=True)
            options.evidence.write_text(json.dumps(report, indent=2) + "\n")
        print("GKD_PROFILE_TEST=PASS library_sha256=" + digest)

if __name__ == "__main__":
    main()
