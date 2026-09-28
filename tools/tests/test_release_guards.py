#!/usr/bin/env python3
"""Release entry-point guards with mock commands; never build or install firmware."""
import ast
import contextlib
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / "tools/gkd-build-release.py"

class ReleaseGuards(unittest.TestCase):
    def exercise(self, optimize, scenario):
        with tempfile.TemporaryDirectory(prefix="gkd-release-guard-") as directory:
            base = Path(directory)
            repo, inputs, scratch, evidence = (base / n for n in ("repo", "inputs", "scratch", "evidence"))
            (repo / "build").mkdir(parents=True)
            (repo / "system").mkdir()
            inputs.mkdir(); scratch.mkdir()
            (repo / "VERSION").write_text("BAD\n" if scenario == "version" else "RC3.7\n")
            item = repo / "system/fixture.c"
            item.write_text("unchanged\n")
            manifest = {"release": "RC3.7", "builder_image": "fixture-builder", "files": {}}
            for name in ("p1.img", "slot-header-template.bin", "busybox-1.22.1.tar.bz2"):
                data = b"fixture-not-firmware"
                (inputs / name).write_bytes(data)
                manifest["files"][name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            if scenario == "hash": manifest["files"]["p1.img"]["sha256"] = "0" * 64
            if scenario == "size": manifest["files"]["p1.img"]["bytes"] += 1
            if scenario == "symlink":
                (inputs / "p1.img").rename(inputs / "real.img")
                (inputs / "p1.img").symlink_to("real.img")
            (repo / "build/rc3.7-inputs.json").write_text(json.dumps(manifest))
            module = {"__file__": str(DRIVER), "__name__": "guard_fixture"}
            exec(compile(DRIVER.read_text(), str(DRIVER), "exec", optimize=optimize), module)
            module["ROOT"] = repo
            module["Path"] = lambda *a: scratch if a == ("/tmp/gkd-mini-public",) else Path(*a)
            calls = []
            def command(argv, **kwargs):
                calls.append(argv)
                if scenario == "drift": item.write_text("changed-during-build\n")
                for path in (scratch / "gkd-app-minimal-test/application-a-slot.bin",
                             scratch / "gkd-kernel-current-r-test/recovery-r-slot.bin"):
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(b"MOCK: NOT FIRMWARE")
                return subprocess.CompletedProcess(argv, 0)
            args = ["driver", "--inputs", str(inputs), "--name", "test", "--evidence", str(evidence)]
            with patch.object(sys, "argv", args), patch.object(subprocess, "run", side_effect=command), \
                 patch.object(subprocess, "check_output", return_value="wrong" if scenario == "builder" else "fixture-builder") as inspect, \
                 contextlib.redirect_stdout(io.StringIO()):
                if scenario == "valid": module["main"]()
                else:
                    with self.assertRaises(ValueError): module["main"]()
            if scenario in ("valid", "drift"):
                plan = json.loads((evidence / "build-plan.json").read_text())
                self.assertEqual(plan["status"], "BUILD_PASS" if scenario == "valid" else "BUILD_FAILED")
                self.assertFalse(plan["deployed"])
                self.assertEqual(len(calls), 6)
            else:
                self.assertEqual(calls, [])
            self.assertEqual(inspect.call_count, 1 if scenario in ("builder", "valid", "drift") else 0)

    def test_guards_survive_all_optimization_modes(self):
        for mode in (0, 1, 2):
            for scenario in ("valid", "version", "hash", "size", "symlink", "builder", "drift"):
                with self.subTest(optimize=mode, scenario=scenario): self.exercise(mode, scenario)

    def test_no_assertion_is_a_production_guard(self):
        self.assertFalse(any(isinstance(n, ast.Assert) for n in ast.walk(ast.parse(DRIVER.read_text()))))

    def test_slot_name_is_explicit(self):
        result = subprocess.run([sys.executable, str(ROOT / "system/rc33-system-update/scripts/build-kernel-slot.py"),
            "--source-slot", "not-read", "--kernel-build", "not-read", "--output", "not-created"],
            capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--name", result.stderr)

if __name__ == "__main__": unittest.main()
