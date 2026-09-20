#!/usr/bin/env python3
from __future__ import annotations

import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import unittest


LANE = Path(__file__).resolve().parents[1]
PROJECT = LANE.parents[1]
SCHEMA = LANE / "schema/gdkmini.schema"
MANAGER = LANE / "device/gkd-config"
MERGER = LANE / "device/gkd-config-merge.awk"
ACCEPTED = LANE / "tests/fixtures/compatibility-gdkmini.conf"
LAYOUT = PROJECT / "system/rc33-ui-status-layout/source/gdkmini.conf.fragment"
UI_DEFAULTS = PROJECT / "system/ui-core/config/gdkmini-ui.defaults.conf"
R_PROFILE = LANE / "profiles/dedicated-r.override.conf"


def schema_rows() -> list[list[str]]:
    return [
        line.split("|")
        for line in SCHEMA.read_text(encoding="utf-8").splitlines()
        if line and not line.startswith("#")
    ]


def config_values(*paths: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for path in paths:
        for line in path.read_text(encoding="utf-8").splitlines():
            if line and not line.startswith("#") and "=" in line:
                key, value = line.split("=", 1)
                if key in result:
                    raise AssertionError(f"duplicate accepted key: {key}")
                result[key] = value
    return result


def clear_tree(path: Path) -> None:
    if not path.exists():
        return
    for item in path.rglob("*"):
        if item.is_dir():
            item.chmod(0o755)
    path.chmod(0o755)
    shutil.rmtree(path)


class ConfigCoreTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="gkd-config-core-")
        self.root = Path(self.temporary.name)
        self.override = self.root / "override.conf"
        self.override.write_text("", encoding="utf-8")
        self.run = self.root / "run"
        self.state = self.root / "state"
        self.env = os.environ.copy()
        self.env.update(
            {
                "GKD_CONFIG_SCHEMA": str(SCHEMA),
                "GKD_CONFIG_MERGER": str(MERGER),
                "GKD_CONFIG_OVERRIDE": str(self.override),
                "GKD_CONFIG_RUN_DIR": str(self.run),
                "GKD_CONFIG_STATE_DIR": str(self.state),
            }
        )

    def tearDown(self) -> None:
        clear_tree(self.run)
        clear_tree(self.state)
        self.temporary.cleanup()

    def run_manager(
        self, *arguments: str, failpoint: str | None = None
    ) -> subprocess.CompletedProcess[str]:
        environment = self.env.copy()
        if failpoint is not None:
            environment["GKD_CONFIG_FAILPOINT"] = failpoint
        return subprocess.run(
            ["sh", str(MANAGER), *arguments],
            env=environment,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def current_target(self) -> str:
        return os.readlink(self.run / "current")

    def test_schema_is_the_single_complete_149_key_contract(self) -> None:
        rows = schema_rows()
        self.assertEqual(149, len(rows))
        self.assertTrue(all(len(row) == 8 for row in rows))
        self.assertEqual(149, len({row[0] for row in rows}))
        self.assertEqual(
            {"immediate", "keys-released", "network-rebuild", "next-entry",
             "transaction-idle"},
            {row[7] for row in rows},
        )
        self.assertTrue(all(row[6] and row[7] for row in rows))

    def test_schema_defaults_preserve_rc34_and_add_ui_core(self) -> None:
        rc34 = config_values(ACCEPTED, LAYOUT)
        # These two values were advanced after the older source file and were
        # read back from the accepted RC3.4 recovery overlay.
        rc34["screenshot_output_dir"] = "/media/sdcard/screenshots"
        rc34["screenshot_osd_timeout_ms"] = "3000"
        ui = config_values(UI_DEFAULTS)
        expected = {**rc34, **ui}
        expected.update({
            "accent": "#D6B85A", "menu_content_shift": "12", "menu_text_shift": "1",
            "accent_x": "16", "accent_width": "132", "accent_height": "16",
            "accent_period": "20", "accent_stroke": "11", "accent_alpha": "156",
            "accent_dim_alpha": "124", "accent_rail_alpha": "65",
            "action_disabled_alpha": "72",
            "ui_show_fps": "disabled", "input_style": "raw",
        })
        actual = {row[0]: row[2] for row in schema_rows()}
        self.assertEqual(99, len(rc34))
        self.assertEqual(36, len(ui))
        self.assertEqual(149, len(expected))
        self.assertEqual(expected, actual)

    def test_ui_dynamic_effects_default_and_enum_rejection(self) -> None:
        self.assertEqual("enabled", {row[0]: row[2] for row in schema_rows()}["ui_dynamic_effects"])
        self.override.write_text("ui_dynamic_effects=disabled\n", encoding="utf-8")
        self.assertEqual(0, self.run_manager("apply").returncode)
        self.assertEqual("disabled", self.run_manager("get", "ui_dynamic_effects").stdout.strip())
        self.override.write_text("ui_dynamic_effects=slide\n", encoding="utf-8")
        rejected = self.run_manager("apply")
        self.assertEqual(2, rejected.returncode)
        self.assertIn("invalid_value key=ui_dynamic_effects", rejected.stderr)

    def test_input_style_defaults_switch_and_reject_without_generation_change(self):
        self.assertEqual("raw", {row[0]: row[2] for row in schema_rows()}["input_style"])
        for style in ("raw", "xbox", "ps"):
            self.override.write_text("input_style="+style+"\n")
            result=self.run_manager("apply")
            self.assertEqual(0,result.returncode,result.stderr)
            self.assertEqual(style,self.run_manager("get","input_style").stdout.strip())
        before=self.current_target()
        self.override.write_text("input_style=invalid\n")
        self.assertEqual(2,self.run_manager("apply").returncode)
        self.assertEqual(before,self.current_target())

    def test_partial_override_creates_an_immutable_atomic_generation(self) -> None:
        self.override.write_text(
            "auto_suspend_timeout_seconds=0\nui_status_text_x=40\n",
            encoding="utf-8",
        )
        result = self.run_manager("apply")
        self.assertEqual(0, result.returncode, result.stderr)
        target = self.current_target()
        self.assertRegex(target, r"^generations/[0-9a-f]{64}$")
        generation = self.run / target
        effective = (generation / "effective.conf").read_text(encoding="utf-8")
        self.assertEqual(149, len(effective.splitlines()))
        self.assertIn("auto_suspend_timeout_seconds=0\n", effective)
        self.assertIn("ui_status_text_x=40\n", effective)
        self.assertIn("volume_step=2\n", effective)
        self.assertEqual(
            stat.S_IMODE((generation / "effective.conf").stat().st_mode), 0o444
        )
        self.assertEqual(stat.S_IMODE(generation.stat().st_mode), 0o555)
        self.assertEqual(target, os.readlink(self.state / "current"))

    def test_dedicated_r_profile_uses_the_same_complete_schema(self) -> None:
        profile_values = config_values(R_PROFILE)
        self.assertEqual(
            {
                "screenshot_output_dir": "/media/gkd-r-screenshots",
                "screenshot_osd_timeout_ms": "0",
                "screenshot_hotkey": "NONE",
            },
            profile_values,
        )
        self.env["GKD_CONFIG_OVERRIDE"] = str(R_PROFILE)
        result = self.run_manager("apply")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual("", result.stderr)
        effective = config_values(self.run / "current/effective.conf")
        self.assertEqual(149, len(effective))
        self.assertEqual(profile_values["screenshot_output_dir"], effective["screenshot_output_dir"])
        self.assertEqual(profile_values["screenshot_osd_timeout_ms"], effective["screenshot_osd_timeout_ms"])
        self.assertEqual(profile_values["screenshot_hotkey"], effective["screenshot_hotkey"])
        self.assertEqual("zh", effective["ui_language"])
        self.assertEqual("enabled", effective["ui_dynamic_effects"])
        self.assertEqual("2", effective["volume_step"])

    def test_a_text_overrides_validate_before_publication(self) -> None:
        schema = self.root / "a.schema"
        subprocess.run(["python3", str(PROJECT / "system/application-core/scripts/derive-schema.py"),
                        str(SCHEMA), str(schema)], check=True, capture_output=True)
        self.env["GKD_CONFIG_SCHEMA"] = str(schema)
        result = self.run_manager("apply")
        self.assertEqual(0, result.returncode, result.stderr)
        self.override.write_text("app_text_bat_low_en=LOW POWER\napp_text_bat_low_zh=电量过低\n")
        result = self.run_manager("apply")
        self.assertEqual(0, result.returncode, result.stderr)
        before = self.current_target()
        for invalid in ("app_text_bat_low_en=" + "X"*21,
                        "app_text_yes_en=TOO LONG", "app_text_yes_zh=电量过低",
                        "app_text_bat_low_en=电量低", "app_text_bat_low_zh=测试",
                        "app_text_bat_low_en=BAD\x01TEXT"):
            self.override.write_text(invalid+"\n")
            rejected = self.run_manager("apply")
            self.assertNotEqual(0, rejected.returncode, invalid)
            self.assertEqual(before, self.current_target())

    def test_invalid_candidate_never_replaces_current(self) -> None:
        self.assertEqual(0, self.run_manager("apply").returncode)
        before = self.current_target()
        before_effective = (self.run / "current/effective.conf").read_bytes()
        self.override.write_text("unknown_setting=1\n", encoding="utf-8")
        rejected = self.run_manager("apply")
        self.assertEqual(2, rejected.returncode)
        self.assertIn("unknown_key", rejected.stderr)
        self.assertEqual(before, self.current_target())
        self.assertEqual(before_effective, (self.run / "current/effective.conf").read_bytes())

    def test_prepare_is_non_visible_until_explicit_commit(self) -> None:
        self.assertEqual(0, self.run_manager("apply").returncode)
        before = self.current_target()
        self.override.write_text("volume_step=3\n", encoding="utf-8")
        prepared = self.run_manager("prepare")
        self.assertEqual(0, prepared.returncode, prepared.stderr)
        self.assertEqual(before, self.current_target())
        generation = prepared.stdout.split("generation=", 1)[1].split()[0]
        self.assertTrue((self.run / f"generations/{generation}/effective.conf").is_file())
        committed = self.run_manager("commit", generation)
        self.assertEqual(0, committed.returncode, committed.stderr)
        self.assertNotEqual(before, self.current_target())
        self.assertEqual("3\n", self.run_manager("get", "volume_step").stdout)

    def test_switch_failure_rolls_back_runtime_and_durable_pointer(self) -> None:
        self.assertEqual(0, self.run_manager("apply").returncode)
        run_before = self.current_target()
        state_before = os.readlink(self.state / "current")
        self.override.write_text("volume_step=3\n", encoding="utf-8")
        failed = self.run_manager("apply", failpoint="before_switch")
        self.assertEqual(75, failed.returncode)
        self.assertEqual(run_before, self.current_target())
        self.assertEqual(state_before, os.readlink(self.state / "current"))

    def test_post_runtime_failure_reverses_the_runtime_pointer(self) -> None:
        self.assertEqual(0, self.run_manager("apply").returncode)
        before = self.current_target()
        self.override.write_text("volume_step=3\n", encoding="utf-8")
        failed = self.run_manager("apply", failpoint="after_run_switch")
        self.assertEqual(75, failed.returncode)
        self.assertEqual(before, self.current_target())
        self.assertEqual(before, os.readlink(self.state / "current"))

    def test_last_valid_generation_restores_after_runtime_loss(self) -> None:
        self.override.write_text("auto_suspend_timeout_seconds=0\n", encoding="utf-8")
        self.assertEqual(0, self.run_manager("apply").returncode)
        before = (self.run / "current/effective.conf").read_bytes()
        clear_tree(self.run)
        self.override.write_text("auto_suspend_timeout_seconds=bad\n", encoding="utf-8")
        rejected = self.run_manager("apply")
        self.assertEqual(2, rejected.returncode)
        self.assertEqual(before, (self.run / "current/effective.conf").read_bytes())

    def test_equal_warning_and_suspend_thresholds_are_valid(self) -> None:
        self.override.write_text("battery_low_percent=5\nbattery_critical_percent=5\n", encoding="utf-8")
        result = self.run_manager("apply")
        self.assertEqual(0, result.returncode, result.stderr)
        values = config_values(self.run / "current/effective.conf")
        self.assertEqual("5", values["battery_low_percent"])
        self.assertEqual("5", values["battery_critical_percent"])

    def test_duplicate_invalid_and_cross_field_values_reject(self) -> None:
        cases = (
            "volume_step=2\nvolume_step=3\n",
            "auto_suspend_timeout_seconds=86401\n",
            "battery_low_percent=5\nbattery_critical_percent=6\n",
            "ui_status_icon_x=97\nui_status_icon_width=13\n",
            "input_map_a=NOT_A_KEY\n",
            "usb_internet_interface=interface_name_too_long\n",
            "debug_return_key=KEY_LEFTALT\ndebug_confirm_key=KEY_LEFTALT\n",
            "debug_status_timeout_ms=1000\ndebug_status_refresh_ms=1000\n",
        )
        for content in cases:
            with self.subTest(content=content):
                clear_tree(self.run)
                clear_tree(self.state)
                self.override.write_text(content, encoding="utf-8")
                self.assertEqual(2, self.run_manager("apply").returncode)

    def test_change_receipt_names_owner_and_policy_but_not_values(self) -> None:
        self.assertEqual(0, self.run_manager("apply").returncode)
        self.override.write_text("volume_step=7\n", encoding="utf-8")
        self.assertEqual(0, self.run_manager("apply").returncode)
        changes = (self.run / "current/changes.tsv").read_text(encoding="utf-8")
        self.assertEqual("volume_step\thardware\timmediate\n", changes)
        self.assertNotIn("=7", changes)

    def test_set_and_unset_keep_the_override_sparse(self) -> None:
        queued = self.run_manager("set", "auto_suspend_timeout_seconds", "0")
        self.assertEqual(0, queued.returncode, queued.stderr)
        self.assertEqual(
            "auto_suspend_timeout_seconds=0\n",
            self.override.read_text(encoding="utf-8"),
        )
        self.assertEqual(0, self.run_manager("apply").returncode)
        self.assertEqual("0\n", self.run_manager("get", "auto_suspend_timeout_seconds").stdout)
        self.assertEqual(0, self.run_manager("unset", "auto_suspend_timeout_seconds").returncode)
        self.assertEqual("", self.override.read_text(encoding="utf-8"))
        self.assertEqual(0, self.run_manager("apply").returncode)
        self.assertEqual("600\n", self.run_manager("get", "auto_suspend_timeout_seconds").stdout)

    def test_config_core_never_publishes_two_legacy_authorities(self) -> None:
        source = MANAGER.read_text(encoding="utf-8")
        self.assertNotIn("/etc/gkd-mini/gdkmini.conf", source)
        self.assertNotIn("/usr/local/etc/gkd-mini/gdkmini.conf", source)


if __name__ == "__main__":
    unittest.main()
