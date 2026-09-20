#!/usr/bin/env python3
"""Image-local command closure: missing host tools must never mask omissions."""
import importlib.util
import copy
import json
from pathlib import Path
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location(
    "capsule", PROJECT / "system/rc33-system-update/scripts/build-recovery-capsule.py")
capsule = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capsule)


class CapsuleCommands(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="gkd-app-command-test.")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in capsule.APPLICATION_COMMANDS:
            path = self.root / name.lstrip("/")
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"fixture")
            path.chmod(0o555)

    def test_application_profile_is_closed_partition(self):
        manifest = json.loads((PROJECT / "kernel/current/initramfs/ram-payload-manifest.json").read_text())
        original = copy.deepcopy(manifest)
        profile = capsule.application_payload_profile(manifest)
        self.assertEqual(manifest, original)
        self.assertEqual(len(profile["retained_base_regular"]), 10)
        self.assertEqual(len(profile["excluded_base_regular"]), 17)
        self.assertEqual(profile["replaced_regular"], ["/usr/sbin/gkd-screenshot"])
        extra = copy.deepcopy(manifest)
        extra["p1_regular_files"].append({"destination": "/usr/sbin/unreviewed"})
        missing = copy.deepcopy(manifest)
        missing["p1_regular_files"].pop()
        for changed in (extra, missing):
            with self.assertRaisesRegex(SystemExit, "payload-profile-inventory"):
                capsule.application_payload_profile(changed)

    def test_obsolete_application_payload_is_rejected(self):
        manifest = json.loads((PROJECT / "kernel/current/initramfs/ram-payload-manifest.json").read_text())
        profile = capsule.application_payload_profile(manifest)
        for name in profile["retained_base_regular"] + profile["replaced_regular"]:
            path = self.root / name.lstrip("/")
            path.parent.mkdir(parents=True, exist_ok=True)
            if not path.exists():
                path.write_bytes(b"current")
        capsule.verify_application_payload(self.root, profile)
        removed = set(profile["excluded_base_regular"]) - set(profile["replaced_regular"])
        for name in removed:
            with self.subTest(path=name):
                path = self.root / name.lstrip("/")
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"obsolete")
                with self.assertRaisesRegex(SystemExit, "obsolete-backend"):
                    capsule.verify_application_payload(self.root, profile)
                path.unlink()
                path.symlink_to("missing")
                with self.assertRaisesRegex(SystemExit, "obsolete-backend"):
                    capsule.verify_application_payload(self.root, profile)
                path.unlink()
        for name in profile["retained_base_regular"] + profile["replaced_regular"]:
            with self.subTest(required=name):
                path = self.root / name.lstrip("/")
                path.unlink()
                with self.assertRaisesRegex(SystemExit, "missing-retained"):
                    capsule.verify_application_payload(self.root, profile)
                path.write_bytes(b"current")

    def test_complete_image(self):
        capsule.verify_application_commands(self.root)

    def test_each_required_command_is_mandatory(self):
        for name in capsule.APPLICATION_COMMANDS:
            with self.subTest(command=name):
                path = self.root / name.lstrip("/")
                path.unlink()
                with self.assertRaisesRegex(SystemExit, "missing-command " + name):
                    capsule.verify_application_commands(self.root)
                path.write_bytes(b"fixture")
                path.chmod(0o555)

    def test_applet_links_resolve_to_image_busybox(self):
        for name, target in capsule.APPLICATION_EXTRA_LINKS.items():
            path = self.root / name.lstrip("/")
            path.unlink()
            path.symlink_to(target)
        capsule.verify_application_commands(self.root)

    def test_host_escape_is_rejected(self):
        path = self.root / "bin/wc"
        path.unlink()
        path.symlink_to("/bin/sh")
        with self.assertRaisesRegex(SystemExit, "missing-command /bin/wc"):
            capsule.verify_application_commands(self.root)

    def test_broken_link_is_rejected(self):
        path = self.root / "bin/wc"
        path.unlink()
        path.symlink_to("absent")
        with self.assertRaisesRegex(SystemExit, "missing-command /bin/wc"):
            capsule.verify_application_commands(self.root)

    def test_nonexecutable_is_rejected(self):
        (self.root / "bin/wc").chmod(0o444)
        with self.assertRaisesRegex(SystemExit, "missing-command /bin/wc"):
            capsule.verify_application_commands(self.root)


if __name__ == "__main__":
    unittest.main()
