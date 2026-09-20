#!/usr/bin/env python3
"""Native host/controller integration. Isolated fixture root, never block devices."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

LANE = Path(__file__).resolve().parents[1]
PROJECT = LANE.parents[1]

def run(*args, **kwargs):
    return subprocess.run([str(x) for x in args], check=True, **kwargs)

def main():
    assert os.geteuid() != 0
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True,
                        help="Fresh build_init_probe.sh output for this source lane")
    parser.add_argument("--controls-mode", choices=("ready", "eof", "bad", "early", "timeout", "hung", "state-symlink", "state-device"),
                        default="ready")
    args = parser.parse_args()
    BUILD = args.build.resolve()
    work = Path(tempfile.mkdtemp(prefix="gkd-app-host-proof.", dir="/tmp/gkd-mini-public"))
    root = work / "root"
    profile = Path(tempfile.mkdtemp(prefix="gkd-app-profile-", dir="/dev/shm"))
    profile.rmdir()
    for p in ("bin", "etc", "proc", "dev", "var/run/gkd-app", "mnt/SimpleMenu", "evidence", "media/data/local/home"):
        (root / p).mkdir(parents=True, exist_ok=True)
    (root / "etc/inittab").touch()
    (root / "bin/busybox").touch()
    (root / "dev/null").touch()
    fixture = root / "mnt/SimpleMenu/simplemenu.real"
    shutil.copyfile(BUILD / "init-fixture", fixture)
    fixture.chmod(0o755)
    executable_hash = hashlib.sha256(fixture.read_bytes()).hexdigest()
    header = '#define APP_PROFILE ' + json.dumps(str(profile)) + '\n'
    header += '#define APP_INIT ' + json.dumps(str(BUILD / "busybox-init-native")) + '\n'
    header += '#define APP_LIBRARY ' + json.dumps(str(BUILD / "libgkd-app-ready-native.so")) + '\n'
    header += '#define APP_FPS_LIBRARY ' + json.dumps(str(BUILD / "libgkd-app-ready-native.so")) + '\n'
    header += '#define APP_SM_SHA256 "' + executable_hash + '"\n'
    (work / "gkd-app-manifest.generated.h").write_text(header)
    native = """
static int prepare_root(void)
{
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) ||
        mount("proc", ROOT "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) ||
        mount("/dev/null", ROOT "/dev/null", NULL, MS_BIND, NULL) ||
        gkd_app_bind_readonly(APP_INIT, ROOT "/bin/busybox") ||
        gkd_app_bind_readonly(APP_PROFILE "/inittab", ROOT "/etc/inittab") ||
        gkd_app_profile_mount(APP_PROFILE, ROOT "/var/run/gkd-app") ||
        chroot(ROOT) || chdir("/mnt/SimpleMenu")) return -1;
    return 0;
}
""".replace("ROOT", json.dumps(str(root)))
    (work / "gkd-app-host-native.generated.h").write_text(native)
    launcher = work / "controls-child"
    expected_path = root / "media/data"
    if args.controls_mode == "state-symlink":
        (work / "outside").mkdir()
        (root / "media/data/local/home/.gkdmini").symlink_to(work / "outside")
    elif args.controls_mode == "state-device":
        expected_path = Path("/dev/shm")
        assert expected_path.stat().st_dev != (root / "media/data").stat().st_dev
    cc = ["gcc", "-std=gnu99", "-O2", "-Wall", "-Wextra", "-Werror",
          "-DGKD_APP_HOST_NATIVE_TEST", "-I/lane/include", "-I/update", "-I/test", "-I/format",
          "-DGKD_APP_CONTROLS_TEST_EXPECTED_PATH=" + json.dumps(str(expected_path)),
          "-DGKD_APP_CONTROLS_LAUNCHER=" + json.dumps(str(launcher))]
    sources = ["host", "controls", "animation", "display", "profile", "diagnostics", "loop", "prepare", "ready", "exec", "watch", "init", "release", "transfer"]
    run("docker", "run", "--rm", "--network", "none", "--user", f"{os.getuid()}:{os.getgid()}",
        "-v", f"{LANE}:/lane:ro", "-v", f"{work}:/test:rw", "local/c-builder:2026.08.02-kernel",
        "gcc", "-std=gnu99", "-O2", "-Wall", "-Wextra", "-Werror", "-static", "/lane/tests/controls_child_fixture.c",
        "-o", "/test/controls-child")
    launcher.chmod(0o755)
    run("docker", "run", "--rm", "--network", "none", "--user", f"{os.getuid()}:{os.getgid()}",
        "-v", f"{LANE}:/lane:ro", "-v", f"{PROJECT}/system/rc33-system-update/source:/update:ro",
        "-v", f"{PROJECT}/kernel/current/initramfs:/format:ro",
        "-v", f"{work}:/test:rw", "local/c-builder:2026.08.02-kernel",
        *cc, *[f"/lane/source/gkd-app-{s}.c" for s in sources],
        "/update/gkd-update-sha256.c", "/lane/tests/host_time_fixture.c",
        "-Wl,--wrap=clock_gettime,--wrap=poll,--wrap=gkd_app_launch_token,--wrap=gkd_app_release_send,--wrap=gkd_app_exec_replace_report",
        "-o", "/test/host")
    results = []
    cases = ("normal",) if args.controls_mode != "ready" else (
        "normal", "late-entropy", "entropy-timeout", "cancel-before-release", "bad-app", "missing-config",
        "output-flood-timeout", "output-flood-cancel")
    for case in cases:
        (root / "etc/inittab").touch(exist_ok=True)
        for name in ("started", "orphan-reaped", "completed", "C_EXEC_CONTAINED"):
            (root / "evidence" / name).unlink(missing_ok=True)
        for name in ("identity", "flush"):
            (root / "media/data/local/home/.gkdmini" / name).unlink(missing_ok=True)
        if profile.exists():
            # Exact test-owned directory, no host mount may remain.
            assert str(profile) not in Path("/proc/self/mountinfo").read_text()
            shutil.rmtree(profile)
        if case == "bad-app":
            fixture.write_bytes(b"\x7fELF")
        else:
            shutil.copyfile(BUILD / "init-fixture", fixture)
            fixture.chmod(0o755)
        if case == "missing-config":
            (root / "etc/inittab").unlink()
        env = dict(os.environ, GKD_HOST_TIME_FIXTURE=case,
                   GKD_CONTROLS_MODE=args.controls_mode)
        result = subprocess.run(["unshare", "--user", "--map-root-user", "--mount",
                                 str(work / "host"), "5000"], env=env,
                                capture_output=True, text=True, timeout=15)
        (work / (case + ".log")).write_text(result.stdout + result.stderr)
        assert result.returncode == 1, (case, result.returncode, result.stderr)
        if args.controls_mode in ("state-symlink", "state-device"):
            assert "GKD_APP_STAGE=CONTROLS_START_FAILED" in result.stderr, result.stderr
            assert "GKD_APP_READY pid=" not in result.stderr
            assert not (root / "media/data/local/home/.gkdmini/identity").exists()
        elif args.controls_mode == "early":
            assert ("GKD_APP_STAGE=CONTROLS_READY_FAILED" in result.stderr or
                    "GKD_APP_STAGE=CONTROLS_EXITED" in result.stderr), result.stderr
            assert "GKD_APP_CONTROLS_REAPED status=0 clean=1" in result.stderr
        elif args.controls_mode == "hung":
            assert "GKD_APP_READY pid=" in result.stderr, result.stderr
            assert "GKD_APP_STAGE=APP_EXITED" in result.stderr, result.stderr
        elif args.controls_mode != "ready":
            assert "GKD_APP_READY pid=" not in result.stderr, result.stderr
            if args.controls_mode == "timeout":
                assert ("GKD_APP_STAGE=CONTROLS_READY_FAILED" in result.stderr or
                        "GKD_APP_STAGE=APP_EXITED_DURING_CONTROLS_START" in result.stderr), result.stderr
            else:
                assert "GKD_APP_STAGE=CONTROLS_READY_FAILED" in result.stderr, result.stderr
        elif case in ("normal", "late-entropy"):
            assert "GKD_APP_READY pid=" in result.stderr, result.stderr
            assert "GKD_APP_STAGE=APP_EXITED" in result.stderr
            assert (root / "evidence/orphan-reaped").exists()
        elif case == "entropy-timeout":
            assert "GKD_APP_STAGE=ENTROPY_FAILED" in result.stderr, result.stderr
            assert "GKD_APP_STAGE=ROOT_READY" not in result.stderr
            assert "GKD_APP_READY pid=" not in result.stderr
        elif case in ("cancel-before-release", "output-flood-cancel"):
            assert "GKD_APP_STAGE=APP_CANCELLED" in result.stderr, result.stderr
            assert "GKD_APP_READY pid=" not in result.stderr
        elif case == "output-flood-timeout":
            assert "GKD_APP_FAILED state=timed-out" in result.stderr, result.stderr
            assert "GKD_APP_READY pid=" not in result.stderr
        else:
            assert "GKD_APP_READY pid=" not in result.stderr
            assert "GKD_APP_STAGE=TRANSFER_FAILED" in result.stderr
        captured = profile / "child-output.log"
        if captured.exists():
            assert captured.stat().st_size <= 65536
            assert captured.stat().st_mode & 0o777 == 0o600
            (work / (case + ".child.log")).write_bytes(captured.read_bytes())
            if case in ("normal", "late-entropy"):
                assert b"EXEC_ATTEMPT" in captured.read_bytes(), captured.read_bytes()
            if case.startswith("output-flood-"):
                assert captured.stat().st_size == 65536
                assert b"EXEC_ATTEMPT" in captured.read_bytes()
                assert "eof=1" in result.stderr, result.stderr
        assert str(root) not in Path("/proc/self/mountinfo").read_text()
        if args.controls_mode == "ready" and case in ("normal", "late-entropy"):
            assert "GKD_APP_CONTROLS_REAPED status=0 clean=1" in result.stderr, result.stderr
        elif args.controls_mode == "hung":
            assert "clean=0" in result.stderr, result.stderr
        results.append({"case": case, "result": "PASS"})
    if profile.exists():
        shutil.rmtree(profile)
    (work / "result.json").write_text(json.dumps({"cases": results,
        "scope": "native C host clone/controller/preparer; synthetic root only, no card/VT/SDL"}, indent=2))
    print("GKD_MINIMAL_HOST=PASS cases=" + str(len(results)) + " controls_mode=" +
          args.controls_mode + " evidence=" + str(work))

if __name__ == "__main__":
    main()
