#!/usr/bin/env python3
"""Native BusyBox-init proof; no card, network, device input or firmware writes.

Run on NUC as the ordinary aiops user after build_init_probe.sh. All mount and
PID operations are below a fresh unprivileged user namespace. Never run as root.
"""
import argparse
import array
import ctypes
import errno
import hashlib
import json
import os
from pathlib import Path
import select
import signal
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time

TOKEN = bytes(range(1, 17))
WIRE = b"GKDAPR1\0" + TOKEN


class ReadyGate(ctypes.Structure):
    # Exact public native struct from gkd-app-ready.h; uses production parser.
    _fields_ = [("fd", ctypes.c_int), ("pid", ctypes.c_int), ("uid", ctypes.c_uint),
                ("token", ctypes.c_ubyte * 16), ("deadline_ms", ctypes.c_uint64),
                ("last_ms", ctypes.c_uint64), ("state", ctypes.c_int)]


class Watch(ctypes.Structure):
    _fields_ = [("ready", ReadyGate), ("pidfd", ctypes.c_int), ("error_fd", ctypes.c_int),
               ("exec_errno", ctypes.c_int), ("error_eof", ctypes.c_int),
               ("last_ms", ctypes.c_uint64), ("state", ctypes.c_int)]


class ReleaseRx(ctypes.Structure):
    _fields_ = [("fd", ctypes.c_int), ("owner", ctypes.c_int), ("sender_pid", ctypes.c_int),
               ("sender_uid", ctypes.c_uint), ("token", ctypes.c_ubyte * 16),
               ("last_ms", ctypes.c_uint64), ("deadline_ms", ctypes.c_uint64), ("state", ctypes.c_int)]


class ReleaseTx(ctypes.Structure):
    _fields_ = [("sent", ctypes.c_int)]

class Transfer(ctypes.Structure):
    _fields_ = [("fd", ctypes.c_int), ("init_fd", ctypes.c_int), ("child_fd", ctypes.c_int),
        ("owner", ctypes.c_int), ("init_pid", ctypes.c_int), ("child_pid", ctypes.c_int),
        ("init_uid", ctypes.c_uint), ("token", ctypes.c_ubyte * 16),
        ("last_ms", ctypes.c_uint64), ("deadline_ms", ctypes.c_uint64), ("state", ctypes.c_int)]


class ExecRequest(ctypes.Structure):
    _fields_ = [("preparer_pid", ctypes.c_int), ("executable_fd", ctypes.c_int),
        ("terminal_fd", ctypes.c_int), ("output_fd", ctypes.c_int), ("ready_fd", ctypes.c_int),
        ("preload_path", ctypes.c_char_p), ("launch_token", ctypes.c_void_p),
        ("argv", ctypes.POINTER(ctypes.c_char_p)), ("base_env", ctypes.POINTER(ctypes.c_char_p))]

class PrepareRequest(ctypes.Structure):
    _fields_ = [("app", ExecRequest), ("init_executable_fd", ctypes.c_int),
        ("error_fd", ctypes.c_int), ("release_fd", ctypes.c_int),
        ("controller_pidns_fd", ctypes.c_int), ("controller_mntns_fd", ctypes.c_int),
        ("controller_mapped_pid", ctypes.c_int), ("controller_mapped_uid", ctypes.c_uint),
        ("timeout_ms", ctypes.c_uint64)]

def release_library(path):
    library = ctypes.CDLL(str(path), use_errno=True)
    library.gkd_app_release_channel.argtypes = [ctypes.POINTER(ctypes.c_int)]
    library.gkd_app_release_rx_init.argtypes = [ctypes.POINTER(ReleaseRx), ctypes.c_int,
        ctypes.c_int, ctypes.c_uint, ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint64]
    library.gkd_app_release_poll.argtypes = [ctypes.POINTER(ReleaseRx), ctypes.c_uint64]
    library.gkd_app_release_send.argtypes = [ctypes.POINTER(ReleaseTx), ctypes.POINTER(Watch),
        ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_uint64]
    library.gkd_app_transfer_send.argtypes = [ctypes.POINTER(ReleaseTx), ctypes.c_int,
        ctypes.c_int, ctypes.c_int, ctypes.c_void_p]
    library.gkd_app_transfer_init.argtypes = [ctypes.POINTER(Transfer), ctypes.c_int,
        ctypes.c_int, ctypes.c_int, ctypes.c_uint, ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint64]
    library.gkd_app_transfer_poll.argtypes = [ctypes.POINTER(Transfer), ctypes.c_uint64]
    library.gkd_app_transfer_take.argtypes = [ctypes.POINTER(Transfer), ctypes.POINTER(ctypes.c_int)]
    library.gkd_app_transfer_close.argtypes = [ctypes.POINTER(Transfer)]
    library.gkd_app_prepare_replace.argtypes = [ctypes.POINTER(PrepareRequest)]
    return library


def inside(root, sender_fd, guarded, control_fd=-1, case="", release_fd=-1, extra=()):
    # These checks run before ANY mount/chroot/exec in the test helper.
    assert os.getpid() == 1 and os.getuid() == 0
    assert Path("/proc/self/uid_map").read_text().split()[1] != "0"
    library = release_library(root / "libgkd-proof.so") if control_fd >= 0 else None
    cprepare = case.startswith("cprepare-")
    subprocess.run(["mount", "--make-rprivate", "/"], check=True)
    subprocess.run(["mount", "-t", "proc", "proc", str(root / "proc")], check=True)
    config = root / "etc/inittab"
    if (guarded or control_fd >= 0) and not config.is_file() and not cprepare:
        return 78  # Probe of the required preflight, not production code.
    if control_fd >= 0 and not cprepare:
        assert config.read_bytes() == b"", "pre-init proof requires no init actions"
    if config.exists() and case not in ("cprepare-writable-inittab", "cprepare-symlink-inittab"):
        subprocess.run(["mount", "--bind", str(config), str(config)], check=True)
        subprocess.run(["mount", "-o", "remount,bind,ro", str(config)], check=True)
        try:
            fd = os.open(config, os.O_WRONLY)
        except OSError as error:
            assert error.errno == errno.EROFS
        else:
            os.close(fd)
            raise AssertionError("test inittab was not sealed read-only")
    if cprepare:
        # Only mounting/chroot/test parameter construction remain Python here.
        # App fork/release/exec and init exec all happen inside the C preparer.
        # pass_fds crosses the test wrapper exec; restore the C caller's contract.
        for fd in (sender_fd, control_fd, release_fd, *extra):
            os.set_inheritable(fd, False)
        os.chroot(root)
        os.chdir("/")
        init = os.open("/bin/busybox", os.O_RDONLY | os.O_CLOEXEC)
        app = os.open("/fixture", os.O_RDONLY | os.O_CLOEXEC)
        argv = (ctypes.c_char_p * 3)(b"/fixture", b"--prepare-check", None)
        env = (ctypes.c_char_p * 3)(b"HOME=/", b"PATH=/bin", None)
        if case == "cprepare-env-refusal":
            env[1] = b"LD_PRELOAD=/not-allowed"
        token = ctypes.create_string_buffer(bytes.fromhex(os.environ["GKD_INIT_PROBE_TOKEN"]))
        request = PrepareRequest(
            app=ExecRequest(1, app, extra[2], extra[3], sender_fd, b"/test-only-unused-static-preload.so",
                ctypes.cast(token, ctypes.c_void_p), argv, env),
            init_executable_fd=init, error_fd=control_fd, release_fd=release_fd,
            controller_pidns_fd=extra[0], controller_mntns_fd=extra[1],
            controller_mapped_pid=0, controller_mapped_uid=0,
            timeout_ms=1500 if case == "cprepare-timeout" else 4000)
        if case == "cprepare-same-pidns":
            request.controller_pidns_fd = os.open("/proc/self/ns/pid", os.O_RDONLY | os.O_CLOEXEC)
        if case == "cprepare-same-mntns":
            request.controller_mntns_fd = os.open("/proc/self/ns/mnt", os.O_RDONLY | os.O_CLOEXEC)
        if case == "cprepare-alias":
            request.error_fd = sender_fd
        if case == "cprepare-zerotimeout":
            request.timeout_ms = 0
        if case == "cprepare-signals":
            signal.signal(signal.SIGCHLD, signal.SIG_IGN)
            signal.signal(signal.SIGPIPE, signal.SIG_IGN)
            signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGUSR1})
        assert library.gkd_app_prepare_replace(ctypes.byref(request)) == -1
        # Test-only diagnostic, then immediate worker exit.
        Path("/prepare-errno").write_text(str(ctypes.get_errno()))
        return 78
    os.dup2(sender_fd, 3, inheritable=True)
    if control_fd >= 0:
        assert control_fd > sender_fd >= 3  # Fixture allocation, not general ABI.
        os.dup2(control_fd, 4, inheritable=False)
        assert release_fd > control_fd
        os.dup2(release_fd, 5, inheritable=False)
    os.closerange(6 if control_fd >= 0 else 4, os.sysconf("SC_OPEN_MAX"))
    os.chroot(root)
    os.chdir("/")
    if control_fd >= 0:
        control = socket.socket(fileno=4)
        child = os.fork()
        if child == 0:
            # Real C transfer/release; Python still prepares/forks/execs.
            try:
                if case == "preinit-transfer-root":
                    os.chroot("/bin")
                    os.chdir("/")
                if case == "preinit-transfer-namespace":
                    assert ctypes.CDLL(None, use_errno=True).unshare(0x20000) == 0
                token = os.environ["GKD_INIT_PROBE_TOKEN"]
                expected_release = bytes.fromhex(token)
                if case == "preinit-wrong-release-token":
                    expected_release = bytes([expected_release[0] ^ 1]) + expected_release[1:]
                rx = ReleaseRx(fd=-1, state=5)
                now_ms = lambda: time.monotonic_ns() // 1000000
                # Ancestor controller is invisible here (PID 0), mapped UID 0.
                assert library.gkd_app_release_rx_init(ctypes.byref(rx), 5, 0, 0,
                                                       expected_release, now_ms(), 4000) == 0
                while library.gkd_app_release_poll(ctypes.byref(rx), now_ms()) == 0:
                    time.sleep(0.001)
                Path("/release-state").write_text(str(rx.state))
                os.close(5)
                if rx.state != 1:
                    os._exit(125)
                if case == "preinit-wrong-token":
                    token = ("01" if token[:2] == "00" else "00") + token[2:]
                target = "/missing" if case == "preinit-missing-executable" else "/fixture"
                os.execve(target, [target], {"HOME": "/", "PATH": "/bin",
                                           "GKD_APP_READY_TOKEN": token})
            except OSError as error:
                try:
                    # Fixture injection into the production C decoder; the
                    # Python preparation/barrier is still NOT shipping code.
                    control.send(b"GKDEXE1\0" + struct.pack("!I", error.errno))
                except OSError:
                    pass
            finally:
                os._exit(111)
        # No SIGCHLD auto-reap or competing wait: this pidfd refers to our child.
        app_fd = os.pidfd_open(child)
        # Fault injection bypasses C sender only for a deliberately wrong pidfd.
        if case == "preinit-transfer-init-fd":
            wrong = os.pidfd_open(os.getpid())
            control.sendmsg([b"GKDPID1\0" + bytes.fromhex(os.environ["GKD_INIT_PROBE_TOKEN"])],
                [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array("i", [wrong]))])
            os.close(wrong)
        else:
            tx = ReleaseTx()
            assert library.gkd_app_transfer_send(ctypes.byref(tx), 4, app_fd, child,
                bytes.fromhex(os.environ["GKD_INIT_PROBE_TOKEN"])) == 0, ctypes.get_errno()
        os.close(app_fd)
        control.close()
        os.close(3)  # The future init must own neither lifecycle nor frame sender.
        os.close(5)
    os.execve("/bin/busybox", ["init"], {"HOME": "/", "PATH": "/bin", "TERM": "linux"})


def launch_namespace(root, sender_fd, guarded, control_fd=-1, case="", release_fd=-1, extra=()):
    # unshare --pid (without --fork) enters PID namespace only for new children.
    # Its exec'd Python wrapper closes passed endpoints before waiting, unlike
    # util-linux's --fork wrapper, which would keep those endpoint copies alive.
    assert os.getpid() != 1 and os.getuid() == 0
    assert Path("/proc/self/uid_map").read_text().split()[1] != "0"
    signal.signal(signal.SIGCHLD, signal.SIG_DFL)
    parent_fd = os.pidfd_open(os.getpid())
    child = os.fork()
    if child == 0:
        try:
            # Pin/check the parent to close the parent-death setup race.
            libc = ctypes.CDLL(None, use_errno=True)
            assert libc.prctl(1, signal.SIGKILL, 0, 0, 0) == 0  # PR_SET_PDEATHSIG
            parent_poll = select.poll()
            parent_poll.register(parent_fd, select.POLLIN)
            if parent_poll.poll(0):
                os._exit(126)
            os.close(parent_fd)
            os._exit(inside(root, sender_fd, guarded, control_fd, case, release_fd, extra))
        finally:
            os._exit(127)
    os.close(parent_fd)
    os.close(sender_fd)
    for fd in extra:
        os.close(fd)
    if control_fd >= 0:
        os.close(control_fd)
        os.close(release_fd)
    _, status = os.waitpid(child, 0)
    return os.WEXITSTATUS(status) if os.WIFEXITED(status) else 128 + os.WTERMSIG(status)


def await_condition(predicate, process, seconds=5):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return
        if process.poll() is not None:
            raise AssertionError(f"namespace exited early: {process.returncode}")
        time.sleep(0.01)
    raise AssertionError("bounded condition timeout")


def run_case(build, work, case):
    cprepare = case.startswith("cprepare-")
    preinit = case.startswith("preinit-") or cprepare
    library = None
    token = TOKEN
    if preinit:
        library = release_library(build / "libgkd-app-ready-native.so")
        library.gkd_app_launch_token.argtypes = [ctypes.c_void_p]
        value = (ctypes.c_ubyte * 16)()
        assert library.gkd_app_launch_token(value) == 0
        token = bytes(value)
        library.gkd_app_watch_init.argtypes = [ctypes.POINTER(Watch), ctypes.c_int,
            ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_uint, ctypes.c_void_p,
            ctypes.c_uint64, ctypes.c_uint64]
        library.gkd_app_watch_poll.argtypes = [ctypes.POINTER(Watch), ctypes.c_uint64]
        library.gkd_app_watch_close.argtypes = [ctypes.POINTER(Watch)]
        library.gkd_app_init_image.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
    assert any(token)
    root = work / case
    for name in ("bin", "etc/init.d", "proc", "evidence"):
        (root / name).mkdir(parents=True, exist_ok=True)
    shutil.copyfile(build / "busybox-init-native", root / "bin/busybox")
    shutil.copyfile(build / "init-fixture", root / "fixture")
    shutil.copyfile(build / "init-fixture", root / "etc/init.d/rcS")
    if preinit:
        shutil.copyfile(build / "libgkd-app-ready-native.so", root / "libgkd-proof.so")
    for name in ("bin/busybox", "fixture", "etc/init.d/rcS"):
        (root / name).chmod(0o755)
    if case not in ("missing-inittab", "guarded-missing-inittab", "preinit-missing-inittab",
                    "cprepare-missing-inittab"):
        command = "/missing" if case == "missing-executable" else "/fixture"
        content = "" if case == "empty-inittab" or preinit else f"::once:{command}\n"
        (root / "etc/inittab").write_text(content, encoding="ascii")
        if case == "cprepare-nonempty-inittab":
            (root / "etc/inittab").write_text("::once:/fixture\n")
        if case == "cprepare-symlink-inittab":
            (root / "etc/inittab").rename(root / "etc/empty")
            (root / "etc/inittab").symlink_to("empty")
    if case in ("bad-elf", "preinit-bad-elf", "cprepare-bad-elf"):
        (root / "fixture").write_bytes(b"\x7fELF")
    if case in ("preinit-init-failure", "cprepare-init-failure"):
        (root / "bin/busybox").write_bytes(b"\x7fELF")

    receiver, sender = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
    receiver.setsockopt(socket.SOL_SOCKET, socket.SO_PASSCRED, 1)
    receiver.setblocking(False)
    sender.setblocking(False)
    sender_inode = os.fstat(sender.fileno()).st_ino
    before_ns = os.readlink("/proc/self/ns/pid")
    init_fd = None
    expected_image = None
    app_fd = None
    gate = None
    transfer = None
    controls = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET) if preinit else None
    releases = None
    if controls:
        controls[0].setsockopt(socket.SOL_SOCKET, socket.SO_PASSCRED, 1)
        controls[0].setblocking(False)
        controls[1].setblocking(False)
        pair = (ctypes.c_int * 2)(-1, -1)
        assert library.gkd_app_release_channel(pair) == 0
        releases = [socket.socket(fileno=pair[0]), socket.socket(fileno=pair[1])]
    control_inode = os.fstat(controls[1].fileno()).st_ino if controls else None
    release_inode = os.fstat(releases[1].fileno()).st_ino if releases else None
    control_args = [str(controls[1].fileno()), case, str(releases[1].fileno())] if controls else []
    passed = (sender.fileno(), controls[1].fileno(), releases[1].fileno()) if controls else (sender.fileno(),)
    extra = ()
    diagnostic_reader = -1
    if cprepare:
        diagnostic_reader, diagnostic_writer = os.pipe2(os.O_NONBLOCK | os.O_CLOEXEC)
        extra = (os.open("/proc/self/ns/pid", os.O_RDONLY | os.O_CLOEXEC),
                 os.open("/proc/self/ns/mnt", os.O_RDONLY | os.O_CLOEXEC),
                 os.open("/dev/null", os.O_RDWR | os.O_CLOEXEC), diagnostic_writer)
        passed += extra
        control_args += [str(fd) for fd in extra]
    def read_diagnostics():
        deadline = time.monotonic() + 2
        chunks = []
        while time.monotonic() < deadline:
            try:
                chunk = os.read(diagnostic_reader, 4096)
            except BlockingIOError:
                time.sleep(0.01)
                continue
            if not chunk:
                break
            chunks.append(chunk)
        data = b"".join(chunks)
        assert data and data.endswith(b"\n")
        lines = data.splitlines()
        assert all(line.startswith(b"GKD_APP phase=") and b" errno=" in line and
                   b" mono_ms=" in line and b" release_state=" in line for line in lines)
        return lines
    process = None
    with (root / "console.log").open("wb") as log:
        try:
            process = subprocess.Popen(
                ["unshare", "--user", "--map-root-user", "--mount", "--pid",
                 sys.executable, __file__, "--launch", str(root), str(sender.fileno()),
                 "guard" if case == "guarded-missing-inittab" else "raw"] + control_args,
                pass_fds=passed, stdin=subprocess.DEVNULL,
                stdout=log, stderr=log,
                env={"PATH": os.defpath, "GKD_INIT_PROBE_TOKEN": token.hex()},
            )
            sender.close()
            if controls:
                controls[1].close()
                releases[1].close()
            if case in ("guarded-missing-inittab", "preinit-missing-inittab"):
                assert process.wait(timeout=5) == 78
                assert not list((root / "evidence").iterdir())
                return {"case": case, "result": "PASS", "scope": "test preflight only"}
            if case == "preinit-init-failure":
                assert process.wait(timeout=5) == 127
                assert not list((root / "evidence").iterdir())
                try:
                    receiver.recv(64)
                    raise AssertionError("failed init produced readiness")
                except BlockingIOError:
                    pass
                return {"case": case, "result": "PASS"}

            preflight_errors = {
                "cprepare-missing-inittab": errno.ENOENT, "cprepare-nonempty-inittab": errno.EPERM,
                "cprepare-writable-inittab": errno.EPERM, "cprepare-symlink-inittab": errno.ELOOP,
                "cprepare-same-pidns": errno.EPERM,
                "cprepare-same-mntns": errno.EPERM, "cprepare-alias": errno.EINVAL,
                "cprepare-zerotimeout": errno.EINVAL}
            if case in preflight_errors:
                assert process.wait(timeout=5) == 78
                assert (root / "prepare-errno").read_text() == str(preflight_errors[case])
                assert not list((root / "evidence").iterdir())
                assert not (root / "proc/1/task/1/children").exists(), "namespace must exit"
                return {"case": case, "result": "PASS", "scope": "C preflight refusal before app fork"}
            if case == "cprepare-init-failure":
                assert process.wait(timeout=5) == 78
                assert (root / "prepare-errno").read_text() == str(errno.ENOEXEC)
                # A four-byte ELF passes the signature preflight: the real exec
                # fails AFTER transfer/fork, and PID 1 exit must kill the blocked app.
                packet, ancillary, flags, _ = controls[0].recvmsg(24, 256, socket.MSG_CMSG_CLOEXEC)
                rights = []
                for level, kind, payload in ancillary:
                    if level == socket.SOL_SOCKET and kind == socket.SCM_RIGHTS:
                        received = array.array("i")
                        received.frombytes(payload)
                        rights.extend(received)
                try:
                    assert not flags & (socket.MSG_TRUNC | socket.MSG_CTRUNC)
                    assert packet == b"GKDPID1\0" + token and len(rights) == 1
                    dead = select.poll()
                    dead.register(rights[0], select.POLLIN)
                    assert dead.poll(1000), "failed init must tear down its blocked app"
                finally:
                    for fd in rights:
                        os.close(fd)
                assert not list((root / "evidence").iterdir())
                assert not (root / "proc/1/task/1/children").exists()
                return {"case": case, "result": "PASS", "scope": "C init exec failure after fork; blocked app exits"}
            children_path = Path(f"/proc/{process.pid}/task/{process.pid}/children")
            await_condition(lambda: children_path.exists() and bool(children_path.read_text().split()), process)
            children = children_path.read_text().split()
            assert len(children) == 1
            init_pid = int(children[0])
            init_fd = os.pidfd_open(init_pid)
            assert os.readlink(f"/proc/{init_pid}/ns/pid") != before_ns
            if preinit:
                expected_image = os.open(root / "bin/busybox", os.O_PATH | os.O_CLOEXEC)
                def init_image_observed():
                    result = library.gkd_app_init_image(init_fd, init_pid, expected_image)
                    assert result >= 0, f"C init image observation failed: {ctypes.get_errno()}"
                    return result == 1
                await_condition(init_image_observed, process)
            else:
                await_condition(lambda: os.path.samefile(f"/proc/{init_pid}/exe", root / "bin/busybox"), process)
            poller = select.poll()
            poller.register(init_fd, select.POLLIN)
            child_list = Path(f"/proc/{init_pid}/task/{init_pid}/children")

            if preinit:
                now_ms = lambda: time.monotonic_ns() // 1000000
                transfer = Transfer(fd=-1, init_fd=-1, child_fd=-1, state=5)
                assert library.gkd_app_transfer_init(ctypes.byref(transfer), controls[0].fileno(),
                    init_fd, init_pid, os.getuid(), token, now_ms(), 4000) == 0
                # Fault cases synchronize the child's changed root/namespace
                # before observing the C receipt (not a production barrier).
                if case == "preinit-transfer-root":
                    await_condition(lambda: any(os.path.samefile(f"/proc/{pid}/root", root / "bin")
                        for pid in child_list.read_text().split()), process)
                if case == "preinit-transfer-namespace":
                    await_condition(lambda: any(os.readlink(f"/proc/{pid}/ns/mnt") !=
                        os.readlink(f"/proc/{init_pid}/ns/mnt")
                        for pid in child_list.read_text().split()), process)
                await_condition(lambda: library.gkd_app_transfer_poll(ctypes.byref(transfer), now_ms()) != 0, process)
                if case in ("preinit-transfer-root", "preinit-transfer-namespace", "preinit-transfer-init-fd"):
                    assert transfer.state == 4 and transfer.child_fd == -1
                    assert not list((root / "evidence").iterdir())
                    return {"case": case, "result": "PASS", "scope": "C transfer identity refusal before release"}
                assert transfer.state == 1, (transfer.state, ctypes.get_errno())
                if case == "preinit-transfer-death":
                    signal.pidfd_send_signal(transfer.child_fd, signal.SIGKILL)
                    died = select.poll()
                    died.register(transfer.child_fd, select.POLLIN)
                    assert died.poll(3000)
                    number = ctypes.c_int()
                    assert library.gkd_app_transfer_take(ctypes.byref(transfer), ctypes.byref(number)) == -1
                    assert not list((root / "evidence").iterdir())
                    return {"case": case, "result": "PASS", "scope": "death before take refuses handoff"}
                number = ctypes.c_int()
                app_fd = library.gkd_app_transfer_take(ctypes.byref(transfer), ctypes.byref(number))
                assert app_fd >= 3 and transfer.child_fd == -1 and transfer.state == 6
                app_pid = number.value
                assert library.gkd_app_transfer_take(ctypes.byref(transfer), ctypes.byref(number)) == -1
                assert not os.get_inheritable(app_fd)
                assert child_list.read_text().split() == [str(app_pid)]
                assert os.readlink(f"/proc/{app_pid}/ns/pid") == os.readlink(f"/proc/{init_pid}/ns/pid")
                # These checks happen while the application is blocked, BEFORE GO.
                assert b"GKD_" not in Path(f"/proc/{init_pid}/environ").read_bytes()
                for owner in (init_pid, process.pid):
                    for fd in Path(f"/proc/{owner}/fd").iterdir():
                        try:
                            assert fd.stat().st_ino not in (sender_inode, control_inode, release_inode)
                        except FileNotFoundError:
                            pass  # Transient init logging descriptor.
                gate = Watch(ready=ReadyGate(fd=-1, state=5), pidfd=-1, error_fd=-1, state=6)
                now_ms = lambda: time.monotonic_ns() // 1000000
                result = library.gkd_app_watch_init(ctypes.byref(gate), app_fd, controls[0].fileno(), receiver.fileno(),
                    init_pid if case == "preinit-wrong-identity" else app_pid,
                    os.getuid(), token, now_ms(), 5000)
                if case == "preinit-wrong-identity":
                    assert result == -1 and ctypes.get_errno() == errno.EINVAL
                    assert gate.pidfd == -1 and not list((root / "evidence").iterdir())
                    return {"case": case, "result": "PASS", "scope": "C pidfd binding refused before release"}
                assert result == 0
                receiver.detach()
                controls[0].detach()
                app_fd = None  # Watch now owns all three descriptors.
                app_poll = select.poll()
                app_poll.register(gate.pidfd, select.POLLIN)
                assert library.gkd_app_watch_poll(ctypes.byref(gate), now_ms()) == 0
                tx = ReleaseTx()
                if case in ("preinit-cancel", "cprepare-cancel"):
                    releases[0].close()
                elif case == "cprepare-timeout":
                    pass
                elif case == "cprepare-wrong-release":
                    bad = bytes([token[0] ^ 1]) + token[1:]
                    releases[0].send(b"GKDREL1\0" + bad)  # Deliberate bad-controller fixture.
                    releases[0].close()
                else:
                    if case == "preinit-image-mismatch":
                        other_image = os.open(root / "fixture", os.O_PATH | os.O_CLOEXEC)
                        try:
                            assert library.gkd_app_release_send(ctypes.byref(tx), ctypes.byref(gate),
                                releases[0].fileno(), init_fd, init_pid, other_image, now_ms()) == -1
                            assert ctypes.get_errno() == errno.EAGAIN and not tx.sent
                        finally:
                            os.close(other_image)
                        assert not list((root / "evidence").iterdir())
                        return {"case": case, "result": "PASS", "scope": "C image mismatch prevents C release"}
                    assert library.gkd_app_release_send(ctypes.byref(tx), ctypes.byref(gate),
                        releases[0].fileno(), init_fd, init_pid, expected_image, now_ms()) == 0
                    assert tx.sent == 1
                    releases[0].close()
                await_condition(lambda: library.gkd_app_watch_poll(ctypes.byref(gate), now_ms()) != 0, process)
                if case in ("preinit-normal", "preinit-wrong-token", "cprepare-normal", "cprepare-signals"):
                    assert gate.state == (5 if case == "preinit-wrong-token" else 1)
                    if case != "preinit-wrong-token":
                        assert gate.error_eof, "C observer must confirm sender closure AND frame"
                    await_condition(lambda: (root / "evidence/completed").exists(), process)
                    assert (root / "evidence/orphan-reaped").exists()
                    if cprepare:
                        assert (root / "evidence/C_EXEC_CONTAINED").exists()
                elif case not in ("preinit-cancel", "preinit-wrong-release-token",
                                  "cprepare-cancel", "cprepare-timeout", "cprepare-wrong-release"):
                    expected = errno.ENOENT if case == "preinit-missing-executable" else (
                        errno.EINVAL if case == "cprepare-env-refusal" else errno.ENOEXEC)
                    assert gate.state == 2 and gate.exec_errno == expected
                if case in ("preinit-cancel", "preinit-wrong-release-token"):
                    assert (root / "release-state").read_text() == ("2" if case == "preinit-cancel" else "4")
                assert app_poll.poll(3000), "exact application must become exited"
                state = library.gkd_app_watch_poll(ctypes.byref(gate), now_ms())
                assert state == (5 if case == "preinit-wrong-token" else
                                 2 if case in ("preinit-missing-executable", "preinit-bad-elf",
                                               "cprepare-bad-elf", "cprepare-env-refusal") else 3)
                await_condition(lambda: not child_list.read_text().strip(), process)
                if cprepare and case in ("cprepare-timeout", "cprepare-wrong-release", "cprepare-bad-elf"):
                    lines = read_diagnostics()
                    expected_phase = {"cprepare-timeout": b"phase=RELEASE_WAIT errno=110",
                                      "cprepare-wrong-release": b"phase=RELEASE_RECEIVE errno=0",
                                      "cprepare-bad-elf": b"phase=EXEC_FAILURE errno=8"}[case]
                    assert any(expected_phase in line for line in lines), (case, lines)
                assert not poller.poll(0), "init remains available for reaping"
                if case not in ("preinit-normal", "preinit-wrong-token", "preinit-wrong-identity",
                                "cprepare-normal", "cprepare-signals"):
                    assert not list((root / "evidence").iterdir())
            elif case == "normal":
                await_condition(lambda: (root / "evidence/completed").exists(), process)
                assert (root / "evidence/orphan-reaped").read_text() == "ok\n"
                data, ancillary, flags, _ = receiver.recvmsg(64, socket.CMSG_SPACE(12))
                assert data == WIRE and flags == 0
                credentials = [struct.unpack("3i", value) for level, kind, value in ancillary
                               if level == socket.SOL_SOCKET and kind == socket.SCM_CREDENTIALS]
                assert len(credentials) == 1
                assert credentials[0][0] > 1 and credentials[0][1] == os.getuid()
                # The source PID is translated to the receiving parent namespace.
                # It is not production acceptance: that requires a pre-bound child.
                await_condition(lambda: not child_list.read_text().strip(), process)
                assert not poller.poll(200), "init must outlive its once workload"
                assert os.stat(f"/proc/{init_pid}/fd/3").st_ino == sender_inode
            elif case == "missing-inittab":
                await_condition(lambda: (root / "evidence/OLD_RCS_EXECUTED").exists(), process)
                assert not poller.poll(200)
            else:
                if case in ("missing-executable", "bad-elf"):
                    await_condition(lambda: "can't run" in (root / "console.log").read_text(), process)
                else:
                    time.sleep(0.2)
                assert process.poll() is None and not poller.poll(0)
                assert not list((root / "evidence").iterdir())
                try:
                    receiver.recv(64)
                    raise AssertionError("failure produced a frame event")
                except BlockingIOError:
                    pass
            if case != "missing-inittab":
                assert not (root / "evidence/OLD_RCS_EXECUTED").exists()
            assert str(root) not in Path("/proc/self/mountinfo").read_text()
            return {"case": case, "result": "PASS"}
        finally:
            if process is not None and process.poll() is None:
                # Kill our exact Popen wrapper; pinned-parent PDEATHSIG kills PID 1.
                process.kill()
                process.wait(timeout=5)
            if init_fd is not None:
                poller = select.poll()
                poller.register(init_fd, select.POLLIN)
                assert poller.poll(3000), "test namespace survived its launcher"
                os.close(init_fd)
            if app_fd is not None:
                os.close(app_fd)
            if transfer is not None:
                library.gkd_app_transfer_close(ctypes.byref(transfer))
            if expected_image is not None:
                os.close(expected_image)
            if gate is not None:
                library.gkd_app_watch_close(ctypes.byref(gate))
            if controls:
                for control in controls:
                    control.close()
            if releases:
                for release in releases:
                    release.close()
            for fd in extra:
                os.close(fd)
            if diagnostic_reader >= 0:
                os.close(diagnostic_reader)
            receiver.close()
            sender.close()


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--launch":
        return launch_namespace(Path(sys.argv[2]), int(sys.argv[3]), sys.argv[4] == "guard",
                      int(sys.argv[5]) if len(sys.argv) > 5 else -1,
                      sys.argv[6] if len(sys.argv) > 6 else "",
                      int(sys.argv[7]) if len(sys.argv) > 7 else -1,
                      tuple(map(int, sys.argv[8:])))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    args = parser.parse_args()
    assert os.geteuid() != 0, "run only as unprivileged NUC user"
    build = args.build.resolve(strict=True)
    assert build.is_relative_to("/tmp/gkd-mini-public")
    boot_id = Path("/proc/sys/kernel/random/boot_id").read_text()
    namespace = os.readlink("/proc/self/ns/mnt")
    # Retain evidence alongside the build; no recursive host cleanup is needed.
    work = Path(tempfile.mkdtemp(prefix="namespace-cases.", dir=build))
    cases = ("normal", "missing-executable", "bad-elf", "empty-inittab",
             "missing-inittab", "guarded-missing-inittab", "preinit-normal",
             "preinit-missing-executable", "preinit-bad-elf", "preinit-cancel", "preinit-wrong-token",
             "preinit-wrong-identity", "preinit-init-failure", "preinit-missing-inittab",
             "preinit-image-mismatch", "preinit-wrong-release-token", "preinit-transfer-root",
             "preinit-transfer-namespace", "preinit-transfer-init-fd", "preinit-transfer-death",
             "cprepare-normal", "cprepare-signals", "cprepare-cancel", "cprepare-timeout",
             "cprepare-bad-elf", "cprepare-env-refusal", "cprepare-missing-inittab",
             "cprepare-nonempty-inittab", "cprepare-writable-inittab", "cprepare-symlink-inittab",
             "cprepare-init-failure", "cprepare-same-pidns", "cprepare-same-mntns",
             "cprepare-alias", "cprepare-zerotimeout", "cprepare-wrong-release")
    results = []
    fd_count = len(os.listdir("/proc/self/fd"))
    for case in cases:
        result = run_case(build, work, case)
        assert len(os.listdir("/proc/self/fd")) == fd_count, "test leaked a descriptor"
        results.append(result)
        print(result, flush=True)
    assert boot_id == Path("/proc/sys/kernel/random/boot_id").read_text()
    assert namespace == os.readlink("/proc/self/ns/mnt")
    hashes = {name: hashlib.sha256((build / name).read_bytes()).hexdigest()
              for name in ("busybox-init-native", "init-fixture", "libgkd-app-ready-native.so")}
    (work / "result.json").write_text(json.dumps({
        "result": "PASS", "cases": results, "binary_sha256": hashes,
        "host_unchanged": True, "target_hardware_tested": False,
        "host_descriptor_leaks": 0,
        "production_launch_guard_implemented": False,
        "production_C_watch_in_preinit_proof": True,
        "production_C_init_image_observation": True,
        "production_C_release_barrier": True,
        "production_C_pidfd_transfer_and_identity": True,
        "prepared_root_C_fork_transfer_release_app_exec_and_init_exec": True,
        "namespace_creation_mount_chroot_and_config_seal_still_Python": True,
    }, indent=2) + "\n", encoding="utf-8")
    print(f"GKD_BUSYBOX_INIT=PASS cases={len(cases)} host-unchanged=PASS evidence={work}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
