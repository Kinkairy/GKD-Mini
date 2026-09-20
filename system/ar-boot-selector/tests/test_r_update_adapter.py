#!/usr/bin/env python3
"""Exercise the R adapter with disposable fake tools, never block devices."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[3]
ADAPTER = PROJECT / "kernel/current/initramfs/dedicated-r-update.sh"


class RecoveryUpdateAdapterTest(unittest.TestCase):
    def run_case(self, request=3, prepare=0, coordinator=10, mount=0,
                 power=0, offline=0, locked=False, unmount=0):
        with tempfile.TemporaryDirectory(prefix="gkd-r-update-") as directory:
            root = Path(directory)
            state = root / "state"
            state.mkdir()
            if locked:
                (state / "card-operation.lock").mkdir()
            trace = root / "trace"
            for name, status in (("request", request), ("prepare", prepare),
                                 ("coordinator", coordinator), ("engine", 0)):
                tool = root / name
                tool.write_text(f'#!/bin/sh\necho "{name} $*" >> "$TRACE"\nexit {status}\n')
                tool.chmod(0o700)
            (root / "package").touch()
            (root / "key").touch()
            # Only the fixture source overrides OS boundaries; production has
            # no environment switch permitting fake mounts or device paths.
            source = ADAPTER.read_text().replace("state_dir=/run/gkd-recovery",
                                                 f"state_dir={state}")
            script = root / "test.sh"
            script.write_text('set -eu\n' + source + f'''
request={root}/request
prepare={root}/prepare
coordinator={root}/coordinator
engine={root}/engine
public_key={root}/key
package={root}/package
disk=fixture-disk
p1=fixture-p1
p3=fixture-p3
game_mount=fixture-game
recovery_update_offline() {{ return {offline}; }}
power_gate() {{ return {power}; }}
mount_game() {{ echo mount >> "$TRACE"; return {mount}; }}
umount() {{ echo umount >> "$TRACE"; return {unmount}; }}
sync() {{ echo sync >> "$TRACE"; }}
recovery_update_reboot() {{ echo reboot >> "$TRACE"; }}
prepare_update
''')
            result = subprocess.run(["sh", str(script)],
                                    env={**os.environ, "TRACE": str(trace)},
                                    capture_output=True, text=True)
            actions = trace.read_text().splitlines() if trace.exists() else []
            log = (state / "update.log").read_text() if (state / "update.log").exists() else ""
            self.assertEqual(locked or bool(unmount), (state / "card-operation.lock").exists())
            return result.returncode, actions, log

    def test_new_package_runs_existing_engine_before_reboot(self):
        code, actions, log = self.run_case()
        self.assertEqual(2, code)  # fixture reboot returns, production must not
        self.assertTrue(any("--source-kernel" in line for line in actions))
        order = [line.split()[0] for line in actions]
        self.assertEqual(["request", "mount", "prepare", "coordinator", "umount", "sync", "reboot"], order)
        self.assertIn("reboot-returned", log)

    def test_existing_transaction_recovers_without_game_card(self):
        _, actions, _ = self.run_case(request=0, mount=1, coordinator=20)
        self.assertFalse(any(line.startswith("prepare ") for line in actions))
        self.assertFalse("umount" in actions)
        self.assertIn("reboot", actions)

    def test_rejections_do_not_reboot_and_always_release_owned_mount(self):
        for kwargs in ({"prepare": 2}, {"coordinator": 2}, {"request": 2},
                       {"power": 1}, {"offline": 1}, {"mount": 1},
                       {"locked": True}, {"unmount": 1}):
            with self.subTest(kwargs=kwargs):
                code, actions, _ = self.run_case(**kwargs)
                self.assertEqual(2, code)
                self.assertNotIn("reboot", actions)
                if kwargs in ({"prepare": 2}, {"coordinator": 2}):
                    self.assertEqual(1, actions.count("umount"))


if __name__ == "__main__":
    unittest.main()
