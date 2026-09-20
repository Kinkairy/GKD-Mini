#!/usr/bin/env python3
"""Fresh A defaults remain representable without changing shared R defaults."""
from pathlib import Path
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[3]

class ApplicationDefaults(unittest.TestCase):
    def test_fresh_defaults_and_shared_schema_isolation(self):
        source = PROJECT / "system/config-core/schema/gdkmini.schema"
        original = source.read_bytes()
        with tempfile.TemporaryDirectory(prefix="gkd-schema-", dir="/tmp/gkd-mini-public") as work:
            target = Path(work) / "a.schema"
            subprocess.run(["python3", str(PROJECT / "system/application-core/scripts/derive-schema.py"),
                            str(source), str(target)], check=True)
            def rows(data):
                return {p[0]: p for line in data.splitlines()
                        if len(p := line.split("|")) == 8 and not line.startswith("#")}
            shared, derived = rows(original.decode()), rows(target.read_text())
            expected = {"ui_dynamic_effects": "enabled", "auto_suspend_timeout_seconds": "600",
                        "ui_show_fps": "disabled", "ui_language": "en"}
            self.assertEqual({k: derived[k][2] for k in expected}, expected)
            self.assertEqual(set(shared) - set(derived),
                             {"battery_voltage_fallback", "battery_voltage_thresholds_mv"})
            for key in derived:
                if key != "ui_language":
                    self.assertEqual(derived[key], shared[key], key)
            self.assertEqual(source.read_bytes(), original)
            self.assertEqual(shared["ui_language"][2], "zh")

if __name__ == "__main__":
    unittest.main()
