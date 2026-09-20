#!/usr/bin/env python3
"""Exercise real per-game PID/mount namespaces without a device or block loop."""
import os
from pathlib import Path
import subprocess
import sys
project = Path(__file__).resolve().parents[3]
out = Path(sys.argv[1])
if out.exists() or not str(out).startswith("/tmp/gkd-mini-public/gkd-launcher-test-"):
    raise SystemExit("new scoped NUC test output required")
out.mkdir(mode=0o700)
(out / "gkd-app-fps-build.generated.h").write_text(
    '#define GKD_APP_FPS_SDL_SHA256 "' + "0" * 64 + '"\n'
    '#define GKD_APP_FPS_INTERPOSER_SHA256 "' + "0" * 64 + '"\n')
image = "local/c-builder:2026.08.02-kernel"
assert subprocess.check_output(["docker", "image", "inspect", image, "--format", "{{.Id}}"],
    text=True).strip() == "sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1"
base = ["docker", "run", "--rm", "--network", "none", "--cap-add", "SYS_ADMIN",
        "--security-opt", "seccomp=unconfined", "-v", str(project)+":/src:ro",
        "-v", str(out)+":/out:rw", image]
app = "/src/system/application-core"
for variant, flags in (("normal", []), ("ubsan", ["-fsanitize=undefined", "-fno-sanitize-recover=all"])):
    for test in ("payload", "launcher"):
        binary = "/out/" + test + "-" + variant
        subprocess.run(base+["cc", "-std=gnu99", "-O2", "-Wall", "-Wextra", "-Werror",
            '-DGKD_APP_GAME_CLIENT="' + binary + '"', "-I"+app+"/include",
            "-I/src/system/ui-core/include", "-I/out", "-I/src/system/rc33-system-update/source"]+flags+[
            app+"/source/gkd-app-payload.c", app+"/source/gkd-app-menu-launch.c", app+"/source/gkd-app-menu-config.c", app+"/source/gkd-app-settings.c", app+"/source/gkd-app-fps-launch.c",
            app+"/source/gkd-app-fps-gate.c",
            "/src/system/rc33-system-update/source/gkd-update-sha256.c",
            app+"/source/gkd-app-game-control.c",
            app+"/tests/"+test+"_fixture.c", "-o", binary], check=True)
        result = subprocess.run(base+["timeout", "15", binary], capture_output=True, text=True)
        (out/(test+"-"+variant+".log")).write_text(result.stdout+result.stderr)
        if result.returncode:
            print(result.stdout+result.stderr)
            result.check_returncode()
        print("GKD_NATIVE_TEST=PASS test="+test+" variant="+variant)
