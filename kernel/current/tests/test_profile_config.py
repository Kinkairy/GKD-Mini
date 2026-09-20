#!/usr/bin/env python3
"""Native regression tests for the bounded kernel profile configuration delta."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


CURRENT = Path(__file__).resolve().parents[1]
SCRIPT = CURRENT / "scripts" / "configure-profile.py"
BASE_PATH = CURRENT / "config" / "rc34.config"
SPEC = importlib.util.spec_from_file_location("configure_profile", SCRIPT)
PROFILE = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
SPEC.loader.exec_module(PROFILE)


class ProfileConfigTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.base = BASE_PATH.read_text()
        cls.base_symbols = PROFILE.symbols(cls.base)

    def replace_once(self, text, old, new):
        self.assertEqual(text.count(old), 1, old)
        return text.replace(old, new)

    def test_normal_is_byte_identical(self):
        self.assertEqual(PROFILE.derive(self.base, "normal"), self.base)

    def test_dedicated_recovery_is_byte_identical(self):
        self.assertEqual(PROFILE.derive(self.base, "dedicated-recovery"), self.base)

    def test_application_minimal_adds_only_required_ui_power_usb_features(self):
        actual = PROFILE.derive(self.base, "application-minimal")
        before, after = PROFILE.symbols(self.base), PROFILE.symbols(actual)
        changed = {key: after[key] for key in before if before[key] != after[key]}
        self.assertEqual(changed, {
            "CONFIG_POWER_SUPPLY": "y",
            "CONFIG_USB_CONFIGFS_ACM": "y",
            "CONFIG_HW_RANDOM": "y",
            "CONFIG_HW_RANDOM_INGENIC_TRNG": "y",
            "CONFIG_CMDLINE": json.dumps(json.loads(before["CONFIG_CMDLINE"]) + " " + PROFILE.QUALITY),
        })
        self.assertEqual(after["CONFIG_FB_X1830_USER_PLANE"], "y")
        self.assertNotIn("CONFIG_FB_X1830_USER_PLANE", before)
        self.assertEqual(after["CONFIG_POWER_SUPPLY_HWMON"], "n")
        self.assertEqual(after["CONFIG_USB_U_SERIAL"], "y")
        self.assertEqual(after["CONFIG_USB_F_ACM"], "y")
        self.assertEqual(set(after)-set(before), {"CONFIG_FB_X1830_USER_PLANE",
            "CONFIG_POWER_SUPPLY_HWMON", "CONFIG_USB_U_SERIAL", "CONFIG_USB_F_ACM"})
        self.assertEqual(actual.count(PROFILE.QUALITY), 1)

    def test_verify_rejects_changed_expected_symbol(self):
        actual = self.replace_once(PROFILE.derive(self.base, "application-minimal"),
                                   "CONFIG_HW_RANDOM=y", "CONFIG_HW_RANDOM=m")
        with self.assertRaisesRegex(ValueError, "CONFIG_HW_RANDOM"):
            PROFILE.verify(self.base, actual, "application-minimal")

    def test_verify_rejects_added_symbol(self):
        actual = self.base + "CONFIG_GKD_UNEXPECTED=y\n"
        with self.assertRaisesRegex(ValueError, "CONFIG_GKD_UNEXPECTED"):
            PROFILE.verify(self.base, actual, "normal")

    def test_verify_rejects_deleted_symbol(self):
        line = "CONFIG_HW_RANDOM=m\n"
        actual = self.replace_once(self.base, line, "")
        with self.assertRaisesRegex(ValueError, "CONFIG_HW_RANDOM"):
            PROFILE.verify(self.base, actual, "normal")

    def test_normal_and_dedicated_reject_deleted_explicit_disabled_symbol(self):
        line = "# CONFIG_POWER_SUPPLY is not set\n"
        actual = self.replace_once(self.base, line, "")
        for mode in ("normal", "dedicated-recovery"):
            with self.subTest(mode=mode), \
                    self.assertRaisesRegex(ValueError, "CONFIG_POWER_SUPPLY"):
                PROFILE.verify(self.base, actual, mode)

    def test_application_accepts_only_observed_dependency_defaults(self):
        actual = PROFILE.derive(self.base, "application-minimal")
        for key in PROFILE.APPLICATION_DEPENDENCY_DEFAULTS:
            actual += "# " + key + " is not set\n"
        PROFILE.verify(self.base, actual, "application-minimal")

        unexpected = actual + "# CONFIG_GKD_UNEXPECTED is not set\n"
        with self.assertRaisesRegex(ValueError, "CONFIG_GKD_UNEXPECTED"):
            PROFILE.verify(self.base, unexpected, "application-minimal")

    def test_application_rejects_deleted_explicit_disabled_symbol(self):
        actual = PROFILE.derive(self.base, "application-minimal")
        actual = self.replace_once(
            actual, "# CONFIG_FRAMEBUFFER_CONSOLE is not set\n", "")
        with self.assertRaisesRegex(ValueError, "CONFIG_FRAMEBUFFER_CONSOLE"):
            PROFILE.verify(self.base, actual, "application-minimal")

    def test_symbols_rejects_duplicate_symbol(self):
        with self.assertRaisesRegex(ValueError, "duplicate configuration symbol: CONFIG_HW_RANDOM"):
            PROFILE.symbols(self.base + "CONFIG_HW_RANDOM=m\n")

    def test_derive_rejects_unknown_mode(self):
        with self.assertRaisesRegex(ValueError, "unknown kernel profile"):
            PROFILE.derive(self.base, "unsupported")

    def test_derive_rejects_base_drift_and_duplicate_quality(self):
        drifted = self.replace_once(self.base, "CONFIG_HW_RANDOM=m", "CONFIG_HW_RANDOM=y")
        with self.assertRaisesRegex(ValueError, "accepted base drift: CONFIG_HW_RANDOM"):
            PROFILE.derive(drifted, "application-minimal")
        command_line = self.base_symbols["CONFIG_CMDLINE"]
        quality_twice = self.replace_once(
            self.base, "CONFIG_CMDLINE=" + command_line,
            "CONFIG_CMDLINE=" + json.dumps(json.loads(command_line) + " " + PROFILE.QUALITY))
        with self.assertRaisesRegex(ValueError, "accepted base command line drift"):
            PROFILE.derive(quality_twice, "application-minimal")

    def test_cli_rejects_shared_base_and_config_without_writing(self):
        with tempfile.TemporaryDirectory() as temporary:
            config = Path(temporary) / "config"
            config.write_text(self.base)
            result = subprocess.run(
                [sys.executable, str(SCRIPT), "apply", "--mode", "application-minimal",
                 "--base", str(config), "--config", str(config)],
                text=True, capture_output=True, check=False)
            self.assertEqual(result.returncode, 2)
            self.assertIn("base must be a separate immutable configuration snapshot", result.stderr)
            self.assertEqual(config.read_text(), self.base)


if __name__ == "__main__":
    unittest.main()
