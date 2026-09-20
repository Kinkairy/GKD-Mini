#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

P1_BYTES = 131072
KERNEL_BYTES = 16384
KERNEL_OFFSET = 8192
DISK_BYTES = 65536
RECOVERY_BYTES = 524288
KERNEL_BACKUP_OFFSET = 16384
P1_BACKUP_OFFSET = 65536
ZERO_HASH = "0" * 64


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def run(command: list[str], fail_after: int | None = None,
        expected: int = 0) -> subprocess.CompletedProcess[str]:
    environment = os.environ.copy()
    if fail_after is not None:
        environment["GKDU_FAIL_AFTER"] = str(fail_after)
    result = subprocess.run(command, env=environment, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != expected:
        raise AssertionError((command, result.returncode, result.stdout, result.stderr))
    return result


def writes(result: subprocess.CompletedProcess[str]) -> int:
    match = re.search(r"writes=(\d+)", result.stdout + result.stderr)
    if not match:
        raise AssertionError(result.stdout + result.stderr)
    return int(match.group(1))


def write_fixture(root: Path) -> dict[str, object]:
    old_p1 = bytes((index * 13 + 7) & 255 for index in range(P1_BYTES))
    new_p1 = bytes((index * 29 + 3) & 255 for index in range(P1_BYTES))
    old_kernel = bytes((index * 17 + 5) & 255 for index in range(KERNEL_BYTES))
    new_kernel = bytes((index * 31 + 11) & 255 for index in range(KERNEL_BYTES))
    disk = bytearray((index * 5 + 1) & 255 for index in range(DISK_BYTES))
    disk[KERNEL_OFFSET:KERNEL_OFFSET + KERNEL_BYTES] = old_kernel
    paths = {name: root / name for name in
             ("p1", "disk", "recovery", "target-p1.gz", "target-kernel")}
    paths["p1"].write_bytes(old_p1)
    paths["disk"].write_bytes(disk)
    paths["recovery"].write_bytes(bytes(RECOVERY_BYTES))
    paths["target-p1.gz"].write_bytes(new_p1)
    paths["target-kernel"].write_bytes(new_kernel)
    paths["target-package"] = root / "target-package"
    prefix = b"signed-container-prefix"
    paths["target-package"].write_bytes(
        prefix + paths["target-p1.gz"].read_bytes() + new_kernel)
    return {"paths": paths, "old_p1": old_p1, "new_p1": new_p1,
            "old_disk": bytes(disk), "old_kernel": old_kernel,
            "new_kernel": new_kernel,
            "package": sha(b"fixture-package"), "source_p1": sha(old_p1),
            "source_kernel": sha(old_kernel), "target_p1": sha(new_p1),
            "target_kernel": sha(new_kernel)}


def commands(engine: Path, fixture: dict[str, object]) -> tuple[list[str], list[str], list[str], list[str], list[str]]:
    p = fixture["paths"]
    assert isinstance(p, dict)
    backup = [str(engine), "backup", str(p["recovery"]), str(p["p1"]),
              str(p["disk"]), str(fixture["package"]), str(fixture["source_p1"]),
              str(fixture["source_kernel"]), str(fixture["target_p1"]),
              str(fixture["target_kernel"])]
    apply = [str(engine), "apply", str(p["recovery"]), str(p["p1"]),
             str(p["disk"]), str(p["target-p1.gz"]), str(p["target-kernel"])]
    restore = [str(engine), "restore", str(p["recovery"]), str(p["p1"]),
               str(p["disk"])]
    begin = [str(engine), "begin-trial", str(p["recovery"]), str(p["p1"]),
             str(p["disk"])]
    good = [str(engine), "trial-good", str(p["recovery"]), str(p["p1"]),
            str(p["disk"])]
    return backup, apply, restore, begin, good


def assert_old(fixture: dict[str, object]) -> None:
    p = fixture["paths"]
    assert isinstance(p, dict)
    assert p["p1"].read_bytes() == fixture["old_p1"]
    assert p["disk"].read_bytes() == fixture["old_disk"]


def reset_system(fixture: dict[str, object]) -> None:
    p = fixture["paths"]
    assert isinstance(p, dict)
    p["p1"].write_bytes(fixture["old_p1"])
    p["disk"].write_bytes(fixture["old_disk"])


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_engine.py ENGINE")
    engine = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="gkdsu-engine-") as directory:
        root = Path(directory)
        fixture = write_fixture(root)
        p = fixture["paths"]
        assert isinstance(p, dict)
        backup, apply, restore, begin, good = commands(engine, fixture)

        wrong = backup.copy()
        wrong[7] = ZERO_HASH
        result = run(wrong, expected=1)
        assert "source-identity" in result.stderr
        assert_old(fixture)

        backup[6] = ZERO_HASH
        result = run(backup)
        backup_writes = writes(result)
        ready = p["recovery"].read_bytes()
        for point in range(backup_writes):
            reset_system(fixture)
            p["recovery"].write_bytes(bytes(RECOVERY_BYTES))
            run(backup, point, 99)
            assert_old(fixture)

        reset_system(fixture)
        p["recovery"].write_bytes(ready)
        corrupt = bytearray(ready)
        corrupt[P1_BACKUP_OFFSET + 20] ^= 1
        p["recovery"].write_bytes(corrupt)
        result = run(apply, expected=1)
        assert "backup-identity" in result.stderr
        assert_old(fixture)

        p["recovery"].write_bytes(ready)
        result = run(apply)
        apply_writes = writes(result)
        assert p["p1"].read_bytes() == fixture["new_p1"]
        disk = p["disk"].read_bytes()
        assert disk[KERNEL_OFFSET:KERNEL_OFFSET + KERNEL_BYTES] == fixture["new_kernel"]
        assert disk[:KERNEL_OFFSET] == fixture["old_disk"][:KERNEL_OFFSET]
        assert disk[KERNEL_OFFSET + KERNEL_BYTES:] == fixture["old_disk"][KERNEL_OFFSET + KERNEL_BYTES:]
        trial = p["recovery"].read_bytes()

        reset_system(fixture)
        p["recovery"].write_bytes(ready)
        p1_offset = len(b"signed-container-prefix")
        p1_bytes = p["target-p1.gz"].stat().st_size
        package_apply = [str(engine), "apply-package", str(p["recovery"]),
                         str(p["p1"]), str(p["disk"]), str(p["target-package"]),
                         str(p1_offset), str(p1_bytes), str(p1_offset + p1_bytes),
                         str(KERNEL_BYTES)]
        run(package_apply)
        assert p["p1"].read_bytes() == fixture["new_p1"]
        assert p["disk"].read_bytes()[KERNEL_OFFSET:KERNEL_OFFSET + KERNEL_BYTES] == fixture["new_kernel"]

        for point in range(apply_writes):
            reset_system(fixture)
            p["recovery"].write_bytes(ready)
            run(apply, point, 99)
            run(restore)
            assert_old(fixture)

        p["p1"].write_bytes(fixture["new_p1"])
        disk = bytearray(fixture["old_disk"])
        disk[KERNEL_OFFSET:KERNEL_OFFSET + KERNEL_BYTES] = fixture["new_kernel"]
        p["disk"].write_bytes(disk)
        p["recovery"].write_bytes(trial)
        result = run(begin)
        begin_writes = writes(result)
        assert "action=begin" in result.stdout
        booting = p["recovery"].read_bytes()
        p["recovery"].write_bytes(trial)
        run(begin, 0, 99)
        run(begin)
        assert p["recovery"].read_bytes() == booting

        p["recovery"].write_bytes(booting)
        run(good, 0, 99)
        result = run(good)
        good_writes = writes(result)
        assert "action=good" in result.stdout
        result = run(restore, expected=1)
        assert "restore-state" in result.stderr

        p["recovery"].write_bytes(booting)
        result = run(restore)
        restore_writes = writes(result)
        assert_old(fixture)

        for point in range(restore_writes):
            p["p1"].write_bytes(fixture["new_p1"])
            disk = bytearray(fixture["old_disk"])
            disk[KERNEL_OFFSET:KERNEL_OFFSET + KERNEL_BYTES] = fixture["new_kernel"]
            p["disk"].write_bytes(disk)
            p["recovery"].write_bytes(booting)
            run(restore, point, 99)
            run(restore)
            assert_old(fixture)

    print("GKDSU_ENGINE_FAULT_TEST=PASS backup_points=%d apply_points=%d "
          "begin_points=%d good_points=%d restore_points=%d source_rejected=1 "
          "corrupt_backup_rejected=1" %
          (backup_writes, apply_writes, begin_writes, good_writes, restore_writes))


if __name__ == "__main__":
    main()
