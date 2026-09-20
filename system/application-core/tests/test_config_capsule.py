#!/usr/bin/env python3
"""Run real shared config in an image-shaped root with native pinned BusyBox.

Native-only proof of shell/command closure, not MIPS or SM boot acceptance.
All generated files are in a new NUC temporary directory; no card access.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

PROJECT = Path(__file__).resolve().parents[3]


def run(*args, **kwargs):
    return subprocess.run([str(x) for x in args], check=True, **kwargs)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--archive", type=Path, required=True)
    p.add_argument("--capsule-root", type=Path, required=True)
    args = p.parse_args()
    assert os.geteuid() != 0
    assert hashlib.sha256(args.archive.read_bytes()).hexdigest() == \
        "ae0b029d0a9e4dd71a077a790840e496dd838998e4571b87b60fed7462b6678b"
    image = "local/c-builder:2026.08.02-kernel"
    assert run("docker", "image", "inspect", image, "--format", "{{.Id}}",
               capture_output=True, text=True).stdout.strip() == \
        "sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1"
    work = Path(tempfile.mkdtemp(prefix="gkd-app-config-proof.", dir="/tmp/gkd-mini-public"))
    run("docker", "run", "--rm", "--network", "none", "--user", f"{os.getuid()}:{os.getgid()}",
        "-v", f"{PROJECT}:/project:ro", "-v", f"{args.archive.resolve()}:/source.tar.bz2:ro",
        "-v", f"{work}:/test:rw", image, "sh", "-c", """
        set -eu
        tar --no-same-owner -xjf /source.tar.bz2 -C /test
        cd /test/busybox-1.22.1
        KCONFIG_ALLCONFIG=/project/system/application-core/tests/config-native.config make allnoconfig > /test/config.log 2>&1
        make -j2 > /test/build.log 2>&1
        cp busybox /test/busybox
        gcc -static -Os -Wall -Wextra -Werror /project/system/config-core/device/gkd-config-rename.c -o /test/gkd-config-rename
        """)
    results = []
    for case in ("missing-applets", "complete"):
        root = work / case
        shutil.copytree(args.capsule_root, root, symlinks=True)
        for source, dest in ((work / "busybox", root / "bin/busybox"),
                             (work / "gkd-config-rename", root / "usr/sbin/gkd-config-rename")):
            dest.chmod(0o755)
            shutil.copyfile(source, dest)
        for name in ("run", "dev", "var"):
            (root / name).mkdir(exist_ok=True)
        (root / "dev/null").touch()
        if case == "missing-applets":
            for name in ("wc", "tr", "sha256sum"):
                (root / "bin" / name).unlink()
        env = dict(os.environ, GKD_CONFIG_SCHEMA="/etc/gkd-mini/gdkmini.schema",
            GKD_CONFIG_MERGER="/usr/lib/gkd-config-merge.awk",
            GKD_CONFIG_OVERRIDE="/etc/gkd-mini/application.override.conf",
            GKD_CONFIG_RUN_DIR="/run/gkd-config", GKD_CONFIG_STATE_DIR="/run/gkd-config-state",
            GKD_CONFIG_RENAME="/usr/sbin/gkd-config-rename")
        def config(action, *arguments):
            return subprocess.run(["unshare", "--user", "--map-root-user", "--mount", "sh", "-c",
                'mount --make-rprivate / && mount --bind /dev/null "$1/dev/null" && root="$1" && shift && exec chroot "$root" /bin/sh /usr/sbin/gkd-config "$@"',
                "fixture", str(root), action, *arguments], env=env, capture_output=True, text=True, timeout=20)
        result = config("apply")
        (work / (case + ".log")).write_text(result.stdout + result.stderr)
        if case == "missing-applets":
            assert result.returncode != 0 and "not found" in result.stderr, result
            assert not (root / "run/gkd-config/current").exists()
        else:
            assert result.returncode == 0 and "GKD_CONFIG_COMMITTED" in result.stdout, result
            assert "usb_frontend_ready_timeout_ms=60000" in \
                (root / "run/gkd-config/current/effective.conf").read_text()
            again = config("apply")
            assert again.returncode == 0 and "GKD_CONFIG_UNCHANGED" in again.stdout, again
            value = config("get", "usb_frontend_ready_timeout_ms")
            assert value.returncode == 0 and value.stdout.strip() == "60000", value
        results.append({"case": case, "returncode": result.returncode, "result": "PASS"})
    (work / "result.json").write_text(json.dumps(results, indent=2))
    print("GKD_CONFIG_CAPSULE=PASS cases=2 evidence=" + str(work))


if __name__ == "__main__":
    main()
