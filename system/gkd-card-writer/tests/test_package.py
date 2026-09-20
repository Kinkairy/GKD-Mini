from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
TOOL = ROOT / "tools"
TEST_KEY = Path("/opt/gkd-build/private-state/gkd-card-writer-dev/test-rsa-private.pem")


class PackageTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="gkd-writer-package-")
        self.root = Path(self.temporary.name)
        self.public_key = self.root / "test-rsa-public.pem"
        subprocess.run(
            ["openssl", "pkey", "-in", str(TEST_KEY), "-pubout", "-out", str(self.public_key)],
            check=True,
            capture_output=True,
            text=True,
        )
        self.a = self.root / "synthetic-a.gkdupdate"
        self.b = self.root / "synthetic-b.gkdupdate"
        for package, disk in ((self.a, self.root / "disk-a.img"),
                              (self.b, self.root / "disk-b.img")):
            subprocess.run(
                ["python3", str(TOOL / "build_synthetic.py"),
                 "--output", str(package), "--disk", str(disk), "--key", str(TEST_KEY)],
                check=True,
                capture_output=True,
                text=True,
            )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def verify(self, package: Path, ok: bool = True) -> None:
        result = subprocess.run(
            ["python3", str(TOOL / "verify_package.py"),
             str(package), str(self.public_key)],
            capture_output=True,
            text=True,
        )
        self.assertEqual(0 if ok else 1, result.returncode, result.stderr + result.stdout)

    def test_ab_and_verify(self) -> None:
        self.verify(self.a)
        self.verify(self.b)
        self.assertEqual(self.a.read_bytes(), self.b.read_bytes())

    def test_tamper_is_rejected(self) -> None:
        data = bytearray(self.a.read_bytes())
        data[-1] ^= 1
        self.a.write_bytes(data)
        self.verify(self.a, False)

    def test_signature_and_casefold(self) -> None:
        data = bytearray(self.a.read_bytes())
        data[12] ^= 1
        self.a.write_bytes(data)
        self.verify(self.a, False)
        copy = self.root / "Case.GKDUPDATE"
        shutil.copyfile(self.b, copy)
        self.verify(copy)


if __name__ == "__main__":
    unittest.main()
