#!/usr/bin/env python3
import hashlib, os, struct, subprocess, sys, tempfile, zlib
from pathlib import Path

P1_BYTES, KERNEL_BYTES, KERNEL_OFFSET = 131072, 16384, 8192
RECOVERY_BYTES, REQUEST_OFFSET = 524288, 20967424
DISK_BYTES = 31457280

def sha(value): return hashlib.sha256(value).digest()

def request_bytes(package, old_p1, old_kernel, new_p1, new_kernel, p1_offset, p1_bytes):
    raw = bytearray(4096)
    raw[:8] = b"GKDRQ1\0\0"
    struct.pack_into("<II", raw, 8, 1, 4096)
    values = (sha(package), bytes.fromhex("11" * 32), bytes.fromhex("22" * 32),
              sha(old_p1), sha(old_kernel), sha(new_p1), sha(new_kernel))
    at = 16
    for value in values:
        raw[at:at + 32] = value; at += 32
    struct.pack_into("<QQQQQ", raw, at, len(package), p1_offset, p1_bytes,
                     p1_offset + p1_bytes, KERNEL_BYTES)
    struct.pack_into("<I", raw, 4092, zlib.crc32(raw[:4092]))
    return raw

def run(args, expected):
    result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode != expected:
        raise AssertionError((args, result.returncode, result.stdout, result.stderr))
    return result

def main():
    coordinator, engine = map(str, map(Path, sys.argv[1:3]))
    with tempfile.TemporaryDirectory(prefix="gkdsu-coordinator-") as name:
        root = Path(name)
        old_p1 = bytes((i * 13 + 7) & 255 for i in range(P1_BYTES))
        new_p1 = bytes((i * 29 + 3) & 255 for i in range(P1_BYTES))
        old_kernel = bytes((i * 17 + 5) & 255 for i in range(KERNEL_BYTES))
        new_kernel = bytes((i * 31 + 11) & 255 for i in range(KERNEL_BYTES))
        packed = new_p1; prefix = b"signed-prefix"
        package = prefix + packed + new_kernel
        paths = {n: root / n for n in ("package", "recovery", "p1", "disk")}
        paths["package"].write_bytes(package); paths["recovery"].write_bytes(bytes(RECOVERY_BYTES))
        paths["p1"].write_bytes(old_p1)
        with paths["disk"].open("wb") as stream: stream.truncate(DISK_BYTES)
        with paths["disk"].open("r+b") as stream:
            stream.seek(KERNEL_OFFSET); stream.write(old_kernel)
            stream.seek(REQUEST_OFFSET)
            stream.write(request_bytes(package, old_p1, old_kernel, new_p1, new_kernel,
                                       len(prefix), len(packed)))
        command = [coordinator, str(paths["package"]), str(paths["recovery"]),
                   str(paths["p1"]), str(paths["disk"]), engine]
        r_command = [sys.argv[3], *command[1:]] if len(sys.argv) > 3 else command
        run(r_command, 10)
        assert paths["p1"].read_bytes() == new_p1
        if len(sys.argv) > 3:
            pending = paths["recovery"].read_bytes()
            run(r_command, 10)
            assert paths["recovery"].read_bytes() == pending, "R consumed A trial"
        run(command, 0)
        # A trial failed; R restores without the original package present.
        paths["package"].unlink()
        run(r_command, 20)
        assert paths["p1"].read_bytes() == old_p1
        with paths["disk"].open("rb") as stream:
            stream.seek(KERNEL_OFFSET); assert stream.read(KERNEL_BYTES) == old_kernel
            stream.seek(REQUEST_OFFSET); assert stream.read(4096) == bytes(4096)
        recovery_after = paths["recovery"].read_bytes()
        assert recovery_after[4086:4096] == b"SWAPSPACE2"
        assert recovery_after[4096:8192] == bytes(4096)
        paths["recovery"].write_bytes(bytes(RECOVERY_BYTES)); paths["p1"].write_bytes(old_p1)
        paths["package"].write_bytes(package)
        with paths["disk"].open("r+b") as stream:
            stream.seek(REQUEST_OFFSET)
            stream.write(request_bytes(package, old_p1, old_kernel, new_p1, new_kernel,
                                       len(prefix), len(packed)))
        run(command, 10); run(command, 0)
        run([engine, "trial-good", str(paths["recovery"]), str(paths["p1"]),
             str(paths["disk"])], 0)
        run(r_command, 20)
        assert paths["p1"].read_bytes() == new_p1
    print("GKDSU_COORDINATOR_TEST=PASS apply=1 trial=1 rollback=1 finalize=1 journal_copies_cleared=2")

if __name__ == "__main__": main()
