#!/usr/bin/env python3
"""Native proof of the real gkd-app-host --service-fd 3 lifecycle."""
import argparse
import atexit
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import socket
import struct
import subprocess
import tempfile
import time

LANE = Path(__file__).resolve().parents[1]
PROJECT = LANE.parents[1]
MAGIC = 0x474B4131
READY = struct.Struct("=IIiii")
LIVE_CHILDREN = set()


def cleanup_children():
    for pid in tuple(LIVE_CHILDREN):
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    deadline = time.monotonic() + 5
    while LIVE_CHILDREN and time.monotonic() < deadline:
        for pid in tuple(LIVE_CHILDREN):
            try:
                got, _ = os.waitpid(pid, os.WNOHANG)
            except ChildProcessError:
                got = pid
            if got == pid:
                LIVE_CHILDREN.discard(pid)
        if LIVE_CHILDREN:
            time.sleep(0.02)
    for pid in tuple(LIVE_CHILDREN):
        try:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
        except (ChildProcessError, ProcessLookupError):
            pass
        LIVE_CHILDREN.discard(pid)


atexit.register(cleanup_children)


def run(*args):
    subprocess.run([str(value) for value in args], check=True)


def wait_child(pid, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        got, status = os.waitpid(pid, os.WNOHANG)
        if got == pid:
            LIVE_CHILDREN.discard(pid)
            return status
        time.sleep(0.02)
    os.kill(pid, signal.SIGKILL)
    os.waitpid(pid, 0)
    LIVE_CHILDREN.discard(pid)
    raise AssertionError(f"child {pid} did not exit within {timeout}s")


def process_parent(pid):
    fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
    return int(fields[1])


def spawn_host(work, socket_type, controls_mode):
    parent, child = socket.socketpair(socket.AF_UNIX, socket_type)
    log_path = work / f"host-{time.monotonic_ns()}.log"
    log_fd = os.open(log_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    pid = os.fork()
    if pid == 0:
        try:
            parent.close()
            if child.fileno() != 3:
                os.dup2(child.fileno(), 3)
            os.dup2(log_fd, 1)
            os.dup2(log_fd, 2)
            for fd in range(4, 256):
                try:
                    os.close(fd)
                except OSError:
                    pass
            env = dict(os.environ, GKD_CONTROLS_MODE=controls_mode,
                       GKD_HOST_TIME_FIXTURE="service")
            os.execve(str(work / "host"),
                      [str(work / "host"), "5000", "--service-fd", "3"], env)
        finally:
            os._exit(127)
    child.close()
    os.close(log_fd)
    LIVE_CHILDREN.add(pid)
    return pid, parent, log_path


def launch_ready(work, profile, root):
    for name in ("identity", "flush"):
        (root / "media/data/local/home/.gkdmini" / name).unlink(missing_ok=True)
    pid, endpoint, log_path = spawn_host(work, socket.SOCK_SEQPACKET, "ready")
    endpoint.settimeout(10)
    payload = endpoint.recv(128)
    assert len(payload) == READY.size, (len(payload), payload)
    magic, version, host, init, application = READY.unpack(payload)
    assert (magic, version, host) == (MAGIC, 1, pid)
    assert init > 1 and application > 1 and len({host, init, application}) == 3
    assert process_parent(init) == host
    assert process_parent(application) == init
    assert Path(f"/proc/{host}").exists()
    assert Path(f"/proc/{init}").exists()
    assert Path(f"/proc/{application}").exists()
    identity = (root / "media/data/local/home/.gkdmini/identity").read_text()
    assert f"parent={host}" in identity, identity

    os.kill(pid, signal.SIGTERM)
    status = wait_child(pid, 12)
    endpoint.close()
    assert os.waitstatus_to_exitcode(status) == 0, (status, log_path.read_text())
    for process in (host, init, application):
        assert not Path(f"/proc/{process}").exists(), process
    flush = root / "media/data/local/home/.gkdmini/flush"
    assert flush.read_text() == "clean=1\n"
    profile_path = Path(f"{profile}.{host}")
    mountinfo = Path("/proc/self/mountinfo").read_text()
    assert str(profile_path) not in mountinfo
    assert str(root) not in mountinfo
    log = log_path.read_text()
    assert "GKD_APP_READY pid=" in log
    assert "GKD_APP_STAGE=APP_CANCELLED" in log
    assert "GKD_APP_CONTROLS_REAPED status=0 clean=1" in log
    assert "LOOP_CLEANUP_FAILED" not in log
    assert "NAMESPACE_REAP_FAILED" not in log
    assert "NAMESPACE_HELD_BY_CONTROLS" not in log
    shutil.rmtree(profile_path)
    return {"host": host, "init": init, "application": application,
            "log": str(log_path)}


def early_exit_fails(work, profile):
    pid, endpoint, log_path = spawn_host(work, socket.SOCK_SEQPACKET, "eof")
    endpoint.settimeout(10)
    payload = endpoint.recv(128)
    assert payload == b"", payload
    status = wait_child(pid, 12)
    endpoint.close()
    assert os.waitstatus_to_exitcode(status) == 1, (status, log_path.read_text())
    log = log_path.read_text()
    assert "GKD_APP_STAGE=CONTROLS_READY_FAILED" in log
    assert "GKD_APP_READY pid=" not in log
    profile_path = Path(f"{profile}.{pid}")
    assert str(profile_path) not in Path("/proc/self/mountinfo").read_text()
    shutil.rmtree(profile_path)
    return {"exit": 1, "log": str(log_path)}


def malformed_endpoint_fails(work):
    pid, endpoint, log_path = spawn_host(work, socket.SOCK_STREAM, "ready")
    endpoint.settimeout(3)
    status = wait_child(pid, 3)
    assert endpoint.recv(1) == b""
    endpoint.close()
    assert os.waitstatus_to_exitcode(status) == 64, (status, log_path.read_text())
    return {"exit": 64, "log": str(log_path)}


def controller(work, profile, root):
    first = launch_ready(work, profile, root)
    second = launch_ready(work, profile, root)
    assert first["host"] != second["host"]
    early = early_exit_fails(work, profile)
    malformed = malformed_endpoint_fails(work)
    result = {
        "cases": {
            "ready_clean_stop": "PASS",
            "same_boot_restart": "PASS",
            "early_controls_exit": "PASS",
            "malformed_endpoint": "PASS",
        },
        "ready": [first, second],
        "early": early,
        "malformed": malformed,
        "scope": "actual gkd-app-host service fd; synthetic root only; no block devices",
    }
    (work / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"GKD_APP_HOST_SERVICE=PASS evidence={work}")


def build_and_run(build):
    assert os.geteuid() != 0
    work = Path(tempfile.mkdtemp(prefix="gkd-app-host-service.", dir="/tmp/gkd-mini-public"))
    (work / "loop-devices").mkdir()
    root = work / "root"
    profile = Path(tempfile.mkdtemp(prefix="gkd-app-service-profile-", dir="/dev/shm"))
    profile.rmdir()
    for relative in ("bin", "etc", "proc", "dev", "var/run/gkd-app",
                     "mnt/SimpleMenu", "media/data/local/home", "evidence"):
        (root / relative).mkdir(parents=True, exist_ok=True)
    (root / "etc/inittab").touch()
    (root / "bin/busybox").touch()
    (root / "dev/null").touch()

    image = "local/c-builder:2026.08.02-kernel"
    expected_image = "sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1"
    actual_image = subprocess.check_output(
        ["docker", "image", "inspect", image, "--format", "{{.Id}}"], text=True).strip()
    assert actual_image == expected_image

    base = ["docker", "run", "--rm", "--network", "none",
            "--user", f"{os.getuid()}:{os.getgid()}",
            "-v", f"{LANE}:/lane:ro", "-v", f"{work}:/test:rw", image]
    run(*base, "gcc", "-std=gnu99", "-static", "-O2", "-Wall", "-Wextra", "-Werror",
        "-I/lane/include", "/lane/tests/host_service_app_fixture.c",
        "/lane/source/gkd-app-ready.c", "-o", "/test/service-app")
    shutil.copyfile(work / "service-app", root / "mnt/SimpleMenu/simplemenu.real")
    (root / "mnt/SimpleMenu/simplemenu.real").chmod(0o755)
    executable_hash = hashlib.sha256(
        (root / "mnt/SimpleMenu/simplemenu.real").read_bytes()).hexdigest()

    header = (
        "#define APP_PROFILE " + json.dumps(str(profile)) + "\n"
        "#define APP_GAME_CLIENT " + json.dumps(str(work / "service-app")) + "\n"
        "#define APP_INIT " + json.dumps(str(build / "busybox-init-native")) + "\n"
        "#define APP_LIBRARY " + json.dumps(str(build / "libgkd-app-ready-native.so")) + "\n"
        "#define APP_FPS_LIBRARY " + json.dumps(str(build / "libgkd-app-ready-native.so")) + "\n"
        '#define APP_SM_SHA256 "' + executable_hash + '"\n'
    )
    (work / "gkd-app-manifest.generated.h").write_text(header)
    native = """
static int prepare_root(void)
{
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) ||
        mount("proc", ROOT "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) ||
        mount("/dev/null", ROOT "/dev/null", NULL, MS_BIND, NULL) ||
        gkd_app_bind_readonly(APP_INIT, ROOT "/bin/busybox") ||
        gkd_app_bind_readonly(profile_inittab, ROOT "/etc/inittab") ||
        gkd_app_profile_mount(profile, ROOT "/var/run/gkd-app") ||
        chroot(ROOT) || chdir("/mnt/SimpleMenu")) return -1;
    return 0;
}
""".replace("ROOT", json.dumps(str(root)))
    (work / "gkd-app-host-native.generated.h").write_text(native)

    launcher = work / "controls-child"
    run(*base, "gcc", "-std=gnu99", "-O2", "-Wall", "-Wextra", "-Werror", "-static",
        "/lane/tests/controls_child_fixture.c", "-o", "/test/controls-child")
    launcher.chmod(0o755)
    sources = ["host", "controls", "animation", "display", "profile", "diagnostics",
               "loop", "prepare", "ready", "exec", "watch", "init", "release", "transfer"]
    run("docker", "run", "--rm", "--network", "none",
        "--user", f"{os.getuid()}:{os.getgid()}",
        "-v", f"{LANE}:/lane:ro",
        "-v", f"{PROJECT}/system/rc33-system-update/source:/update:ro",
        "-v", f"{PROJECT}/kernel/current/initramfs:/format:ro",
        "-v", f"{work}:/test:rw", image,
        "gcc", "-std=gnu99", "-O2", "-Wall", "-Wextra", "-Werror",
        "-DGKD_APP_LOOP_DEVICE_ROOT=" + json.dumps(str(work / "loop-devices")),
        "-DGKD_APP_HOST_NATIVE_TEST", "-I/lane/include", "-I/update", "-I/test", "-I/format",
        "-DGKD_APP_CONTROLS_TEST_EXPECTED_PATH=" + json.dumps(str(root / "media/data")),
        "-DGKD_APP_CONTROLS_LAUNCHER=" + json.dumps(str(launcher)),
        *[f"/lane/source/gkd-app-{name}.c" for name in sources],
        "/update/gkd-update-sha256.c", "/lane/tests/host_time_fixture.c",
        "-Wl,--wrap=clock_gettime,--wrap=poll,--wrap=gkd_app_launch_token,"
        "--wrap=gkd_app_release_send,--wrap=gkd_app_exec_replace_report",
        "-o", "/test/host")

    run("unshare", "--user", "--map-root-user", "--mount",
        str(Path(__file__).resolve()), "--controller",
        "--work", work, "--profile", profile, "--root", root)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path)
    parser.add_argument("--controller", action="store_true")
    parser.add_argument("--work", type=Path)
    parser.add_argument("--profile", type=Path)
    parser.add_argument("--root", type=Path)
    args = parser.parse_args()
    if args.controller:
        assert os.geteuid() == 0
        controller(args.work, args.profile, args.root)
        return
    if args.build is None:
        parser.error("--build is required")
    build_and_run(args.build.resolve())


if __name__ == "__main__":
    main()
