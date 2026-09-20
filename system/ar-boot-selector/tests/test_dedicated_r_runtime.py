#!/usr/bin/env python3
from pathlib import Path
import importlib.util
import json
import re
import subprocess
import tempfile
import unittest
from unittest import mock


PROJECT = Path(__file__).resolve().parents[3]
DERIVE = PROJECT / "system/rc33-system-update/scripts/derive-update-supervisor.py"
SUPERVISOR = PROJECT / "kernel/current/initramfs/round83-supervisor.c"
LOADER = PROJECT / "kernel/current/initramfs/round83-maintenance-loader.generated.h"
UI = PROJECT / "system/ui-core/source/gkd-recovery-ui.c"
INPUT_OWNER = PROJECT / "system/ui-core/source/gkd-input-owner.c"
INPUT_DAEMON = PROJECT / "system/ui-core/source/gkd-input.c"
INPUT_INIT = PROJECT / "system/ui-core/device/S95gkd-input"
SCREENSHOT = PROJECT / "system/ui-core/source/gkd-screenshot.c"
CONFIG_MANAGER = PROJECT / "system/config-core/device/gkd-config"
CONFIG_SCHEMA = PROJECT / "system/config-core/schema/gdkmini.schema"
R_PROFILE = PROJECT / "system/config-core/profiles/dedicated-r.override.conf"
BUILD = PROJECT / "system/rc33-system-update/scripts/build-kernel-candidate.sh"
CAPSULE_BUILD = PROJECT / "system/rc33-system-update/scripts/build-recovery-capsule.py"
KERNEL_BUILD = PROJECT / "kernel/current/scripts/build-in-container.sh"
RAM_MANIFEST = PROJECT / "kernel/current/initramfs/ram-payload-manifest.json"
DEDICATED_MANIFEST = (
    PROJECT / "kernel/current/initramfs/dedicated-r-p1-manifest.json"
)


def load_capsule_builder():
    spec = importlib.util.spec_from_file_location("gkd_capsule_builder", CAPSULE_BUILD)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class DedicatedRecoveryRuntimeTest(unittest.TestCase):
    def test_recovery_power_gate_reuses_accepted_probe_and_fails_closed(self) -> None:
        adapter = PROJECT / "kernel/current/initramfs/dedicated-r-power-gate.sh"
        text = adapter.read_text()
        self.assertIn("gkd-battery-notify --probe-once", text)
        self.assertIn("/sys/class/udc/*/state", text)
        for percent, usb, allowed in (("30", "0", True), ("100", "0", True),
                                       ("29", "0", False), ("101", "0", False),
                                       ("", "0", False), ("invalid", "0", False),
                                       ("999999999999999999", "0", False),
                                       ("1", "1", True), ("", "1", True)):
            result = subprocess.run(["sh", "-c", '. "$1"; gkd_recovery_power_allowed "$2" "$3"',
                                     "test", str(adapter), percent, usb])
            self.assertEqual(allowed, result.returncode == 0, (percent, usb))
        manifest = json.loads(DEDICATED_MANIFEST.read_text())
        probe = next(row for row in manifest["p1_regular_files"]
                     if row["source"] == "/usr/sbin/gkd-battery-notify")
        self.assertEqual(probe["sha256"], load_capsule_builder().DEDICATED_BATTERY_SHA256)

    def test_elf_dependency_gate_checks_transitive_links_and_interpreter(self) -> None:
        builder = load_capsule_builder()
        with tempfile.TemporaryDirectory(prefix="gkd-r-elf-") as directory:
            root = Path(directory) / "root"
            for name in ("bin", "lib", "usr/lib"):
                (root / name).mkdir(parents=True, exist_ok=True)
            for name in ("bin/app", "lib/loader.so", "usr/lib/libfirst.so.1",
                         "usr/lib/libsecond.so.1"):
                (root / name).write_bytes(b"\x7fELFfixture")
            link = root / "usr/lib/libfirst.so"
            link.symlink_to("libfirst.so.1")
            reports = {
                "app": "[Requesting program interpreter: /lib/loader.so]\n"
                       " 0x1 (NEEDED) Shared library: [libfirst.so]\n",
                "libfirst.so.1": " 0x1 (NEEDED) Shared library: [libsecond.so.1]\n",
            }

            def inspect(command, **kwargs):
                self.assertEqual(command[:3], ["readelf", "-l", "-d"])
                return subprocess.CompletedProcess(
                    command, 0, reports.get(Path(command[-1]).name, ""), ""
                )

            with mock.patch.object(builder.subprocess, "run", side_effect=inspect):
                builder.verify_elf_dependencies(root)
                second = root / "usr/lib/libsecond.so.1"
                second.unlink()
                with self.assertRaisesRegex(SystemExit, "missing-elf-dependency.*libsecond"):
                    builder.verify_elf_dependencies(root)
                second.write_bytes(b"\x7fELFfixture")
                interpreter = root / "lib/loader.so"
                interpreter.unlink()
                with self.assertRaisesRegex(SystemExit, "missing-elf-dependency.*loader"):
                    builder.verify_elf_dependencies(root)
                interpreter.write_bytes(b"\x7fELFfixture")
                link.unlink()
                link.symlink_to("absent.so")
                with self.assertRaisesRegex(SystemExit, "missing-elf-dependency.*libfirst"):
                    builder.verify_elf_dependencies(root)
                outside = Path(directory) / "outside.so"
                outside.write_bytes(b"\x7fELFfixture")
                link.unlink()
                link.symlink_to(outside)
                with self.assertRaisesRegex(SystemExit, "missing-elf-dependency.*libfirst"):
                    builder.verify_elf_dependencies(root)

    def derive(self, dedicated: bool) -> str:
        with tempfile.TemporaryDirectory(prefix="gkd-r-runtime-") as directory:
            output = Path(directory) / "supervisor.c"
            command = [
                "python3", str(DERIVE), "--source", str(SUPERVISOR),
                "--output", str(output),
            ]
            if dedicated:
                command.append("--dedicated-recovery")
            subprocess.run(command, check=True, stdout=subprocess.PIPE, text=True)
            return output.read_text(encoding="utf-8")

    def test_dedicated_r_bypasses_legacy_failure_renderer(self) -> None:
        source = self.derive(True)
        hold = source[
            source.index("static __attribute__((noreturn)) void gkdu_recovery_hold"):
            source.index("int main(")
        ]
        self.assertIn('include "dedicated-r-boot.h"', source)
        self.assertIn("r_boot_begin()", hold)
        self.assertIn("r_boot_present(failed_stage, 1)", hold)
        self.assertNotIn("gkdu_recovery_clear_framebuffer", source)
        self.assertIn("if (r64m_runtime(&r64m_final_payload) == 0)", hold)
        self.assertNotIn("r80_debug_runtime(&r64m_final_payload)", hold)
        self.assertNotIn('r80i_trace("8RCV")', hold)
        self.assertIn('gkdu_recovery_hold("RCVR")', source)

    def test_normal_a_derivation_keeps_existing_recovery_wrapper(self) -> None:
        source = self.derive(False)
        self.assertIn("if (r80_debug_runtime(&r64m_final_payload) == 0)", source)
        self.assertIn('r80i_trace("8RCV")', source)
        self.assertNotIn("gkdu_recovery_clear_framebuffer", source)
        self.assertNotIn("dedicated-r-boot.h", source)
        self.assertIn('gkdu_recovery_hold("RCVM")', source)
        self.assertIn('gkdu_recovery_hold("RCVU")', source)

    def test_r_uses_optional_credentials_default_ssh_and_no_cold_rebind(self) -> None:
        source = LOADER.read_text(encoding="utf-8")
        sealed = source[source.index("static int r64m_runtime("):]
        self.assertIn("credentials_ready = r64m_copy_p2() == 0", sealed)
        self.assertIn('r84_recovery_usb("network-start")', sealed)
        self.assertIn('"/usr/sbin/gkd-recovery-usb"', source)
        self.assertIn("if (!r80_gpio_keys_ready())", sealed)
        self.assertIn("if (r80_gpio_keys_rebind() < 0)", sealed)

    def test_every_sealed_capsule_destination_parent_is_prepared(self) -> None:
        builder = load_capsule_builder()
        manifest = json.loads(RAM_MANIFEST.read_text(encoding="utf-8"))
        source = CAPSULE_BUILD.read_text(encoding="utf-8")
        # R uses the original loader; A-only destinations use the A loader
        # generated and validated by the actual application capsule build.
        a_start = source.index('    if arguments.layout == "application-a":\n')
        a_end = source.index('    extra_files = []', a_start)
        source = source[:a_start] + source[a_end:]
        literal_destinations = set(
            re.findall(
                r',\s*"(/[^"]+)"\s*,\s*0o[0-7]+\)',
                source,
            )
        )
        literal_destinations.update(
            re.findall(
                r'install_link\(root,\s*"(/[^"]+)"', source
            )
        )
        destinations = (
            [item["destination"] for item in manifest["p1_regular_files"]]
            + [item["destination"] for item in manifest["p1_symlinks"]]
            + sorted(literal_destinations)
        )
        builder.verify_copy_destination_parents(LOADER, destinations)
        self.assertIn("/etc/gkd-mini/fonts", builder.prepared_directories(LOADER))

        broken = LOADER.read_text(encoding="utf-8").replace(
            '{"/etc/gkd-mini/fonts",0755},\n', ""
        )
        with tempfile.TemporaryDirectory(prefix="gkd-r-parent-gate-") as directory:
            broken_loader = Path(directory) / "loader.h"
            broken_loader.write_text(broken, encoding="utf-8")
            with self.assertRaisesRegex(SystemExit, "unprepared-parent /etc/gkd-mini/fonts"):
                builder.verify_copy_destination_parents(
                    broken_loader, ["/etc/gkd-mini/fonts/fallback.psf"]
                )

    def test_dedicated_capsule_uses_complete_verified_symlink_closure(self) -> None:
        builder = load_capsule_builder()
        manifest = json.loads(RAM_MANIFEST.read_text(encoding="utf-8"))
        normal = builder.manifest_links_for_layout(manifest, "fixed")
        dedicated = builder.manifest_links_for_layout(manifest, "dedicated-r")
        self.assertEqual(
            normal,
            [item for item in manifest["p1_symlinks"] if item["source_required"]],
        )
        self.assertEqual(dedicated, manifest["p1_symlinks"])
        self.assertEqual(len(normal), 5)
        self.assertEqual(len(dedicated), 37)
        dedicated_paths = {item["destination"] for item in dedicated}
        self.assertTrue({"/bin/sh", "/bin/rm", "/bin/cat"} <= dedicated_paths)
        self.assertEqual(
            builder.DEDICATED_EXTRA_LINKS,
            {"/bin/usleep": "busybox", "/sbin/insmod": "../usr/bin/kmod",
             "/usr/lib/libz.so.1": "libz.so.1.2.11"},
        )
        dedicated_manifest = json.loads(
            DEDICATED_MANIFEST.read_text(encoding="utf-8")
        )
        kmod = [
            item for item in dedicated_manifest["p1_regular_files"]
            if item["destination"] == "/usr/bin/kmod"
        ]
        self.assertEqual(len(kmod), 1)
        self.assertEqual(kmod[0]["sha256"], builder.DEDICATED_KMOD_SHA256)
        self.assertEqual(
            {
                item["destination"]: item["target"]
                for item in dedicated_manifest["p1_symlinks"]
            },
            builder.DEDICATED_EXTRA_LINKS,
        )

    def test_capsule_symlink_readback_parser_preserves_targets(self) -> None:
        builder = load_capsule_builder()
        listing = (
            "lrwxrwxrwx root/root 7 2026-07-31 06:00 "
            "squashfs-root/bin/sh -> busybox\n"
            "lrwxrwxrwx root/root 14 2026-07-31 06:00 "
            "squashfs-root/sbin/start-stop-daemon -> ../bin/busybox\n"
        )
        self.assertEqual(
            builder.parse_squashfs_symlinks(listing),
            {"/bin/sh": "busybox", "/sbin/start-stop-daemon": "../bin/busybox"},
        )

    def test_export_transitions_between_debug_and_network(self) -> None:
        source = UI.read_text(encoding="utf-8")
        self.assertIn(
            '#define GKD_RECOVERY_USB_PROGRAM "/usr/sbin/gkd-recovery-usb"',
            source,
        )
        self.assertIn('#define GKD_RECOVERY_USB_START "export-start"', source)
        self.assertIn('#define GKD_RECOVERY_USB_STOP "export-stop"', source)
        self.assertIn(
            '#define GKD_RECOVERY_USB_PROGRAM '
            '"/usr/sbin/gkd-recovery-mass-storage"',
            source,
        )
        self.assertIn('char *stop[] = {GKD_RECOVERY_USB_PROGRAM, "stop", NULL}', source)

    def test_final_r_binary_gate_rejects_legacy_ui_reference(self) -> None:
        source = BUILD.read_text(encoding="utf-8")
        kernel_build = KERNEL_BUILD.read_text(encoding="utf-8")
        self.assertIn("preserving stage marker and holding", source)
        self.assertIn("gkd-round64-rescue-ui", source)
        self.assertIn("! grep -aF", source)
        self.assertIn("squashfs-root/usr/sbin/gkd-recovery-usb", source)
        self.assertIn("! unsquashfs -ll", source)
        self.assertIn("-DR64M_DEDICATED_RECOVERY=1", kernel_build)
        self.assertIn('"$build_mode" = dedicated-recovery', kernel_build)

    def test_r_reuses_one_input_owner_and_screenshot_stack(self) -> None:
        loader = LOADER.read_text(encoding="utf-8")
        ui = UI.read_text(encoding="utf-8")
        owner = INPUT_OWNER.read_text(encoding="utf-8")
        daemon = INPUT_DAEMON.read_text(encoding="utf-8")
        init = INPUT_INIT.read_text(encoding="utf-8")
        screenshot = SCREENSHOT.read_text(encoding="utf-8")
        config_manager = CONFIG_MANAGER.read_text(encoding="utf-8")
        config_schema = CONFIG_SCHEMA.read_text(encoding="utf-8")
        r_profile = R_PROFILE.read_text(encoding="utf-8")
        build = BUILD.read_text(encoding="utf-8")
        capsule_build = CAPSULE_BUILD.read_text(encoding="utf-8")

        self.assertIn("gkd_input_owner_open(&runtime.input)", ui)
        self.assertIn("gkd_input_owner_next_key", ui)
        self.assertNotIn("/run/gkd-ui/input", ui)
        self.assertNotIn("PPM", ui)
        self.assertIn('GKD_INPUT_PHYSICAL_NAME "gpio-keys"', owner)
        self.assertIn(
            'GKD_INPUT_VIRTUAL_NAME "GKD Mini Virtual Controls"', owner
        )
        self.assertIn("attempt < 50U", owner)
        self.assertIn("usleep(20000U)", owner)
        self.assertIn("POLLERR | POLLHUP | POLLNVAL", owner)
        self.assertLess(
            owner.index("ioctl(owner->virtual_fd, EVIOCGRAB, 1)"),
            owner.index("publish_marker()"),
        )
        self.assertLess(
            owner.index("safe_remove_marker()", owner.index("gkd_input_owner_close")),
            owner.index("EVIOCGRAB, 0", owner.index("gkd_input_owner_close")),
        )
        self.assertIn('GKD_PRIORITY_INPUT_RECOVERY_EXE "/usr/sbin/gkd-recovery-ui"', daemon)
        self.assertIn("chooser_grabs_input(config->require_owner)", daemon)
        self.assertIn("daemon --require-owner", init)
        self.assertIn('"/media/gkd-r-screenshots"', loader)
        self.assertNotIn('"/usr/sbin/gkd-screenshot", "set-path"', loader)
        self.assertIn('"GKD_INPUT_REQUIRE_OWNER=1"', loader)
        self.assertIn("if (!timeout_ms)", screenshot)
        self.assertIn('!memcmp(value, "NONE", 4U)', screenshot)
        self.assertIn("L2+R2|NONE", config_manager)
        self.assertIn(
            "screenshot_osd_timeout_ms|uint|3000|0|5000", config_schema
        )
        self.assertEqual(
            "# Dedicated R uses the common schema with a separate machine-validation profile.\n"
            "screenshot_output_dir=/media/gkd-r-screenshots\n"
            "screenshot_osd_timeout_ms=0\n"
            "screenshot_hotkey=NONE\n",
            r_profile,
        )
        self.assertIn('"/etc/gkd-mini/gdkmini.conf"', capsule_build)
        self.assertIn('"/usr/sbin/gkd-screenshot"', capsule_build)
        self.assertIn('"/etc/init.d/S94gkd-screenshot"', capsule_build)
        self.assertIn("verify_copy_destination_parents", capsule_build)
        self.assertIn("verify_capsule_symlinks", capsule_build)
        self.assertIn("--loader", build)
        self.assertLess(
            loader.index("r84_recovery_input_start()", loader.index("static int r64m_runtime")),
            loader.index("r84_recovery_ui_enter()", loader.index("static int r64m_runtime")),
        )
        for path in (
            "squashfs-root/usr/sbin/gkd-input",
            "squashfs-root/etc/init.d/S95gkd-input",
            "squashfs-root/usr/lib/gkd-uinput.ko",
            "squashfs-root/usr/sbin/gkd-screenshot",
        ):
            self.assertIn(path, build)
        self.assertIn(
            "squashfs-root/etc/init.d/S94gkd-screenshot", build
        )
        self.assertIn(
            "squashfs-root/usr/local/etc/gkd-mini/gdkmini.conf", build
        )


if __name__ == "__main__":
    unittest.main()
