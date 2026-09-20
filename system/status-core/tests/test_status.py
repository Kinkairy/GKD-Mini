#!/usr/bin/env python3
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "device/gkd-status"


class StatusTest(unittest.TestCase):
    def test_unified_status_contains_all_required_sections(self) -> None:
        with tempfile.TemporaryDirectory(prefix="gkd-status-") as directory:
            root = Path(directory)
            version = root / "version"
            runtime = root / "runtime"
            kernel = root / "kernel"
            version.write_text("RC3.5-host\n", encoding="utf-8")
            runtime.write_text("abc123\n", encoding="utf-8")
            kernel.write_text("6.1-test\n", encoding="utf-8")
            config = root / "config"
            service = root / "service"
            config.write_text(
                "#!/bin/sh\nprintf 'status=ready\\ngeneration=cfg123\\nresult=applied\\n'\n",
                encoding="utf-8",
            )
            service.write_text(
                "#!/bin/sh\nprintf 'hardware=running\\ninput=running\\n'\n",
                encoding="utf-8",
            )
            config.chmod(0o755)
            service.chmod(0o755)
            udc_state = root / "udc/controller/state"
            udc_state.parent.mkdir(parents=True)
            udc_state.write_text("configured\n", encoding="utf-8")
            update = root / "update"
            update.mkdir()
            (update / "last-result").write_text("idle\n", encoding="utf-8")
            env = os.environ.copy()
            env.update(
                {
                    "GKD_VERSION_FILE": str(version),
                    "GKD_RUNTIME_ID_FILE": str(runtime),
                    "GKD_KERNEL_RELEASE_FILE": str(kernel),
                    "GKD_CONFIG_CMD": str(config),
                    "GKD_SERVICE_CMD": str(service),
                    "GKD_UDC_ROOT": str(root / "udc"),
                    "GKD_NETWORK_INTERFACE": "definitely-absent",
                    "GKD_UPDATE_STATE_DIR": str(update),
                }
            )
            result = subprocess.run(
                ["sh", str(SCRIPT)],
                env=env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertIn("version=RC3.5-host", result.stdout)
            self.assertIn("kernel=6.1-test", result.stdout)
            self.assertIn("generation=cfg123", result.stdout)
            self.assertIn("hardware=running", result.stdout)
            self.assertIn("controller=configured", result.stdout)
            self.assertIn("[network]", result.stdout)
            self.assertIn("last-result=idle ", result.stdout)


if __name__ == "__main__":
    unittest.main()
