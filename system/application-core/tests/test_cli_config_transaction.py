#!/usr/bin/env python3
"""The real wrapper with isolated command endpoints; no pre-apply or false timeout failure."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
CLI = ROOT / "system/application-core/device/gkd-application-config"

class ConfigCLI(unittest.TestCase):
    def exercise(self, mode):
        with tempfile.TemporaryDirectory(prefix="gkd-cli-regression-") as directory:
            work = Path(directory); log = work / "calls"; counter = work / "counter"
            config = work / "gkd-config"; service = work / "service"; sleep = work / "usleep"
            config.write_text('#!/bin/sh\nprintf "UNEXPECTED_CONFIG %s\\n" "$*" >>"$CALLS"\nexit 99\n')
            service.write_text('''#!/bin/sh
printf '%s\\n' "$2" >>"$CALLS"
case "$2" in
 config-save) if [ "$CASE" = busy ]; then echo "GKD_APPLICATION_COMMAND=REJECTED errno=16"; exit 1; fi; [ "$CASE" != lost_submit ] || exit 1; echo 'GKD_APPLICATION_COMMAND=ACCEPTED errno=0';;
 config-status)
  case "$CASE" in
   unknown) exit 1;;
   failed) echo 'GKD_APPLICATION_CONFIG=FAILED errno=5';;
   uncertain) echo 'GKD_APPLICATION_CONFIG=UNKNOWN errno=5';;
   unexpected) echo 'GKD_APPLICATION_CONFIG=IDLE errno=0';;
   pending) echo 'GKD_APPLICATION_CONFIG=SAVING errno=0';;
   *) echo 'GKD_APPLICATION_CONFIG=SAVED errno=0';;
  esac;;
 *) exit 2;;
esac
''')
            sleep.write_text('#!/bin/sh\nexit 0\n')
            for path in (config, service, sleep): path.chmod(0o700)
            text = CLI.read_text().replace('/usr/sbin/gkd-application-service', str(service)).replace('/usr/sbin/gkd-config', str(config))
            text = text.replace('PATH=/sbin:/bin:/usr/sbin:/usr/bin', 'PATH='+str(work)+':/usr/bin:/bin')
            wrapper = work / "wrapper"; wrapper.write_text(text)
            result = subprocess.run(["sh", str(wrapper), "apply"], env=dict(os.environ, CASE=mode, CALLS=str(log)), capture_output=True, text=True, timeout=15)
            calls = log.read_text().splitlines()
            self.assertEqual(calls[0], "config-save")
            self.assertFalse(any("UNEXPECTED_CONFIG" in c for c in calls))
            if mode == "saved": self.assertEqual(result.returncode, 0)
            elif mode in ("busy", "failed"): self.assertNotEqual(result.returncode, 0)
            else:
                self.assertEqual(result.returncode, 75)
                self.assertIn("UNKNOWN" if mode in ("unknown", "lost_submit", "uncertain", "unexpected") else "PENDING", result.stderr)
                self.assertNotIn("CONFIG=FAILED", result.stderr)
            if mode == "busy": self.assertEqual(calls, ["config-save"])

    def test_service_owns_every_apply(self):
        for mode in ("saved", "busy", "failed", "unknown", "pending", "lost_submit", "uncertain", "unexpected"):
            with self.subTest(mode=mode): self.exercise(mode)

if __name__ == "__main__": unittest.main()
