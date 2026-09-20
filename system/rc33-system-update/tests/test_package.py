#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import tempfile
import os
from pathlib import Path

LANE = Path(__file__).resolve().parents[1]
TOOLS = LANE / "tools"
P1_BYTES = 805306880
KERNEL_BYTES = 6291456


def stale_journal() -> bytes:
    raw = bytearray(4096)
    raw[:8] = b"GKDJNL1\0"
    raw[8:12] = (1).to_bytes(4, "little")
    raw[12:16] = (4096).to_bytes(4, "little")
    raw[16:24] = (9).to_bytes(8, "little")
    raw[24:28] = (9).to_bytes(4, "little")
    import zlib
    raw[4092:4096] = zlib.crc32(raw[:4092]).to_bytes(4, "little")
    return bytes(raw)


def sparse(path: Path, size: int, marker: bytes) -> None:
    with path.open("wb") as stream:
        stream.truncate(size)
        stream.seek(4096)
        stream.write(marker)
        stream.seek(size - len(marker))
        stream.write(marker)


with tempfile.TemporaryDirectory(prefix="gkdsu-test-") as directory:
    root = Path(directory)
    source_p1, target_p1 = root / "source-p1", root / "target-p1"
    source_kernel, target_kernel = root / "source-kernel", root / "target-kernel"
    sparse(source_p1, P1_BYTES, b"SOURCE-P1")
    sparse(target_p1, P1_BYTES, b"TARGET-P1")
    sparse(source_kernel, KERNEL_BYTES, b"SOURCE-KERNEL")
    sparse(target_kernel, KERNEL_BYTES, b"TARGET-KERNEL")
    mbr = root / "mbr"
    mbr.write_bytes(bytes(range(256)) * 2)
    private, public = root / "private.pem", root / "public.pem"
    subprocess.run(["openssl", "genrsa", "-out", str(private), "2048"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["openssl", "rsa", "-in", str(private), "-pubout", "-out", str(public)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    package = root / "system.gkdupdate"
    command = ["python3", str(TOOLS / "build-package.py"),
               "--source-p1", str(source_p1), "--target-p1", str(target_p1),
               "--source-kernel", str(source_kernel), "--target-kernel", str(target_kernel),
               "--mbr", str(mbr), "--from-version", "3.2.1", "--to-version", "3.3.0",
               "--source-runtime-id", "1" * 64, "--target-runtime-id", "2" * 64,
               "--private-key", str(private), "--public-key", str(public),
               "--output", str(package)]
    subprocess.run(command, check=True)
    subprocess.run(["python3", str(TOOLS / "verify-package.py"), str(package), str(public)],
                   check=True)
    prepare = os.environ.get("GKDSU_PREPARE_BINARY")
    if prepare:
        request_disk = root / "request-disk"
        recovery = root / "recovery"
        with request_disk.open("wb") as stream:
            stream.truncate(31_457_280)
        with recovery.open("wb") as stream:
            stream.truncate(1_073_741_824)
        prepared = [prepare, str(package), str(public), str(request_disk), "1" * 64,
                    str(recovery)]
        import hashlib
        package_hash = hashlib.sha256(package.read_bytes()).hexdigest()
        before = hashlib.sha256(request_disk.read_bytes()).hexdigest()
        preview = subprocess.check_output(prepared + ["--inspect"], text=True)
        assert preview == f"GKDSU_INSPECT=PASS package={package_hash} from=3.2.1 to=3.3.0\n"
        assert hashlib.sha256(request_disk.read_bytes()).hexdigest() == before
        bad = subprocess.run(prepared + ["--confirmed", "0" * 64], capture_output=True)
        assert bad.returncode and b"confirmation-changed" in bad.stderr
        assert hashlib.sha256(request_disk.read_bytes()).hexdigest() == before
        subprocess.run(prepared + ["--confirmed", package_hash], check=True)
        subprocess.run(prepared, check=True)
        blocked_disk = root / "blocked-request-disk"
        with blocked_disk.open("wb") as stream:
            stream.truncate(31_457_280)
        with recovery.open("r+b") as stream:
            stream.seek(4096)
            stream.write(stale_journal())
        blocked = [prepare, str(package), str(public), str(blocked_disk), "1" * 64,
                   str(recovery)]
        assert subprocess.run(blocked, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE).returncode != 0
        with blocked_disk.open("rb") as stream:
            stream.seek(20_967_424)
            assert stream.read(4096) == bytes(4096)
        with recovery.open("r+b") as stream:
            stream.seek(4096)
            stream.write(bytes(4096))
        r_prepare = os.environ.get("GKDSU_PREPARE_R_BINARY")
        if r_prepare:
            # R is immutable across A releases. A signed actual source kernel
            # replaces a stale R runtime-id, while all package gates remain.
            with blocked_disk.open("r+b") as stream:
                stream.seek(9437184)
                stream.write(source_kernel.read_bytes())
            r_command = [r_prepare, str(package), str(public), str(blocked_disk),
                         "--source-kernel", str(recovery)]
            subprocess.run(r_command, check=True)
            with blocked_disk.open("r+b") as stream:
                stream.seek(20_967_424)
                assert stream.read(8) == b"GKDRQ1\0\0"
                stream.seek(20_967_424)
                stream.write(bytes(4096))
                stream.seek(9437184)
                stream.write(b"damaged-kernel")
            rejected = subprocess.run(r_command, capture_output=True)
            assert rejected.returncode == 2 and b"reason=source-kernel" in rejected.stderr
            with blocked_disk.open("rb") as stream:
                stream.seek(20_967_424)
                assert stream.read(4096) == bytes(4096)
            legacy_arg = r_command.copy()
            legacy_arg[4] = "1" * 64
            assert subprocess.run(legacy_arg, capture_output=True).returncode == 2
    tampered = root / "tampered-payload.gkdupdate"
    data = bytearray(package.read_bytes())
    data[-1] ^= 1
    tampered.write_bytes(data)
    result = subprocess.run(["python3", str(TOOLS / "verify-package.py"),
                             str(tampered), str(public)], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    assert result.returncode != 0
    if prepare:
        tampered_disk = root / "tampered-request-disk"
        with tampered_disk.open("wb") as stream:
            stream.truncate(31_457_280)
        assert subprocess.run([prepare, str(tampered), str(public), str(tampered_disk),
                               "1" * 64, str(recovery)], stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE).returncode != 0
    bad_signature = root / "tampered-signature.gkdupdate"
    data = bytearray(package.read_bytes())
    manifest_size = int.from_bytes(data[8:12], "little")
    data[12 + manifest_size] ^= 1
    bad_signature.write_bytes(data)
    result = subprocess.run(["python3", str(TOOLS / "verify-package.py"),
                             str(bad_signature), str(public)], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    assert result.returncode != 0
    if prepare and os.environ.get("GKDSU_PREPARE_R_BINARY"):
        signed_command = [os.environ["GKDSU_PREPARE_R_BINARY"], str(bad_signature),
                          str(public), str(blocked_disk), "--source-kernel", str(recovery)]
        rejected = subprocess.run(signed_command, capture_output=True)
        assert rejected.returncode == 2 and b"reason=signature" in rejected.stderr
    downgrade = command.copy()
    downgrade[downgrade.index("--from-version") + 1] = "3.3.0"
    downgrade[downgrade.index("--to-version") + 1] = "3.2.1"
    downgrade[downgrade.index("--output") + 1] = str(root / "downgrade.gkdupdate")
    result = subprocess.run(downgrade, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert result.returncode != 0
print("GKDSU_PACKAGE_TEST=PASS signed=1 target_prepare=" +
      ("1" if os.environ.get("GKDSU_PREPARE_BINARY") else "0") +
      " payload_tamper_rejected=1 signature_tamper_rejected=1 downgrade_rejected=1 stale_journal_rejected=1")
