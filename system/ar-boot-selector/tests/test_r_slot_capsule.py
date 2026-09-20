import gzip
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
SCRIPTS = ROOT / "system" / "rc33-system-update" / "scripts"
BUILD = SCRIPTS / "build-kernel-slot.py"
VERIFY = SCRIPTS / "verify-kernel-slot.py"
sys.path.insert(0, str(SCRIPTS))

from kernel_slot_common import HEADER, SLOT_BYTES, parse_capsule_contract


class RecoverySlotCapsuleTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.source_slot = self.root / "source-slot.bin"
        source_header = HEADER.pack(
            0x27051956,
            0,
            1785448800,
            1,
            0x80100000,
            0x80730BD0,
            0,
            5,
            5,
            2,
            1,
            b"source".ljust(32, b"\0"),
        )
        self.source_slot.write_bytes(source_header + bytes(SLOT_BYTES - len(source_header)))
        self.kernel = self.root / "kernel"
        self.kernel.mkdir()
        self.raw = (b"dedicated-r-kernel" * 4096) + b"end"
        (self.kernel / "vmlinux.bin").write_bytes(self.raw)
        (self.kernel / "vmlinux.bin.gz").write_bytes(gzip.compress(self.raw, mtime=0))
        (self.kernel / "readelf-header.txt").write_text(
            "  Entry point address:               0x80730bd0\n", encoding="ascii"
        )
        self.companion = self.root / "recovery-capsule.bin"
        self.companion.write_bytes(b"hsqs" + bytes(range(256)) * 32)
        self.header = self.root / "round84-recovery-capsule.generated.h"
        self._write_header(0x6A0000, self.companion.read_bytes())

    def tearDown(self):
        self.temporary.cleanup()

    def _write_header(self, offset, capsule):
        digest = hashlib.sha256(capsule).digest()
        octets = ",".join(f"0x{value:02x}" for value in digest)
        self.header.write_text(
            "#define R84_RECOVERY_CAPSULE_OFFSET ((off_t)0x%x)\n"
            "#define R84_RECOVERY_CAPSULE_BYTES 0x%xUL\n"
            "static const unsigned char r84_recovery_capsule_sha256[32] =\n"
            "\t{%s};\n" % (offset, len(capsule), octets),
            encoding="ascii",
        )

    def _build(self, output, companion=True):
        command = [
            "python3",
            str(BUILD),
            "--source-slot",
            str(self.source_slot),
            "--kernel-build",
            str(self.kernel),
            "--name",
            "gkd-dedicated-recovery-r",
            "--output",
            str(output),
        ]
        if companion:
            command.extend(
                [
                    "--companion",
                    str(self.companion),
                    "--companion-header",
                    str(self.header),
                    "--slot-offset",
                    "0x300000",
                ]
            )
        return subprocess.run(command, text=True, capture_output=True)

    def _verify(self, slot, companion=True):
        command = [
            "python3",
            str(VERIFY),
            "--slot",
            str(slot),
            "--raw",
            str(self.kernel / "vmlinux.bin"),
            "--name",
            "gkd-dedicated-recovery-r",
        ]
        if companion:
            command.extend(
                [
                    "--companion",
                    str(self.companion),
                    "--companion-header",
                    str(self.header),
                    "--slot-offset",
                    "0x300000",
                ]
            )
        return subprocess.run(command, text=True, capture_output=True)

    def test_companion_is_bound_and_embedded_at_generated_offset(self):
        output = self.root / "r-slot.bin"
        built = self._build(output)
        self.assertEqual(built.returncode, 0, built.stderr)
        slot = output.read_bytes()
        relative_offset = 0x6A0000 - 0x300000
        companion = self.companion.read_bytes()
        self.assertEqual(slot[relative_offset:relative_offset + len(companion)], companion)
        self.assertFalse(any(slot[relative_offset + len(companion):]))
        verified = self._verify(output)
        self.assertEqual(verified.returncode, 0, verified.stderr)
        self.assertIn("companion=0x3a0000/", verified.stdout)

    def test_builder_rejects_companion_not_bound_by_header(self):
        self.companion.write_bytes(self.companion.read_bytes() + b"drift")
        built = self._build(self.root / "rejected-slot.bin")
        self.assertNotEqual(built.returncode, 0)
        self.assertIn("companion-integrity", built.stderr)

    def test_verifier_rejects_embedded_companion_damage(self):
        output = self.root / "r-slot.bin"
        self.assertEqual(self._build(output).returncode, 0)
        damaged = bytearray(output.read_bytes())
        damaged[0x3A0000 + 7] ^= 1
        output.write_bytes(damaged)
        verified = self._verify(output)
        self.assertNotEqual(verified.returncode, 0)
        self.assertIn("companion-integrity", verified.stderr)

    def test_legacy_zero_tail_contract_remains_supported(self):
        output = self.root / "legacy-slot.bin"
        built = self._build(output, companion=False)
        self.assertEqual(built.returncode, 0, built.stderr)
        verified = self._verify(output, companion=False)
        self.assertEqual(verified.returncode, 0, verified.stderr)
        self.assertIn("companion=none", verified.stdout)

    def test_contract_rejects_unaligned_offset(self):
        self._write_header(0x6A1000, self.companion.read_bytes())
        with self.assertRaisesRegex(ValueError, "capsule-layout"):
            parse_capsule_contract(self.header, 0x300000)


if __name__ == "__main__":
    unittest.main()
