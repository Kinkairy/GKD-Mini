#!/usr/bin/env python3
"""Real config compiler versus real A reader; no device access. Root-owned fixtures required."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
APP = ROOT / "system/application-core"
CORE = ROOT / "system/config-core"

@unittest.skipUnless(os.geteuid() == 0, "run native reader fixtures inside the isolated root test container")
class SettingsContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workspace = tempfile.TemporaryDirectory(prefix="gkd-settings-contract-")
        cls.work = Path(cls.workspace.name)
        cls.schema = cls.work / "a.schema"
        cls.shared = (CORE / "schema/gdkmini.schema").read_bytes()
        subprocess.run(["python3", str(APP / "scripts/derive-schema.py"), str(CORE / "schema/gdkmini.schema"), str(cls.schema)], check=True, capture_output=True)
        probe = cls.work / "probe.c"
        probe.write_text('#include "gkd-app-settings.h"\nint main(int n,char **v){struct gkd_app_settings s;return n==2?!!gkd_app_settings_load(v[1],&s):2;}\n')
        cls.reader = cls.work / "reader"
        subprocess.run(["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-I"+str(APP / "include"),
            "-I"+str(ROOT / "system/ui-core/include"), str(APP / "source/gkd-app-settings.c"), str(probe), "-o", str(cls.reader)], check=True)

    @classmethod
    def tearDownClass(cls): cls.workspace.cleanup()

    def check(self, override, accepted, shared=False):
        with tempfile.TemporaryDirectory(dir=self.work) as directory:
            work = Path(directory); proposal = work / "override.conf"; proposal.write_text(override)
            schema = CORE / "schema/gdkmini.schema" if shared else self.schema
            env = dict(os.environ, GKD_CONFIG_SCHEMA=str(schema), GKD_CONFIG_MERGER=str(CORE / "device/gkd-config-merge.awk"),
                GKD_CONFIG_OVERRIDE=str(proposal), GKD_CONFIG_RUN_DIR=str(work / "run"), GKD_CONFIG_STATE_DIR=str(work / "state"))
            result = subprocess.run(["sh", str(CORE / "device/gkd-config"), "validate"], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode == 0, accepted, result.stderr)
            if not shared:
                merged = subprocess.run(["awk", "-v", "schema_file="+str(schema), "-v", "override_file="+str(proposal),
                    "-f", str(CORE / "device/gkd-config-merge.awk"), str(schema), str(proposal)], capture_output=True, check=True)
                candidate = work / "effective.conf"; candidate.write_bytes(merged.stdout); candidate.chmod(0o600)
                loaded = subprocess.run([str(self.reader), str(candidate)])
                self.assertEqual(loaded.returncode == 0, accepted, override)
            self.assertFalse((work / "run/current").exists())
            self.assertEqual((CORE / "schema/gdkmini.schema").read_bytes(), self.shared)

    def test_defaults_and_hotkeys(self):
        self.check("", True)
        for value in ("NONE", "MENU+L1", "L1+L2", "L1+R1", "L1+R2", "R1+L2", "R1+R2", "L2+R2", "MENU+R1", "", "NONE,MENU+L1", "NONE,"):
            with self.subTest(hotkey=value): self.check("screenshot_hotkey="+value+"\n", value in ("NONE", "MENU+L1"))

    def test_path_boundaries(self):
        root = "/media/sdcard/"
        cases = [(root+"screenshots", True), (root+"folder/screenshots/", True), (root+".shots", True),
            (root+"截图", True), (root, False), ("/tmp/screenshots", False), (root+"../screenshots", False),
            (root+"./shots", False), (root+"a/..", False), (root+"a/.", False), (root+"a//b", False),
            (root+"a\x01b", False), (root+"a\x7fb", False), (root+"x"*(255-len(root)), True),
            (root+"x"*(256-len(root)), False), (root+"图"*81, False)]
        for value, accepted in cases:
            with self.subTest(path=value): self.check("screenshot_output_dir="+value+"\n", accepted)

    def test_key_uniqueness(self):
        for key in ("input_map_a", "input_map_menu", "input_map_dpad_left", "input_map_dpad_right"):
            with self.subTest(key=key): self.check(key+"=KEY_UP\n", False)

    def test_recovery_profile_is_unchanged(self):
        self.check("screenshot_hotkey=L1+L2\nscreenshot_output_dir=/tmp/screenshots\n", True, shared=True)

if __name__ == "__main__": unittest.main()
