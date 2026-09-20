#!/usr/bin/env python3
"""Exercise the manual preview launcher against isolated command stubs."""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


LANE = Path(__file__).resolve().parents[1]
LAUNCHER = LANE / "device/gkd-ui-menu-preview"


class MenuPreviewTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="gkd-ui-menu-preview-")
        self.root = Path(self.temporary.name)
        self.launcher = self.root / "gkd-ui-menu-preview"
        self.probe = self.root / "gkd-ui-menu-probe"
        self.config = self.root / "gkd-config"
        self.arguments = self.root / "arguments"
        self.get_arguments = self.root / "get-arguments"
        shutil.copyfile(LAUNCHER, self.launcher)
        self.launcher.chmod(0o555)
        self.probe.write_text(
            "#!/bin/sh\nprintf '%s\\n' \"$@\" >\"$GKD_PREVIEW_ARGUMENTS\"\n",
            encoding="utf-8",
        )
        self.probe.chmod(0o555)
        self.config.write_text(
            "#!/bin/sh\nprintf '%s\\n' \"$@\" >>\"$GKD_PREVIEW_GET_ARGUMENTS\"\n"
            "[ \"${GKD_PREVIEW_GET_FAIL:-0}\" = 0 ] || exit 1\n"
            "printf '%s\\n' \"$GKD_PREVIEW_VALUE\"\n",
            encoding="utf-8",
        )
        self.config.chmod(0o555)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def launch(self, value: str = "enabled", failed: bool = False) -> subprocess.CompletedProcess[str]:
        self.arguments.unlink(missing_ok=True)
        self.get_arguments.unlink(missing_ok=True)
        environment = os.environ | {
            "GKD_UI_MENU_PREVIEW_CONFIG_GET": str(self.config),
            "GKD_PREVIEW_ARGUMENTS": str(self.arguments),
            "GKD_PREVIEW_GET_ARGUMENTS": str(self.get_arguments),
            "GKD_PREVIEW_VALUE": value,
            "GKD_PREVIEW_GET_FAIL": "1" if failed else "0",
            # Target initramfs has no dirname applet link or general host tools.
            "PATH": str(self.root / "empty-path"),
        }
        result = subprocess.run(
            [str(self.launcher), "font.psf", "USB", "20"], env=environment,
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        self.assertEqual(["get", "ui_dynamic_effects"], self.get_arguments.read_text().splitlines())
        return result

    def test_enabled_and_disabled_are_forwarded_exactly(self) -> None:
        for value in ("enabled", "disabled"):
            with self.subTest(value=value):
                self.assertEqual(0, self.launch(value).returncode)
                self.assertEqual(
                    ["font.psf", "USB", "20", value], self.arguments.read_text().splitlines()
                )

    def test_missing_or_invalid_value_never_starts_probe(self) -> None:
        for value in ("", "slide", "enabled "):
            with self.subTest(value=repr(value)):
                self.assertNotEqual(0, self.launch(value).returncode)
                self.assertFalse(self.arguments.exists())

    def test_getter_failure_never_starts_probe(self) -> None:
        self.assertNotEqual(0, self.launch(failed=True).returncode)
        self.assertFalse(self.arguments.exists())


if __name__ == "__main__":
    unittest.main()
