#!/usr/bin/env python3
"""Scoped RC3.6 native regressions. Run only in a disposable root environment."""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "system/application-core"
UI = ROOT / "system/ui-core"
UPDATE = ROOT / "system/rc33-system-update/source"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--isolated-root", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not args.isolated_root or os.geteuid() != 0 or os.environ.get("GKD_AUDIT_ISOLATED") != "1":
        parser.error("use sh tools/gkd-test-audit-fixes; never run fixtures as root on the handheld/NUC")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    records = []
    completed = False

    def run(name, command, extra_env=None):
        environment = dict(os.environ, PYTHONDONTWRITEBYTECODE="1",
                           ASAN_OPTIONS="detect_leaks=1:abort_on_error=1",
                           UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        environment.update(extra_env or {})
        try:
            result = subprocess.run([str(arg) for arg in command], cwd=ROOT,
                env=environment, text=True, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, timeout=90)
            text, code = result.stdout, result.returncode
        except subprocess.TimeoutExpired as error:
            text = error.stdout or b""
            if isinstance(text, bytes): text = text.decode("utf-8", "replace")
            text += "\nTIMEOUT\n"
            code = 124
        logfile = output / (name + ".log")
        logfile.write_text(text)
        records.append({"name": name, "exit_code": code,
                        "log_sha256": hashlib.sha256(logfile.read_bytes()).hexdigest()})
        print(("PASS " if code == 0 else "FAIL ") + name, flush=True)
        if code:
            print(text[-14000:], flush=True)
            raise RuntimeError("regression failed: " + name)

    try:
        suites = {
            "config-core": "system/config-core/tests/test_config_core.py",
            "release-guards": "tools/tests/test_release_guards.py",
            "source-closure": "tools/tests/test_source_closure.py",
            "settings-contract": "system/application-core/tests/test_settings_contract.py",
            "cli-config": "system/application-core/tests/test_cli_config_transaction.py",
            "config-commit": "system/application-core/tests/test_config_commit.py",
            "slot-capsule": "system/ar-boot-selector/tests/test_r_slot_capsule.py",
        }
        for name, path in suites.items(): run(name, [sys.executable, ROOT / path, "-v"])
        run("config-commit-sanitized", [sys.executable, APP / "tests/test_config_commit.py", "-v"],
            {"GKD_TEST_SANITIZE": "1"})
        run("formal-source-closure", [sys.executable, ROOT / "tools/gkd-source-closure.py"])
        run("shell-core-syntax", ["sh", "-n", ROOT / "system/config-core/device/gkd-config"])
        run("shell-cli-syntax", ["sh", "-n", APP / "device/gkd-application-config"])

        # The existing service fixture expects this path. This generated pattern
        # exercises rendering control flow, not the accepted boot art or font.
        Path("/out").mkdir(exist_ok=True)
        font = struct.pack("<8I", 0x864AB572, 0, 32, 0, 256, 16, 16, 8)
        font += bytes((glyph * 13 + row * 17) & 255 for glyph in range(256) for row in range(16))
        Path("/out/fallback.psf").write_bytes(font)
        schema = output / "application.schema"
        run("derive-schema", [sys.executable, APP / "scripts/derive-schema.py",
            ROOT / "system/config-core/schema/gdkmini.schema", schema])
        defaults = []
        for line in schema.read_text().splitlines():
            parts = line.split("|")
            if not line.startswith("#") and len(parts) == 8: defaults.append(parts[0] + "=" + parts[2])

        # Reuse the authoritative service harness's source and wrapper inventory,
        # rather than maintaining another module list in this native runner.
        syntax = ast.parse((APP / "tests/run_service_tests.py").read_text())
        modules = []
        for node in syntax.body:
            if isinstance(node, ast.AugAssign) and isinstance(node.target, ast.Name) and node.target.id == "sources" and isinstance(node.value, ast.ListComp):
                modules.append(ast.literal_eval(node.value.generators[0].iter))
        if len(modules) != 2: raise ValueError("service harness source contract changed")
        wrappers = next(ast.literal_eval(node.value) for node in syntax.body if isinstance(node, ast.Assign)
            and any(isinstance(target, ast.Name) and target.id == "wrappers" for target in node.targets))
        service_sources = [APP / "tests/service_fixture.c"]
        service_sources += [APP / ("source/gkd-app-" + name + ".c") for name in modules[0]]
        service_sources += [UI / ("source/gkd-" + name + ".c") for name in modules[1]]
        service_sources += [UPDATE / "gkd-update-sha256.c"]
        flags = ["cc", "-std=gnu99", "-Os", "-Wall", "-Wextra", "-Werror",
                 "-I" + str(APP / "include"), "-I" + str(UI / "include"), "-I" + str(UPDATE)]
        for variant, extra in (("normal", []), ("sanitized", ["-O1", "-g", "-fsanitize=address,undefined",
                                                       "-fno-omit-frame-pointer", "-no-pie"])):
            for name, source, wrap in (("settings-save", "settings_save_fixture.c", "stat"),
                                       ("config-store", "config_store_fixture.c", "stat,--wrap=fsync")):
                binary = output / (name + "-" + variant)
                run("compile-" + binary.name, flags + extra + [APP / ("tests/" + source),
                    APP / "source/gkd-app-job.c", "-Wl,--wrap=" + wrap, "-o", binary])
                run(binary.name, [binary])
            binary = output / ("service-" + variant)
            run("compile-" + binary.name, flags + extra + ["-DGKD_APPLICATION_UI=1"] + service_sources +
                ["-Wl," + ",".join("--wrap=" + name for name in wrappers), "-o", binary])
            effective = Path("/tmp/gkd-service-effective.conf")
            effective.write_text("\n".join(defaults) + "\n")
            effective.chmod(0o600)
            run(binary.name, [binary])
        completed = True
    finally:
        receipt = {"status": "PASS" if completed else "FAIL", "scope": "native-isolated-regression",
                   "firmware_built": False, "deployed": False, "synthetic_font": True, "checks": records}
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print("GKD_AUDIT_FIX_TESTS=PASS native-only checks=" + str(len(records)))


if __name__ == "__main__": main()
