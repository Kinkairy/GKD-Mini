#!/usr/bin/env python3
"""Run the production media namespace path in an isolated Linux container."""
from pathlib import Path
import subprocess
import sys

project = Path(__file__).resolve().parents[3]
out = Path(sys.argv[1])
if out.exists() or not str(out).startswith("/tmp/gkd-mini-public/gkd-media-namespace-test-"):
    raise SystemExit("new scoped NUC test output required")
out.mkdir(mode=0o700)
image = "local/c-builder:2026.08.02-kernel"
assert subprocess.check_output(
    ["docker", "image", "inspect", image, "--format", "{{.Id}}"], text=True
).strip() == "sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1"
base = [
    "docker", "run", "--rm", "--network", "none", "--cap-add", "SYS_ADMIN",
    "--security-opt", "seccomp=unconfined", "-v", str(project) + ":/src:ro",
    "-v", str(out) + ":/out:rw", image,
]
app = "/src/system/application-core"
defines = [
    '-DGKD_APP_MEDIA_SOURCE="gkd-proof"',
    '-DGKD_APP_MEDIA_TARGET="/out/card"',
    '-DGKD_APP_MEDIA_TYPE="tmpfs"',
    '-DGKD_APP_MEDIA_DATA="size=1m"',
]
for variant, sanitizer in (
    ("normal", []),
    ("ubsan", ["-fsanitize=undefined", "-fno-sanitize-recover=all"]),
):
    binary = "/out/media-namespace-" + variant
    subprocess.run(base + [
        "cc", "-std=gnu99", "-O2", "-Wall", "-Wextra", "-Werror",
        "-I" + app + "/include", *defines, *sanitizer,
        app + "/source/gkd-app-media.c", app + "/tests/media_namespace_fixture.c",
        "-o", binary,
    ], check=True)
    result = subprocess.run(base + ["timeout", "20", binary], capture_output=True, text=True)
    (out / ("media-namespace-" + variant + ".log")).write_text(result.stdout + result.stderr)
    if result.returncode:
        print(result.stdout + result.stderr)
        result.check_returncode()
    print("GKD_MEDIA_NAMESPACE_TEST=PASS variant=" + variant)

# The existing production guard fixture proves the final global scan refuses a
# card device mounted by any process, including a foreign mount namespace.
guard = "/out/card-guard"
subprocess.run(base + [
    "cc", "-std=gnu99", "-O2", "-Wall", "-Wextra", "-Werror",
    app + "/tests/card_guard_fixture.c", "-Wl,--wrap=stat,--wrap=ioctl", "-o", guard,
], check=True)
result = subprocess.run(base + [guard], capture_output=True, text=True)
(out / "card-guard.log").write_text(result.stdout + result.stderr)
if result.returncode:
    print(result.stdout + result.stderr)
    result.check_returncode()
print("GKD_MEDIA_FOREIGN_GUARD_TEST=PASS")
