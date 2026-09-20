#!/usr/bin/env python3
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


PROJECT = Path(__file__).resolve().parents[3]
SERVICE = PROJECT / "system/service-core/device/gkd-service"
CONFIGD = PROJECT / "system/service-core/device/gkd-configd"
CONFIG = PROJECT / "system/config-core/device/gkd-config"
SAFEPOINT = PROJECT / "system/service-core/device/gkd-safe-point"
SCHEMA = PROJECT / "system/config-core/schema/gdkmini.schema"
MERGER = PROJECT / "system/config-core/device/gkd-config-merge.awk"


class ServiceCoreTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="gkd-service-core-")
        self.root = Path(self.temporary.name)
        self.actions = self.root / "actions.log"
        self.safe_actions = self.root / "safe.log"
        self.registry = self.root / "services.registry"
        rows: list[str] = []
        for name, owners, dependencies, order, safe in (
            ("layout", "ui-layout", "", 10, "action-idle"),
            ("hardware", "hardware", "", 20, "transaction-idle"),
            ("input", "input-stack", "hardware", 30, "keys-released"),
        ):
            init = self.root / f"init-{name}"
            init.write_text(
                "#!/bin/sh\n"
                "printf '%s:%s:%s\\n' '" + name + "' \"$1\" "
                "\"$(basename \"$(dirname \"$GKD_CONFIG_EFFECTIVE\")\")\" "
                ">>\"$GKD_TEST_ACTIONS\"\n"
                "case \"$GKD_TEST_FAIL:$GKD_CONFIG_EFFECTIVE\" in "
                "'" + name + "':*/new/effective.conf) exit 1 ;; esac\n"
                "exit 0\n",
                encoding="utf-8",
            )
            init.chmod(0o755)
            rows.append(
                f"{name}|{init}|{owners}|{dependencies}|{order}||oneshot|restart|{safe}\n"
            )
        self.registry.write_text("".join(rows), encoding="utf-8")
        self.safe = self.root / "safe"
        self.safe.write_text(
            "#!/bin/sh\n"
            "printf '%s\\n' \"$1\" >>\"$GKD_TEST_SAFE_ACTIONS\"\n"
            "[ \"$1\" != \"$GKD_TEST_BLOCK_SAFE\" ]\n",
            encoding="utf-8",
        )
        self.safe.chmod(0o755)
        self.old = self.root / "old"
        self.new = self.root / "new"
        self.old.mkdir()
        self.new.mkdir()
        (self.old / "effective.conf").write_text("volume_step=2\n", encoding="utf-8")
        (self.new / "effective.conf").write_text("volume_step=3\n", encoding="utf-8")
        (self.new / "changes.tsv").write_text(
            "volume_step\thardware\timmediate\n", encoding="utf-8"
        )
        self.env = os.environ.copy()
        self.env.update(
            {
                "GKD_SERVICE_REGISTRY": str(self.registry),
                "GKD_SAFEPOINT_CMD": str(self.safe),
                "GKD_SERVICE_RUN_DIR": str(self.root / "run"),
                "GKD_SERVICE_LOG": str(self.root / "service.log"),
                "GKD_ACTION_FREEZE": str(self.root / "action-freeze"),
                "GKD_MENU_LEASE": str(self.root / "menu-lease"),
                "GKD_TEST_ACTIONS": str(self.actions),
                "GKD_TEST_SAFE_ACTIONS": str(self.safe_actions),
                "GKD_TEST_FAIL": "",
                "GKD_TEST_BLOCK_SAFE": "",
                "GKD_REFRESH_FREEZE_HELPER": "disabled",
                "GKD_CONFIG_COMPAT_DEFAULT": str(self.root / "compat-default.conf"),
                "GKD_CONFIG_COMPAT_OVERRIDE": str(self.root / "compat-override.conf"),
            }
        )

    def tearDown(self) -> None:
        for item in self.root.rglob("*"):
            if item.is_dir():
                item.chmod(0o755)
        self.temporary.cleanup()

    def run_service(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["sh", str(SERVICE), *arguments],
            env=self.env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def test_changed_owner_refreshes_only_its_declared_service(self) -> None:
        result = self.run_service("apply-config", str(self.old), str(self.new))
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual("hardware:restart:new\n", self.actions.read_text())
        self.assertEqual("transaction-idle\n", self.safe_actions.read_text())

    def test_all_safe_points_are_checked_before_first_mutation(self) -> None:
        (self.new / "changes.tsv").write_text(
            "volume_step\thardware\timmediate\n"
            "input_map_a\tinput-stack\tkeys-released\n",
            encoding="utf-8",
        )
        self.env["GKD_TEST_BLOCK_SAFE"] = "keys-released"
        result = self.run_service("apply-config", str(self.old), str(self.new))
        self.assertEqual(4, result.returncode)
        self.assertFalse(self.actions.exists())

    def test_failure_rolls_back_touched_services_in_reverse_order(self) -> None:
        (self.new / "changes.tsv").write_text(
            "volume_step\thardware\timmediate\n"
            "input_map_a\tinput-stack\tkeys-released\n",
            encoding="utf-8",
        )
        self.env["GKD_TEST_FAIL"] = "input"
        result = self.run_service("apply-config", str(self.old), str(self.new))
        self.assertEqual(1, result.returncode)
        self.assertEqual(
            "hardware:restart:new\n"
            "input:restart:new\n"
            "input:restart:old\n"
            "hardware:restart:old\n",
            self.actions.read_text(),
        )

    def test_next_entry_change_is_reported_without_mutation(self) -> None:
        (self.new / "changes.tsv").write_text(
            "debug_usb_poll_ms\tdebug\tnext-entry\n", encoding="utf-8"
        )
        result = self.run_service("apply-config", str(self.old), str(self.new))
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIn("policy=next-entry", result.stdout)
        self.assertIn("services=0", result.stdout)
        self.assertFalse(self.actions.exists())

    def test_action_freeze_defers_the_whole_transaction(self) -> None:
        Path(self.env["GKD_ACTION_FREEZE"]).write_text("busy\n", encoding="utf-8")
        result = self.run_service("apply-config", str(self.old), str(self.new))
        self.assertEqual(4, result.returncode)
        self.assertFalse(self.actions.exists())

    def test_registry_rejects_missing_or_reverse_dependencies(self) -> None:
        self.registry.write_text(
            f"late|{self.root / 'init-hardware'}|hardware|missing|10||oneshot|restart|action-idle\n",
            encoding="utf-8",
        )
        self.assertEqual(2, self.run_service("apply-config", str(self.old), str(self.new)).returncode)

    def test_authoritative_safe_points_fail_closed(self) -> None:
        input_idle = self.root / "input-idle"
        input_idle.write_text("#!/bin/sh\nexit \"${GKD_TEST_INPUT_RESULT:-0}\"\n", encoding="utf-8")
        input_idle.chmod(0o755)
        network = self.root / "network"
        network.mkdir()
        safe_env = self.env | {
            "GKD_INPUT_IDLE_CMD": str(input_idle),
            "GKD_NETWORK_RUN_DIR": str(network),
        }
        released = subprocess.run(["sh", str(SAFEPOINT), "keys-released"], env=safe_env)
        self.assertEqual(0, released.returncode)
        safe_env["GKD_TEST_INPUT_RESULT"] = "1"
        pressed = subprocess.run(["sh", str(SAFEPOINT), "keys-released"], env=safe_env)
        self.assertEqual(1, pressed.returncode)
        safe_env["GKD_TEST_INPUT_RESULT"] = "0"
        idle_network = subprocess.run(["sh", str(SAFEPOINT), "network-rebuild"], env=safe_env)
        self.assertEqual(0, idle_network.returncode)
        (network / "network-owned").write_text("1\n", encoding="utf-8")
        active_network = subprocess.run(["sh", str(SAFEPOINT), "network-rebuild"], env=safe_env)
        self.assertEqual(1, active_network.returncode)

    def test_configd_prepares_refreshes_and_only_then_commits(self) -> None:
        override = self.root / "override.conf"
        override.write_text("", encoding="utf-8")
        config_run = self.root / "config-run"
        config_state = self.root / "config-state"
        self.env.update(
            {
                "GKD_CONFIG_SCHEMA": str(SCHEMA),
                "GKD_CONFIG_MERGER": str(MERGER),
                "GKD_CONFIG_OVERRIDE": str(override),
                "GKD_CONFIG_RUN_DIR": str(config_run),
                "GKD_CONFIG_STATE_DIR": str(config_state),
                "GKD_CONFIG_CMD": str(CONFIG),
                "GKD_SERVICE_CMD": str(SERVICE),
            }
        )
        first = self.run_service_script(CONFIGD, "once")
        self.assertEqual(0, first.returncode, first.stderr)
        before = os.readlink(config_run / "current")
        self.assertFalse(self.actions.exists())
        override.write_text("volume_step=3\n", encoding="utf-8")
        second = self.run_service_script(CONFIGD, "once")
        self.assertEqual(0, second.returncode, second.stderr)
        self.assertNotEqual(before, os.readlink(config_run / "current"))
        self.assertEqual("hardware:restart:" + os.readlink(config_run / "current").split("/")[-1] + "\n",
                         self.actions.read_text())

    def run_service_script(
        self, script: Path, *arguments: str
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["sh", str(script), *arguments],
            env=self.env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )


if __name__ == "__main__":
    unittest.main()
