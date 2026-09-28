#!/usr/bin/env python3
"""Build the launcher and FPS interposer against the exact verified P1 ABI."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

PROJECT = Path(__file__).resolve().parents[3]
APP = PROJECT / "system/application-core"
LIBRARIES = ("/lib/ld-uClibc.so.0", "/lib/libc.so.0", "/lib/libdl.so.0",
             "/usr/lib/libopk.so.1", "/usr/lib/libini.so.0", "/usr/lib/libz.so.1")
SDL_LIBRARY = "/usr/lib/libSDL-1.2.so.0"
IMAGE = "local/c-builder:2026.08.02-kernel"
IMAGE_ID = "sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1"

def all_manifest():
    return json.loads((APP / "config/minimal-a-manifest.json").read_text())["dependencies"]

def manifest():
    items = all_manifest()
    found = {item["path"]: item for item in items if item["path"] in LIBRARIES}
    if set(found) != set(LIBRARIES):
        raise ValueError("launcher P1 dependency contract missing")
    return found

def sdl_manifest():
    found = [item for item in all_manifest() if item["path"] == SDL_LIBRARY]
    if len(found) != 1:
        raise ValueError("FPS SDL identity contract missing")
    return found[0]

def elf_dependencies(elf):
    text = subprocess.check_output(["readelf", "-l", "-d", str(elf)], text=True)
    if re.search(r"\((?:RPATH|RUNPATH)\)", text):
        raise ValueError("ELF must use the verified application root: " + str(elf))
    names = re.findall(r"\(NEEDED\).*\[([^]]+)\]", text)
    interpreters = re.findall(r"Requesting program interpreter: ([^]]+)\]", text)
    return names, interpreters

def verify(binary, root):
    root = root.resolve()
    items = manifest()
    for item in items.values():
        target = root / item["resolved"].lstrip("/")
        alias = root / item["path"].lstrip("/")
        if (not target.resolve().is_relative_to(root) or
            alias.resolve() != target.resolve() or
            hashlib.sha256(target.read_bytes()).hexdigest() != item["sha256"]):
            raise ValueError("launcher P1 dependency drift: " + item["path"])
    for elf in [binary] + [root / item["resolved"].lstrip("/") for item in items.values()]:
        names, interpreters = elf_dependencies(elf)
        for name in names + interpreters:
            matches = [key for key in items if key == name or Path(key).name == name]
            if len(matches) != 1:
                raise ValueError("unverified application ELF dependency: " + name)
    print("GKD_LAUNCHER_P1_CLOSURE=PASS")

def dynamic_symbols(path, defined):
    option = "--defined-only" if defined else "--undefined-only"
    text = subprocess.check_output(["nm", "-D", option, str(path)], text=True)
    symbols = set()
    for line in text.splitlines():
        fields = line.split()
        if fields and (defined or len(fields) >= 2 and fields[-2] == "U"):
            symbols.add(fields[-1].split("@", 1)[0])
    return symbols

def verify_interposer(binary, root):
    names, interpreters = elf_dependencies(binary)
    if set(names) != {"libdl.so.0", "libc.so.0"} or interpreters:
        raise ValueError("FPS interposer dependency contract drift: " + repr(names))
    items = manifest()
    providers = set()
    for path in ("/lib/libdl.so.0", "/lib/libc.so.0", "/lib/ld-uClibc.so.0"):
        providers |= dynamic_symbols(root / items[path]["resolved"].lstrip("/"), True)
    missing = dynamic_symbols(binary, False) - providers
    if missing:
        raise ValueError("FPS interposer unresolved target symbols: " + repr(sorted(missing)))
    undefined = dynamic_symbols(binary, False)
    if "dlsym" not in undefined or not ({"pthread_atfork", "__register_atfork"} & undefined):
        raise ValueError("FPS interposer forwarding/fork closure missing")
    print("GKD_FPS_INTERPOSER_CLOSURE=PASS needed=libdl.so.0,libc.so.0")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--p1", type=Path)
    parser.add_argument("--p1-sha256")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--verify-binary", type=Path)
    parser.add_argument("--verify-interposer", type=Path)
    parser.add_argument("--libraries", type=Path)
    args = parser.parse_args()
    if args.verify_binary:
        if not args.libraries:
            parser.error("--libraries required")
        verify(args.verify_binary, args.libraries)
        if args.verify_interposer:
            verify_interposer(args.verify_interposer, args.libraries)
        return
    if not args.p1 or not args.p1_sha256 or not args.output:
        parser.error("--p1 --p1-sha256 --output required")
    out = args.output
    if out.exists() or not out.is_absolute() or not out.is_relative_to("/tmp/gkd-mini-public"):
        raise ValueError("new NUC temporary output required")
    if hashlib.sha256(args.p1.read_bytes()).hexdigest() != args.p1_sha256:
        raise ValueError("P1 drift")
    if subprocess.check_output(["docker", "image", "inspect", IMAGE, "--format", "{{.Id}}"],
                               text=True).strip() != IMAGE_ID:
        raise ValueError("compiler image drift")
    out.mkdir(mode=0o700)
    root = out / "p1-root"
    for item in manifest().values():
        target = root / item["resolved"].lstrip("/")
        target.parent.mkdir(parents=True, exist_ok=True)
        if " " in str(target) or " " in str(args.p1):
            raise ValueError("unsupported debugfs path")
        subprocess.run(["debugfs", "-R", "dump " + item["resolved"] + " " + str(target),
                        str(args.p1)], check=True, capture_output=True)
        if hashlib.sha256(target.read_bytes()).hexdigest() != item["sha256"]:
            raise ValueError("P1 extracted library drift")
        alias = root / item["path"].lstrip("/")
        if alias != target:
            alias.symlink_to(target.name)
    sdl = sdl_manifest()
    sdl_target = root / sdl["resolved"].lstrip("/")
    sdl_target.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["debugfs", "-R", "dump " + sdl["resolved"] + " " + str(sdl_target),
                    str(args.p1)], check=True, capture_output=True)
    if hashlib.sha256(sdl_target.read_bytes()).hexdigest() != sdl["sha256"]:
        raise ValueError("FPS SDL extracted library drift")
    sdl_alias = root / sdl["path"].lstrip("/")
    if sdl_alias != sdl_target:
        sdl_alias.symlink_to(sdl_target.name)
    sdl_hash = sdl["sha256"]
    script = r"""
set -eu
app=/src/system/application-core
ui=/src/system/ui-core
update=/src/system/rc33-system-update/source
cc=/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-gcc.br_real
strip=/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-strip
"$cc" -std=gnu99 -Os -Wall -Wextra -Werror -fPIC -shared -fvisibility=hidden  -ffunction-sections -fdata-sections -I"$app/include"  "$app/source/gkd-fps-present.c"  -L/out/p1-root/lib -Wl,-rpath-link,/out/p1-root/lib  -Wl,--no-as-needed -Wl,-l:libdl.so.0 -Wl,-l:libc.so.0  -Wl,-z,defs -Wl,--gc-sections -Wl,--build-id=none  -Wl,-soname,libgkd-fps-present.so -o /out/libgkd-fps-present.so
"$strip" --strip-unneeded /out/libgkd-fps-present.so
interposer_hash=$(sha256sum /out/libgkd-fps-present.so | cut -d" " -f1)
cat > /out/gkd-app-fps-build.generated.h <<EOF
#ifndef GKD_APP_FPS_BUILD_GENERATED_H
#define GKD_APP_FPS_BUILD_GENERATED_H
#define GKD_APP_FPS_SDL_SHA256 "@SDL_SHA@"
#define GKD_APP_FPS_INTERPOSER_SHA256 "$interposer_hash"
#endif
EOF
"$cc" -std=gnu99 -Os -Wall -Wextra -Werror -Wno-unused-function -DGKD_APP_GAME_NO_MAIN -DGKD_APPLICATION_UI=1  -ffunction-sections -fdata-sections -I/out -I"$app/include" -I"$ui/include" -I"$update"  -I"$app/third_party/libopk-5cb5230"  "$app/source/gkd-app-launcher.c" "$app/source/gkd-opk-plan.c" "$app/source/gkd-app-payload.c" "$app/source/gkd-app-orientation.c"  "$app/source/gkd-app-menu-launch.c" "$app/source/gkd-app-menu-config.c" "$app/source/gkd-app-settings.c" "$app/source/gkd-app-fps-launch.c" "$app/source/gkd-app-fps-gate.c" "$update/gkd-update-sha256.c"  "$app/source/gkd-app-game-control.c" "$app/source/gkd-app-loop.c" "$app/source/gkd-app-game.c" "$app/source/gkd-app-display.c"  "$ui/source/gkd-input-owner.c" "$ui/source/gkd-menu-guard.c"  -L/out/p1-root/usr/lib -L/out/p1-root/lib -Wl,-rpath-link,/out/p1-root/usr/lib -Wl,-rpath-link,/out/p1-root/lib  -Wl,--no-as-needed -Wl,-l:libopk.so.1 -Wl,-l:libini.so.0 -Wl,-l:libz.so.1 -Wl,-l:libc.so.0  -Wl,--as-needed -Wl,--gc-sections -Wl,--build-id=none -o /out/gkd-app-launcher
"$strip" /out/gkd-app-launcher
""".replace("@SDL_SHA@", sdl_hash)
    (out / "build.sh").write_text(script)
    import os
    subprocess.run(["docker", "run", "--rm", "--network", "none", "--user",
                    str(os.getuid()) + ":" + str(os.getgid()),
                    "-v", str(PROJECT) + ":/src:ro", "-v", str(out) + ":/out:rw",
                    "-v", "/srv/c-builder/toolchains/gkdmini-gcw0-d28:/opt/toolchain:ro",
                    IMAGE, "sh", "/out/build.sh"], check=True)
    binary = out / "gkd-app-launcher"
    interposer = out / "libgkd-fps-present.so"
    verify(binary, root)
    verify_interposer(interposer, root)
    files = (binary, interposer, out / "gkd-app-fps-build.generated.h")
    (out / "SHA256SUMS").write_text("".join(
        hashlib.sha256(item.read_bytes()).hexdigest() + "  " + item.name + "\n"
        for item in files))
    print("GKD_LAUNCHER_BUILD=PASS bytes=" + str(binary.stat().st_size) +
          " fps_interposer_bytes=" + str(interposer.stat().st_size))

if __name__ == "__main__":
    main()
