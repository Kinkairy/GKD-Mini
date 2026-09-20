#!/usr/bin/env python3
"""Isolated minimum A profile using the accepted R loader/build foundation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

PROJECT = Path(__file__).resolve().parents[3]
UPDATE = PROJECT / "system/rc33-system-update/scripts"
APP = PROJECT / "system/application-core"
INIT = PROJECT / "kernel/current/initramfs"

def run(*args, **kwargs):
    subprocess.run([str(arg) for arg in args], check=True, **kwargs)

def exact_replace(text, old, new):
    if text.count(old) != 1:
        raise ValueError("source contract mismatch: " + old[:100])
    return text.replace(old, new, 1)

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--p1", type=Path, required=True)
    p.add_argument("--p1-sha256", required=True)
    p.add_argument("--runtime-id", required=True)
    p.add_argument("--source-slot", type=Path, required=True)
    p.add_argument("--app-components", type=Path, required=True)
    p.add_argument("--busybox-archive", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    if args.output.exists() or not str(args.output).startswith("/tmp/gkd-mini-public/gkd-app-minimal-"):
        raise ValueError("new scoped output required")
    if hashlib.sha256(args.p1.read_bytes()).hexdigest() != args.p1_sha256:
        raise ValueError("P1 drift")
    args.output.mkdir(mode=0o700)
    staging = Path(tempfile.mkdtemp(prefix="gkd-current-init-app.", dir="/tmp/gkd-mini-public"))
    kernel = Path("/tmp/gkd-mini-public/gkd-kernel-current-" + args.output.name)
    components = args.output / "update-components"
    ui = Path("/tmp/gkd-mini-public/gkd-ui-" + args.output.name)
    controls = Path("/tmp/gkd-mini-public/gkd-controls-" + args.output.name)
    service = Path("/tmp/gkd-mini-public/gkd-app-service-" + args.output.name)
    adapters = Path("/tmp/gkd-mini-public/gkd-app-adapters-" + args.output.name)
    stat = Path("/tmp/gkd-mini-public/gkd-app-stat-" + args.output.name)
    menu = Path("/tmp/gkd-mini-public/gkd-menu-controller-" + args.output.name)
    print("GKD_APP_BUILD_STAGING=" + str(staging), flush=True)
    run("sh", UPDATE / "build-components.sh", args.p1, args.runtime_id, components, "normal")
    run("sh", PROJECT / "system/ui-core/scripts/build-mips.sh", ui, "dedicated-r")
    run("sh", PROJECT / "system/ui-core/scripts/build-controls.sh", controls, "mips")
    run("sh", APP / "scripts/build-service.sh", service, "mips")
    run("python3", APP / "scripts/build-launcher.py", "--p1", args.p1,
        "--p1-sha256", args.p1_sha256, "--output", service / "launcher")
    run("sh", APP / "scripts/build-stat.sh", stat, args.busybox_archive)
    run("sh", APP / "scripts/build-adapters.sh", adapters, "mips")
    run("sh", PROJECT / "system/ui-core/scripts/build-menu-controller.sh", menu, "mips")
    payload_profile = json.loads((APP / "config/ram-payload-profile.json").read_text())
    exclusions = [argument for path in payload_profile["excluded_base_regular"]
                  for argument in ("--exclude-regular", path)]
    run("python3", PROJECT / "kernel/current/scripts/verify-ram-closure.py",
        "--p1", args.p1, "--temporary", staging / "closure-readback",
        "--derived-header", staging / "round83-ram-payload.generated.h",
        "--extra-manifest", INIT / "dedicated-r-p1-manifest.json", *exclusions)
    schema = staging / "gdkmini.application.schema"
    run("python3", APP / "scripts/derive-schema.py",
        PROJECT / "system/config-core/schema/gdkmini.schema", schema)
    config_env = dict(os.environ, GKD_CONFIG_SCHEMA=str(schema),
        GKD_CONFIG_MERGER=str(PROJECT / "system/config-core/device/gkd-config-merge.awk"),
        GKD_CONFIG_OVERRIDE=str(APP / "config/application.override.conf"),
        GKD_CONFIG_RUN_DIR=str(staging / "config-run"), GKD_CONFIG_STATE_DIR=str(staging / "config-state"))
    run("sh", PROJECT / "system/config-core/device/gkd-config", "apply", env=config_env)
    config = staging / "config-run/current/effective.conf"
    run("python3", UPDATE / "generate-ram-runtime.py", "--p1", args.p1,
        "--p1-sha256", args.p1_sha256, "--components", components,
        "--output", staging / "round84-update-runtime.generated.h")
    supervisor = staging / "round84-supervisor.generated.c"
    run("python3", UPDATE / "derive-update-supervisor.py", "--source", INIT / "round83-supervisor.c",
        "--output", supervisor, "--dedicated-recovery")
    text = supervisor.read_text()
    text = exact_replace(text, 'gkdu_recovery_hold("RCVR")', 'gkdu_recovery_hold("APPA")')
    text = exact_replace(text, "GKD independent RAM recovery entered", "GKD RC3.6 A entered")
    text = exact_replace(text, "GKD RAM recovery service failed; preserving stage marker and holding",
                         "GKD minimal A application failed; preserving SSH and holding")
    supervisor.write_text(text)
    loader = (INIT / "round83-maintenance-loader.generated.h").read_text()
    loader = exact_replace(loader, '{"/usr/lib",0755}',
        '{"/usr/lib",0755},{"/usr/libexec",0755},{"/usr/share",0755},'
        '{"/usr/share/licenses",0755},{"/usr/share/licenses/gkd-native-font",0755}')
    loader = exact_replace(loader, '"/usr/sbin/gkd-recovery-ui"', '"/usr/sbin/gkd-application-start"')
    loader = exact_replace(loader, '"GKD_INPUT_REQUIRE_OWNER=1"', '"GKD_INPUT_REQUIRE_OWNER=0"')
    loader = exact_replace(loader, '"/usr/sbin/gkd-recovery-usb"', '"/usr/sbin/gkd-application-bootstrap-usb"')
    loader = exact_replace(loader, """\tif (r84_recovery_input_start() < 0) {
\t\t(void)R80M_TRACE("8P0D"); return -1;
\t}""", """\t/* Application service starts the sole input producer after P2 config load. */""")
    (staging / "round83-maintenance-loader.generated.h").write_text(loader)
    # No R title/menu or old recovery drawing in A. Accepted external art remains
    # on P1; the application worker now owns playback after its read-only mount.
    # This supervisor remains a non-drawing owner; no early duplicate renderer.
    (staging / "dedicated-r-boot.h").write_text("""
static void *r_boot_mapping;
static unsigned r_boot_stage;
static void r_boot_begin(void) { r_boot_stage = 0; }
static void r_boot_close(void) { r_boot_mapping = 0; }
static void r_boot_present(unsigned stage, int failed) { (void)failed; r_boot_stage = stage; }
static int r_boot_trace(const char marker[4]) { return r80i_trace(marker); }
""")
    for name in ("round64-request-gate.h", "round38-animation-format.h"):
        shutil.copyfile(INIT / name, staging / name)
    capsule = staging / "recovery-capsule.bin"
    header = staging / "round84-recovery-capsule.generated.h"
    run("python3", UPDATE / "build-recovery-capsule.py",
        "--manifest", INIT / "ram-payload-manifest.json",
        "--verified-root", staging / "closure-readback", "--components", components,
        "--recovery-script", INIT / "gkd-recovery", "--mass-storage-script", INIT / "gkd-recovery-mass-storage",
        "--recovery-usb-script", INIT / "gkd-recovery-usb",
        "--ui-binary", ui / "gkd-recovery-ui", "--input-binary", ui / "gkd-input",
        "--input-init", ui / "S95gkd-input", "--screenshot-binary", ui / "gkd-screenshot",
        "--system-config", config, "--ui-config", ui / "gdkmini-ui.defaults.conf",
        "--ui-font", ui / "fallback.psf", "--temporary", staging / "recovery-root",
        "--output", capsule, "--header", header,
        "--loader", staging / "round83-maintenance-loader.generated.h",
        "--layout", "application-a", "--application-components", args.app_components,
        "--controls-components", controls, "--service-components", service,
        "--adapter-components", adapters, "--menu-components", menu, "--stat-binary", stat / "stat", "--application-schema", schema)
    run("sh", PROJECT / "kernel/current/scripts/build-kernel.sh",
        kernel, supervisor, header, env=dict(os.environ, GKD_CURRENT_BUILD_MODE="application-minimal"))
    built_config = (kernel / "gkd350.config").read_text()
    if "# CONFIG_FRAMEBUFFER_CONSOLE is not set" not in built_config.splitlines():
        raise ValueError("A animation requires no framebuffer console writer")
    run("python3", UPDATE / "build-kernel-slot.py", "--source-slot", args.source_slot,
        "--kernel-build", kernel, "--name", "gkd-rc3.6-a", "--output", args.output / "application-a-slot.bin",
        "--companion", capsule, "--companion-header", header, "--slot-offset", "0x900000")
    run("python3", UPDATE / "verify-kernel-slot.py", "--slot", args.output / "application-a-slot.bin",
        "--raw", kernel / "vmlinux.bin", "--name", "gkd-rc3.6-a",
        "--companion", capsule, "--companion-header", header, "--slot-offset", "0x900000")
    for name in ("vmlinux.bin", "vmlinux.bin.gz", "round27-init", "gkd350.config", "uinput.ko"):
        shutil.copyfile(kernel / name, args.output / name)
    shutil.copyfile(capsule, args.output / capsule.name)
    shutil.copyfile(header, args.output / header.name)
    files = sorted(path for path in args.output.iterdir() if path.is_file())
    (args.output / "SHA256SUMS").write_text("".join(
        hashlib.sha256(path.read_bytes()).hexdigest() + "  " + path.name + "\n" for path in files))
    print("GKD_MINIMAL_A_BUILD=PASS output=" + str(args.output), flush=True)

if __name__ == "__main__":
    main()
