#!/usr/bin/env python3
"""Build the fixed pre-P1 recovery capsule and its compile-time identity."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


FIXED_CAPSULE_BYTES = 0x4D0000
FIXED_CAPSULE_OFFSET = 0xF00000
DEDICATED_R_CAPSULE_OFFSET = 0x6A0000
APPLICATION_A_CAPSULE_OFFSET = 0xCA0000
FIXED_TIME = 1785448800
LEGACY_RECOVERY_UI = "/usr/lib/gkd-usb-round64/gkd-round64-rescue-ui"
DEDICATED_REPLACED_FILES = {
    LEGACY_RECOVERY_UI,
    "/etc/gkd-mini/gdkmini.conf",
    "/etc/init.d/S94gkd-screenshot",
    "/usr/sbin/gkd-screenshot",
}
DEDICATED_UINPUT_SHA256 = (
    "64260225444c49f9940918a4ca49e83aa47bfa2f0315626fbb3324abc61bad51"
)
DEDICATED_KMOD_SHA256 = (
    "3316eda58f803e5c1eda728efc93bbd612c832d843ced81c709871dc44f61660"
)
DEDICATED_ZLIB_SHA256 = (
    "5a299afa62d54488af1f5a43d1a4f186f7f862441036688413f159623d1fcc49"
)
DEDICATED_BATTERY_SHA256 = (
    "8a42ade6d03828757f6353fcda19f579710eb39aac9b0a800485eb8ab4479933"
)
DEDICATED_EXTRA_LINKS = {
    "/bin/usleep": "busybox",
    "/sbin/insmod": "../usr/bin/kmod",
    "/usr/lib/libz.so.1": "libz.so.1.2.11",
}
APPLICATION_EXTRA_LINKS = {
    "/bin/dd": "busybox",
    "/bin/ip": "busybox",
    "/bin/ping": "busybox",
    "/bin/wc": "busybox",
    "/bin/tr": "busybox",
    "/bin/sha256sum": "busybox",
}
# External commands used by gkd-application-start and shared gkd-config.
# This is an image-local closure check, never a host PATH lookup. ELF closure
# alone cannot detect missing BusyBox applet entry points.
APPLICATION_COMMANDS = (
    "/bin/usleep", "/usr/sbin/dropbear", "/usr/lib/gkd-usb-round64/ssh-rndis-hook",
    "/bin/dd", "/bin/stat", "/bin/sh", "/bin/busybox", "/bin/mkdir", "/bin/awk", "/bin/sed",
    "/bin/wc", "/bin/tr", "/bin/sha256sum", "/bin/cp", "/bin/chmod",
    "/bin/mv", "/bin/ln", "/bin/rm", "/bin/rmdir", "/bin/readlink",
    "/bin/grep", "/bin/cat", "/usr/sbin/gkd-config",
    "/usr/sbin/gkd-config-rename", "/usr/sbin/gkd-update-request-tool",
    "/usr/sbin/gkd-update-engine", "/usr/sbin/gkd-app-host",
    "/usr/sbin/gkd-controls", "/usr/sbin/gkd-controls-start",
    "/usr/sbin/gkd-application-service", "/usr/sbin/gkd-application-menu",
    "/usr/sbin/gkd-app-management", "/usr/sbin/gkd-app-card-guard", "/usr/sbin/gkd-app-game",
    "/usr/libexec/gkd-app-launcher", "/usr/libexec/gkd-simplemenu-opkrun",
    "/usr/sbin/gkd-app-config-store", "/usr/sbin/gkd-application-config", "/usr/sbin/gkd-application-network",
    "/usr/sbin/gkd-application-update", "/usr/sbin/gkd-update-prepare", "/usr/sbin/gkd-update-coordinator",
    "/usr/sbin/gkd-application-usb", "/usr/sbin/gkd-application-bootstrap-usb",
    "/usr/sbin/gkd-screenshot", "/bin/ip", "/bin/ping", "/bin/sync",
    "/bin/blockdev", "/bin/mount", "/bin/umount", "/bin/id", "/bin/sleep", "/bin/cmp",
)


def application_payload_profile(manifest: dict, profile_path: Path | None = None) -> dict:
    if profile_path is None:
        profile_path = Path(__file__).resolve().parents[3] / "system/application-core/config/ram-payload-profile.json"
    profile = json.loads(profile_path.read_text())
    keys = {"retained_base_regular", "excluded_base_regular", "replaced_regular"}
    if set(profile) != keys:
        raise SystemExit("GKD_APP_CAPSULE=BLOCKED payload-profile-fields")
    for key in keys:
        rows = profile[key]
        if not isinstance(rows, list) or not all(isinstance(x, str) and x.startswith("/") for x in rows) or len(rows) != len(set(rows)):
            raise SystemExit("GKD_APP_CAPSULE=BLOCKED payload-profile-paths")
    retained, excluded, replaced = (set(profile[key]) for key in
        ("retained_base_regular", "excluded_base_regular", "replaced_regular"))
    actual = [item["destination"] for item in manifest["p1_regular_files"]]
    if len(actual) != len(set(actual)) or retained & excluded or \
            retained | excluded != set(actual) or not replaced <= excluded:
        raise SystemExit("GKD_APP_CAPSULE=BLOCKED payload-profile-inventory")
    return profile


def verify_application_payload(root: Path, profile: dict) -> None:
    for name in set(profile["excluded_base_regular"]) - set(profile["replaced_regular"]):
        if (root / name.lstrip("/")).exists() or (root / name.lstrip("/")).is_symlink():
            raise SystemExit("GKD_APP_CAPSULE=BLOCKED obsolete-backend " + name)
    for name in profile["retained_base_regular"] + profile["replaced_regular"]:
        if not (root / name.lstrip("/")).is_file():
            raise SystemExit("GKD_APP_CAPSULE=BLOCKED missing-retained " + name)


def verify_application_commands(root: Path) -> None:
    root = root.resolve()
    for command in APPLICATION_COMMANDS:
        path = root / command.lstrip("/")
        try:
            target = path.resolve(strict=True)
            valid = target.is_relative_to(root) and target.is_file() and \
                bool(target.stat().st_mode & 0o111)
        except (OSError, RuntimeError):
            valid = False
        if not valid:
            raise SystemExit("GKD_APP_CAPSULE=BLOCKED missing-command " + command)


def verify_elf_dependencies(root: Path, application_launcher: Path | None = None) -> None:
    """Resolve target ELF dependencies inside the image, never on the host."""
    root = root.resolve()
    launcher = root / "usr/libexec/gkd-app-launcher"
    if application_launcher is not None:
        verifier = Path(__file__).resolve().parents[3] / "system/application-core/scripts/build-launcher.py"
        subprocess.run(["python3", str(verifier), "--verify-binary", str(launcher),
                        "--libraries", str(application_launcher / "p1-root")], check=True)
    for path in sorted(root.rglob("*")):
        if application_launcher is not None and path == launcher:
            continue
        if path.is_symlink() or not path.is_file():
            continue
        with path.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                continue
        result = subprocess.run(
            ["readelf", "-l", "-d", str(path)], check=True,
            capture_output=True, text=True, timeout=30,
        )
        needed = re.findall(r"\(NEEDED\).*\[([^]]+)\]", result.stdout)
        interpreter = re.findall(
            r"Requesting program interpreter: ([^]]+)\]", result.stdout
        )
        for dependency in needed + interpreter:
            candidates = (
                [root / safe_relative(dependency)] if dependency.startswith("/")
                else [root / directory / dependency for directory in ("lib", "usr/lib")]
            )
            if not any(
                candidate.resolve().is_relative_to(root) and candidate.is_file()
                for candidate in candidates
            ):
                raise SystemExit(
                    "GKDSU_RECOVERY_CAPSULE=BLOCKED missing-elf-dependency "
                    + str(path.relative_to(root)) + ":" + dependency
                )


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def c_hash(value: str) -> str:
    return ",".join("0x" + value[index:index + 2] for index in range(0, 64, 2))


def c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def safe_relative(value: str) -> Path:
    if not value.startswith("/") or ".." in Path(value).parts:
        raise SystemExit("GKDSU_RECOVERY_CAPSULE=BLOCKED unsafe-path")
    return Path(value.lstrip("/"))


def prepared_directories(loader: Path) -> set[str]:
    source = loader.read_text(encoding="utf-8")
    start = source.index("static int r64m_prepare_directories(void)")
    marker = "directories[] = {"
    start = source.index(marker, start) + len(marker)
    end = source.index("\n\t};", start)
    block = source[start:end]
    definitions = dict(re.findall(
        r'^#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+"([^"]+)"',
        source,
        re.MULTILINE,
    ))
    directories = set()
    for token in re.findall(r'\{\s*([^,]+)\s*,\s*0?[0-7]+\s*\}', block):
        token = token.strip()
        if token.startswith('"'):
            directories.add(json.loads(token))
        elif token in definitions:
            directories.add(definitions[token])
        else:
            raise SystemExit(
                "GKDSU_RECOVERY_CAPSULE=BLOCKED unknown-directory-token " + token
            )
    return directories


def verify_copy_destination_parents(loader: Path, destinations: list[str]) -> None:
    directories = prepared_directories(loader)
    missing = sorted({
        str(Path(destination).parent)
        for destination in destinations
        if str(Path(destination).parent) not in directories
    })
    if missing:
        raise SystemExit(
            "GKDSU_RECOVERY_CAPSULE=BLOCKED unprepared-parent " + ",".join(missing)
        )


def install_file(source: Path, root: Path, path: str, mode: int) -> dict:
    destination = root / safe_relative(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    os.chmod(destination, mode)
    os.utime(destination, (FIXED_TIME, FIXED_TIME), follow_symlinks=False)
    return {
        "source": path,
        "destination": path,
        "bytes": destination.stat().st_size,
        "source_mode": f"{mode:04o}",
        "destination_mode": f"{mode:04o}",
        "sha256": sha256(destination),
    }


def install_link(root: Path, path: str, target: str) -> dict:
    destination = root / safe_relative(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.symlink_to(target)
    return {
        "source": path,
        "destination": path,
        "target": target,
        "source_required": True,
    }


def manifest_links_for_layout(manifest: dict, layout: str) -> list[dict]:
    links = manifest["p1_symlinks"]
    if layout in ("dedicated-r", "application-a"):
        return list(links)
    return [item for item in links if item["source_required"]]


def parse_squashfs_symlinks(listing: str) -> dict[str, str]:
    links: dict[str, str] = {}
    marker = " squashfs-root"
    for line in listing.splitlines():
        position = line.find(marker)
        if position < 0:
            continue
        entry = line[position + len(marker):]
        if " -> " not in entry:
            continue
        path, target = entry.split(" -> ", 1)
        if not path.startswith("/") or path in links:
            raise SystemExit("GKDSU_RECOVERY_CAPSULE=BLOCKED symlink-listing")
        links[path] = target
    return links


def verify_capsule_symlinks(capsule: Path, expected_links: list[dict]) -> None:
    result = subprocess.run(
        ["unsquashfs", "-ll", str(capsule)],
        check=False,
        capture_output=True,
        text=True,
        timeout=30,
    )
    if result.returncode:
        raise SystemExit("GKDSU_RECOVERY_CAPSULE=BLOCKED symlink-readback")
    actual = parse_squashfs_symlinks(result.stdout)
    expected: dict[str, str] = {}
    for item in expected_links:
        path = item["destination"]
        if path in expected or not path.startswith("/"):
            raise SystemExit("GKDSU_RECOVERY_CAPSULE=BLOCKED symlink-contract")
        expected[path] = item["target"]
    drift = sorted(
        path for path, target in expected.items()
        if actual.get(path) != target
    )
    if drift:
        raise SystemExit(
            "GKDSU_RECOVERY_CAPSULE=BLOCKED symlink-drift " + ",".join(drift)
        )


def render_header(
    capsule_hash: str,
    capsule_offset: int,
    capsule_bytes: int,
    files: list[dict],
    links: list[dict],
) -> str:
    file_rows = [
        "\t{%s,%s,%d,%s,%s,{%s}}" % (
            c_string(item["source"]), c_string(item["destination"]),
            item["bytes"], item["source_mode"], item["destination_mode"],
            c_hash(item["sha256"]),
        )
        for item in files
    ]
    link_rows = [
        "\t{%s,%s,%s,1}" % (
            c_string(item["source"]), c_string(item["destination"]),
            c_string(item["target"]),
        )
        for item in links
    ]
    return "\n".join((
        "/* Generated sealed pre-P1 recovery capsule identity; do not edit. */",
        "#ifndef GKD_ROUND84_RECOVERY_CAPSULE_GENERATED_H",
        "#define GKD_ROUND84_RECOVERY_CAPSULE_GENERATED_H",
        f"#define R84_RECOVERY_CAPSULE_OFFSET ((off_t)0x{capsule_offset:x})",
        f"#define R84_RECOVERY_CAPSULE_BYTES 0x{capsule_bytes:x}UL",
        "static const unsigned char r84_recovery_capsule_sha256[32] =",
        "\t{%s};" % c_hash(capsule_hash),
        "static const struct r64m_file r84_recovery_extra_files[] = {",
        ",\n".join(file_rows), "};",
        "static const struct r64m_link r84_recovery_extra_links[] = {",
        ",\n".join(link_rows), "};",
        "static const struct r64m_payload r84_recovery_extra_payload = {",
        "\tr84_recovery_extra_files, sizeof(r84_recovery_extra_files) / sizeof(r84_recovery_extra_files[0]),",
        "\tr84_recovery_extra_links, sizeof(r84_recovery_extra_links) / sizeof(r84_recovery_extra_links[0])",
        "};", "#endif", "",
    ))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--verified-root", type=Path, required=True)
    parser.add_argument("--components", type=Path, required=True)
    parser.add_argument("--recovery-script", type=Path, required=True)
    parser.add_argument("--mass-storage-script", type=Path, required=True)
    parser.add_argument("--recovery-usb-script", type=Path, required=True)
    parser.add_argument("--ui-binary", type=Path, required=True)
    parser.add_argument("--input-binary", type=Path, required=True)
    parser.add_argument("--input-init", type=Path, required=True)
    parser.add_argument("--screenshot-binary", type=Path, required=True)
    parser.add_argument("--system-config", type=Path, required=True)
    parser.add_argument("--ui-config", type=Path, required=True)
    parser.add_argument("--ui-font", type=Path, required=True)
    parser.add_argument("--temporary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--loader", type=Path, required=True)
    parser.add_argument(
        "--layout", choices=("fixed", "dedicated-r", "application-a"), default="fixed"
    )
    parser.add_argument("--application-components", type=Path)
    parser.add_argument("--controls-components", type=Path)
    parser.add_argument("--service-components", type=Path)
    parser.add_argument("--adapter-components", type=Path)
    parser.add_argument("--menu-components", type=Path)
    parser.add_argument("--stat-binary", type=Path)
    parser.add_argument("--application-schema", type=Path)
    arguments = parser.parse_args()
    if any(path.exists() for path in (arguments.temporary, arguments.output, arguments.header)):
        raise SystemExit("GKDSU_RECOVERY_CAPSULE=BLOCKED output-exists")
    manifest = json.loads(arguments.manifest.read_text(encoding="utf-8"))
    root = arguments.temporary
    root.mkdir(parents=True)

    base_files = manifest["p1_regular_files"]
    application_profile = application_payload_profile(manifest) if arguments.layout == "application-a" else None
    replacements = application_profile["excluded_base_regular"] if application_profile else DEDICATED_REPLACED_FILES
    if arguments.layout in ("dedicated-r", "application-a"):
        for destination in replacements:
            if sum(item["destination"] == destination for item in base_files) != 1:
                raise SystemExit("GKDSU_RECOVERY_CAPSULE=BLOCKED replacement-contract")
        base_files = [
            item for item in base_files
            if item["destination"] not in replacements
        ]
    for item in base_files:
        source = arguments.verified_root / safe_relative(item["source"])
        if sha256(source) != item["sha256"]:
            raise SystemExit("GKDSU_RECOVERY_CAPSULE=BLOCKED verified-root-drift")
        install_file(source, root, item["source"], int(item["source_mode"], 8))
    layout_links = manifest_links_for_layout(manifest, arguments.layout)
    for item in layout_links:
        install_link(root, item["source"], item["target"])

    extras = [
        (arguments.components / "gkd-update-runtime-static", "/usr/sbin/gkd-update-runtime", 0o555),
        (arguments.components / "gkd-update-request-tool", "/usr/sbin/gkd-update-request-tool", 0o555),
        (arguments.components / "gkd-update-prepare", "/usr/sbin/gkd-update-prepare", 0o555),
        (arguments.components / "runtime-id", "/etc/gkd-mini/runtime-id", 0o444),
        (arguments.components / "update-public.pem", "/etc/gkd-mini/update-public.pem", 0o444),
        (arguments.components / "libcrypto.so.1.0.0", "/usr/lib/libcrypto.so.1.0.0", 0o444),
        (arguments.components / "libdl-0.9.33.2.so", "/lib/libdl-0.9.33.2.so", 0o555),
        (arguments.recovery_script, "/usr/sbin/gkd-recovery", 0o555),
        (arguments.mass_storage_script, "/usr/sbin/gkd-recovery-mass-storage", 0o555),
        (arguments.ui_binary, "/usr/sbin/gkd-recovery-ui", 0o555),
        (arguments.ui_config, "/etc/gkd-mini/gdkmini.ui.conf", 0o444),
        (arguments.ui_font, "/etc/gkd-mini/fonts/fallback.psf", 0o444),
    ]
    if arguments.layout in ("dedicated-r", "application-a"):
        uinput_module = arguments.verified_root / "usr/lib/gkd-uinput.ko"
        if arguments.layout == "dedicated-r":
            battery = arguments.verified_root / "usr/sbin/gkd-battery-notify"
            if not battery.is_file() or battery.is_symlink() or \
                    sha256(battery) != DEDICATED_BATTERY_SHA256:
                raise SystemExit("GKDSU_RECOVERY_CAPSULE=BLOCKED battery-probe-drift")
            # Isolated R adapter: never change the frozen normal recovery script.
            recovery_text = arguments.recovery_script.read_text(encoding="utf-8")
            marker = '[ "$#" = 1 ] || usage'
            if recovery_text.count(marker) != 1:
                raise SystemExit("GKDSU_RECOVERY_CAPSULE=BLOCKED power-gate-contract")
            power_adapter = arguments.recovery_script.with_name(
                "dedicated-r-power-gate.sh"
            ).read_text(encoding="utf-8")
            update_adapter = arguments.recovery_script.with_name(
                "dedicated-r-update.sh"
            ).read_text(encoding="utf-8")
            r_recovery = root.parent / "dedicated-r-recovery.generated.sh"
            if r_recovery.exists():
                raise SystemExit("GKDSU_RECOVERY_CAPSULE=BLOCKED output-exists")
            r_recovery.write_text(
                recovery_text.replace(marker, power_adapter + "\n" + update_adapter + "\n" + marker),
                encoding="utf-8",
            )
            extras = [(r_recovery if destination == "/usr/sbin/gkd-recovery" else source,
                       destination, mode) for source, destination, mode in extras]
        kmod = arguments.verified_root / "usr/bin/kmod"
        zlib = arguments.verified_root / "usr/lib/libz.so.1.2.11"
        if not uinput_module.is_file() or uinput_module.is_symlink() or \
                sha256(uinput_module) != DEDICATED_UINPUT_SHA256:
            raise SystemExit("GKDSU_RECOVERY_CAPSULE=BLOCKED uinput-drift")
        if not kmod.is_file() or kmod.is_symlink() or \
                sha256(kmod) != DEDICATED_KMOD_SHA256:
            raise SystemExit("GKDSU_RECOVERY_CAPSULE=BLOCKED kmod-drift")
        if not zlib.is_file() or zlib.is_symlink() or \
                sha256(zlib) != DEDICATED_ZLIB_SHA256:
            raise SystemExit("GKDSU_RECOVERY_CAPSULE=BLOCKED zlib-drift")
        extras.append(
            (arguments.recovery_usb_script, "/usr/sbin/gkd-recovery-usb", 0o555)
        )
        if arguments.layout == "dedicated-r":
            extras.append((battery, "/usr/sbin/gkd-battery-notify", 0o555))
        extras.extend((
            (arguments.input_binary, "/usr/sbin/gkd-input", 0o555),
            (arguments.input_init, "/etc/init.d/S95gkd-input", 0o555),
            (arguments.screenshot_binary, "/usr/sbin/gkd-screenshot", 0o555),
            (arguments.system_config, "/etc/gkd-mini/gdkmini.conf", 0o444),
            (uinput_module, "/usr/lib/gkd-uinput.ko", 0o444),
            (kmod, "/usr/bin/kmod", 0o555),
            (zlib, "/usr/lib/libz.so.1.2.11", 0o555),
        ))
    if arguments.layout == "application-a":
        # A owns runtime actions; retain only the shared cold management bootstrap.
        removed = {"/usr/sbin/gkd-recovery", "/usr/sbin/gkd-recovery-ui",
                   "/usr/sbin/gkd-recovery-mass-storage", "/usr/sbin/gkd-recovery-usb",
                   "/etc/gkd-mini/gdkmini.conf", "/usr/sbin/gkd-input",
                   "/usr/sbin/gkd-screenshot"}
        extras = [item for item in extras if item[1] not in removed]
        app = arguments.application_components
        controls = arguments.controls_components
        service = arguments.service_components
        adapters = arguments.adapter_components
        menu = arguments.menu_components
        if any(item is None for item in (app, controls, service, adapters, menu, arguments.stat_binary, arguments.application_schema)):
            raise SystemExit("GKD_APP_CAPSULE=BLOCKED missing-components")
        project = Path(__file__).resolve().parents[3]
        bootstrap = root.parent / "gkd-application-bootstrap-usb.generated"
        subprocess.run(["python3", str(project / "system/application-core/scripts/derive-usb-bootstrap.py"),
                        str(arguments.recovery_usb_script), str(bootstrap)], check=True)
        extras.extend((
            (arguments.stat_binary, "/bin/stat", 0o555),
            (bootstrap, "/usr/sbin/gkd-application-bootstrap-usb", 0o555),
            (service / "gkd-application-service", "/usr/sbin/gkd-application-service", 0o555),
            (service / "gkd-app-management", "/usr/sbin/gkd-app-management", 0o555),
            (service / "gkd-app-game", "/usr/sbin/gkd-app-game", 0o555),
            (service / "launcher/gkd-app-launcher", "/usr/libexec/gkd-app-launcher", 0o555),
            (service / "launcher/libgkd-fps-present.so", "/usr/lib/libgkd-fps-present.so", 0o555),
            (project / "system/application-core/device/gkd-simplemenu-opkrun", "/usr/libexec/gkd-simplemenu-opkrun", 0o555),
            (service / "gkd-app-config-store", "/usr/sbin/gkd-app-config-store", 0o555),
            (service / "gkd-screenshot", "/usr/sbin/gkd-screenshot", 0o555),
            (service / "gkd-input", "/usr/sbin/gkd-input", 0o555),
            (adapters / "gkd-app-card-guard", "/usr/sbin/gkd-app-card-guard", 0o555),
            (menu / "gkd-application-menu", "/usr/sbin/gkd-application-menu", 0o555),
            (menu / "native-cn.psf", "/etc/gkd-mini/fonts/native-cn.psf", 0o444),
            (menu / "native-cn-12.psf", "/etc/gkd-mini/fonts/native-cn-12.psf", 0o444),
            (project / "system/ui-core/third_party/wenquanyi/LICENSE.txt", "/usr/share/licenses/gkd-native-font/LICENSE.txt", 0o444),
            (project / "system/ui-core/third_party/wenquanyi/SOURCE.json", "/usr/share/licenses/gkd-native-font/SOURCE.json", 0o444),
            (project / "system/application-core/device/gkd-application-update", "/usr/sbin/gkd-application-update", 0o555),
            (project / "system/application-core/device/gkd-application-usb", "/usr/sbin/gkd-application-usb", 0o555),
            (project / "system/application-core/device/gkd-application-config", "/usr/sbin/gkd-application-config", 0o555),
            (project / "system/application-core/device/gkd-application-network", "/usr/sbin/gkd-application-network", 0o555),
            (controls / "gkd-controls", "/usr/sbin/gkd-controls", 0o555),
            (controls / "gkd-controls-start", "/usr/sbin/gkd-controls-start", 0o555),
            (app / "gkd-app-host", "/usr/sbin/gkd-app-host", 0o555),
            (app / "libgkd-sm-present.so", "/usr/lib/libgkd-sm-present.so", 0o555),
            (app / "gkd-application-start", "/usr/sbin/gkd-application-start", 0o555),
            (app / "gkd-config-rename", "/usr/sbin/gkd-config-rename", 0o555),
            (project / "system/config-core/device/gkd-config", "/usr/sbin/gkd-config", 0o555),
            (project / "system/config-core/device/gkd-config-merge.awk", "/usr/lib/gkd-config-merge.awk", 0o444),
            (arguments.application_schema, "/etc/gkd-mini/gdkmini.schema", 0o444),
            (project / "system/application-core/config/input-routing.conf", "/etc/gkd-mini/input-routing.conf", 0o444),
            (project / "system/application-core/config/application.override.conf", "/etc/gkd-mini/application.override.conf", 0o444),
        ))
    extra_files = []
    for source, destination, mode in extras:
        if not source.is_file() or source.is_symlink():
            raise SystemExit("GKDSU_RECOVERY_CAPSULE=BLOCKED missing-extra")
        extra_files.append(install_file(source, root, destination, mode))
    extra_links = [
        install_link(root, "/lib/libdl.so.0", "libdl-0.9.33.2.so"),
        install_link(root, "/usr/sbin/gkd-update-engine", "gkd-update-runtime"),
        install_link(root, "/usr/sbin/gkd-update-coordinator", "gkd-update-runtime"),
    ]
    if arguments.layout in ("dedicated-r", "application-a"):
        extra_links.extend(
            install_link(root, path, target)
            for path, target in DEDICATED_EXTRA_LINKS.items()
        )
    if arguments.layout == "application-a":
        extra_links.extend(
            install_link(root, path, target)
            for path, target in APPLICATION_EXTRA_LINKS.items()
        )
        verify_application_commands(root)
        verify_application_payload(root, application_profile)
    verify_copy_destination_parents(
        arguments.loader,
        [item["destination"] for item in base_files]
        + [item["destination"] for item in manifest["p1_symlinks"]]
        + [item["destination"] for item in extra_files]
        + [item["destination"] for item in extra_links],
    )
    if arguments.layout in ("dedicated-r", "application-a"):
        verify_elf_dependencies(root, arguments.service_components / "launcher"
                                if arguments.layout == "application-a" else None)
    for directory in sorted((path for path in root.rglob("*") if path.is_dir())):
        os.chmod(directory, 0o755)
        os.utime(directory, (FIXED_TIME, FIXED_TIME), follow_symlinks=False)
    os.utime(root, (FIXED_TIME, FIXED_TIME), follow_symlinks=False)

    raw = arguments.output.with_suffix(".squashfs")
    subprocess.run([
        "mksquashfs", str(root), str(raw), "-noappend", "-comp", "gzip",
        "-b", "1048576" if arguments.layout == "application-a" else "131072",
        "-all-root", "-no-exports", "-no-xattrs",
        "-mkfs-time", str(FIXED_TIME), "-all-time", str(FIXED_TIME),
        "-processors", "1", "-no-progress",
    ], check=True)
    raw_bytes = raw.stat().st_size
    if raw_bytes >= FIXED_CAPSULE_BYTES:
        raise SystemExit("GKDSU_RECOVERY_CAPSULE=BLOCKED capacity")
    if arguments.layout == "fixed":
        capsule_offset = FIXED_CAPSULE_OFFSET
        capsule_bytes = FIXED_CAPSULE_BYTES
    else:
        capsule_offset = (APPLICATION_A_CAPSULE_OFFSET if arguments.layout == "application-a"
                          else DEDICATED_R_CAPSULE_OFFSET)
        capsule_bytes = raw_bytes
        if capsule_bytes >= 0x260000:
            raise SystemExit("GKD_APP_CAPSULE=BLOCKED companion-capacity")
    with arguments.output.open("xb") as destination, raw.open("rb") as source:
        shutil.copyfileobj(source, destination)
        destination.write(bytes(capsule_bytes - destination.tell()))
    raw.unlink()
    verify_capsule_symlinks(arguments.output, layout_links + extra_links)
    capsule_hash = sha256(arguments.output)
    arguments.header.write_text(
        render_header(
            capsule_hash, capsule_offset, capsule_bytes, extra_files, extra_links
        ),
        encoding="ascii",
    )
    print(
        "GKDSU_RECOVERY_CAPSULE=PASS layout=%s bytes=%d sha256=%s offset=0x%x" %
        (arguments.layout, capsule_bytes, capsule_hash, capsule_offset)
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
