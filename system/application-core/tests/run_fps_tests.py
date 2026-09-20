#!/usr/bin/env python3
"""Offline host/sanitizer tests for the production generic FPS lane."""
import argparse
import csv
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

LANE = Path(__file__).resolve().parents[1]
PROJECT = Path("/") if LANE == Path("/lane") else LANE.parents[1]
IMAGE = "local/c-builder:2026.08.02-kernel"
IMAGE_ID = "sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1"

def run(command, **kwargs):
    return subprocess.run([str(x) for x in command], check=True, **kwargs)

def compile_inside(out):
    app = Path("/lane")
    update = Path("/update")
    (out/"gkd-app-fps-build.generated.h").write_text(
        '#define GKD_APP_FPS_SDL_SHA256 "' + "0"*64 + '"\n'
        '#define GKD_APP_FPS_INTERPOSER_SHA256 "' + "0"*64 + '"\n')
    for name, flags in (("normal", []),
                        ("san", ["-fsanitize=address,undefined",
                                 "-fno-sanitize-recover=all"])):
        common = ["gcc", "-std=gnu99", "-O2", "-Wall", "-Wextra", "-Werror", *flags]
        run([*common, "-fPIC", "-shared", app/"tests/fps_fake_sdl.c",
             "-o", out/("libfake-sdl-" + name + ".so")])
        run([*common, "-fPIC", "-shared", app/"tests/fps_unrelated_preload.c",
             "-o", out/("libunrelated-" + name + ".so")])
        run([*common, "-fPIC", "-shared", "-fvisibility=hidden", "-I"+str(app/"include"),
             app/"source/gkd-fps-present.c", "-ldl", "-pthread", "-Wl,-z,defs",
             "-o", out/("libgkd-fps-present-" + name + ".so")])
        run([*common, "-I"+str(app/"include"), app/"tests/fps_present_fixture.c",
             "-L"+str(out), "-l:libfake-sdl-" + name + ".so", "-pthread",
             "-o", out/("fps-present-" + name)])
        run([*common, app/"tests/fps_non_sdl.c", "-o", out/("fps-nonsdl-" + name)])
        run([*common, "-DGKD_APP_FPS_TEST_AUTH", "-I"+str(app/"include"),
             app/"source/gkd-app-fps.c", app/"tests/fps_broker_fixture.c",
             "-o", out/("fps-broker-" + name)])
        run([*common, "-DGKD_APP_FPS_TESTING",
             '-DGKD_APP_FPS_SOCKET="/tmp/gkd-fps-nonblock.sock"',
             "-I"+str(app/"include"), "-I"+str(out), app/"source/gkd-app-fps-launch.c",
             app/"tests/fps_launch_nonblock_fixture.c", "-Wl,--wrap=syscall",
             "-o", out/("fps-launch-nonblock-" + name)])
        run([*common, "-I"+str(app/"include"), "-I"+str(update),
             app/"source/gkd-app-fps-gate.c", update/"gkd-update-sha256.c",
             app/"tests/fps_gate_probe.c", "-o", out/("fps-gate-probe-" + name)])

def present_inside(out, variant):
    env = os.environ.copy()
    suffix = "normal" if variant == "normal" else "san"
    env.update({
        "LD_LIBRARY_PATH": str(out),
        "GKD_TEST_PRELOAD": str(out/("libgkd-fps-present-" + suffix + ".so")),
        "GKD_TEST_FIXTURE": str(out/("fps-present-" + suffix)),
        "GKD_TEST_NOSDL": str(out/("fps-nonsdl-" + suffix)),
        "GKD_TEST_UNRELATED": str(out/("libunrelated-" + suffix + ".so")),
    })
    if variant == "san":
        env["GKD_SANITIZER_PRELOAD"] = subprocess.check_output(
            ["gcc", "-print-file-name=libasan.so"], text=True).strip()
        env["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=1"
        env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    result = run(["python3", "/lane/tests/run_fps_present_tests.py"],
                 env=env, text=True, capture_output=True)
    (out/("present-" + variant + ".log")).write_text(result.stdout + result.stderr)
    print(result.stdout.strip())

def broker_inside(out, variant):
    env = os.environ.copy()
    if variant == "san":
        env["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=1"
        env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    result = run([out/("fps-broker-" + ("normal" if variant == "normal" else "san"))],
                 env=env, text=True, capture_output=True)
    (out/("broker-" + variant + ".log")).write_text(result.stdout + result.stderr)
    print(result.stdout.strip())

def gate_inside(out, variant):
    root = Path("/tmp/gkd-fps-gate-root")
    if root.exists():
        shutil.rmtree(root)
    (root/"usr/lib").mkdir(parents=True)
    (root/"var/run/gkd-app").mkdir(parents=True)
    sdl = root/"usr/lib/libSDL-1.2.so.0.11.4"
    interposer = root/"var/run/gkd-app/libgkd-fps-present.so"
    shutil.copyfile("/target-build/p1-root/usr/lib/libSDL-1.2.so.0.11.4", sdl)
    shutil.copyfile("/target-build/libgkd-fps-present.so", interposer)
    os.chmod(sdl, 0o555);os.chmod(interposer, 0o555)
    (root/"usr/lib/libSDL-1.2.so.0").symlink_to(sdl.name)
    sdl_hash = hashlib.sha256(sdl.read_bytes()).hexdigest()
    interposer_hash = hashlib.sha256(interposer.read_bytes()).hexdigest()
    probe = out/("fps-gate-probe-" + variant)
    gate_environment=os.environ.copy()
    if variant=="san":
        gate_environment["ASAN_OPTIONS"]="detect_leaks=1:abort_on_error=1"
        gate_environment["UBSAN_OPTIONS"]="halt_on_error=1:print_stacktrace=1"
    def decision(path, ld="-", ihash=interposer_hash):
        result = run([probe, path, Path(path).parent, ld,
                      root/"usr/lib/libSDL-1.2.so.0", sdl,
                      sdl_hash, interposer, ihash], text=True, capture_output=True,
                     env=gate_environment)
        return int(result.stdout.strip())
    counts = {"inject": 0, "unsupported": 0}
    rpath_candidate = None
    with open("/table", encoding="utf-8") as source:
        for row in csv.DictReader(source, delimiter="\t"):
            expected = 1 if row["decision"] == "inject" else 0
            path = Path("/gate-tree")/row["relative_path"]
            actual = decision(path)
            assert actual == expected, (row["relative_path"], actual, expected)
            counts[row["decision"]] += 1
            if path.name == "mgba":
                rpath_candidate = path
    for relative in ("3do-test/3doh", "MD32X/picodrive", "vbemu/vb.elf"):
        assert decision(Path("/gate-tree")/relative) == 0, relative
    representative = Path("/gate-tree/FBA-0-2-97-35-2021-0309/fbasdl.dge")
    began=time.perf_counter()
    for unused in range(50):
        assert decision(representative)==1
    host_average_ms=(time.perf_counter()-began)*1000/50
    assert decision(representative, "/untrusted") == 0
    assert decision(representative, "-", "0"*64) == 0
    assert decision("/bin/true") == 0
    assert rpath_candidate is not None
    local = root/"local-search"
    local.mkdir()
    local_executable = local/"mgba"
    shutil.copyfile(rpath_candidate, local_executable)
    os.chmod(local_executable, 0o555)
    (local/"libSDL-1.2.so.0").touch()
    assert decision(local_executable) == 0
    print(("GKD_FPS_GATE=PASS variant=" + variant +
           " inject={inject} unsupported={unsupported} static=3 "
           "bad-elf=1 loader-env=1 local-rpath-shadow=1 pinned-hash=1 "
           "host_average_ms={:.3f}").format(host_average_ms,**counts))

def inside(out):
    compile_inside(out)
    for variant in ("normal", "san"):
        present_inside(out, variant)
        broker_inside(out, variant)
        result=run([out/("fps-launch-nonblock-" + ("normal" if variant == "normal" else "san"))],
                   text=True,capture_output=True)
        (out/("launch-nonblock-" + variant + ".log")).write_text(result.stdout+result.stderr)
        print(result.stdout.strip())
    gate_inside(out,"normal")
    gate_inside(out,"san")
    sums = []
    for path in sorted(out.iterdir()):
        if path.is_file():
            sums.append(hashlib.sha256(path.read_bytes()).hexdigest() + "  " + path.name)
    (out/"SHA256SUMS").write_text("\n".join(sums) + "\n")
    print("GKD_FPS_TESTS=PASS normal=1 asan-ubsan=1")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--target-build", type=Path, required=True)
    parser.add_argument("--gate-tree", type=Path, required=True)
    parser.add_argument("--gate-table", type=Path, required=True)
    parser.add_argument("--inside", action="store_true")
    args = parser.parse_args()
    if args.inside:
        inside(args.output)
        return
    out = args.output
    if out.exists() or not out.is_absolute() or not out.is_relative_to("/tmp/gkd-mini-public"):
        raise ValueError("new NUC temporary output required")
    out.mkdir(mode=0o700)
    actual = subprocess.check_output(
        ["docker", "image", "inspect", IMAGE, "--format", "{{.Id}}"], text=True).strip()
    if actual != IMAGE_ID:
        raise ValueError("compiler image drift")
    for path in (args.target_build, args.gate_tree, args.gate_table):
        if not path.exists():
            raise ValueError("missing FPS test input: " + str(path))
    run(["docker", "run", "--rm", "--network", "none",
         "-v", str(LANE)+":/lane:ro",
         "-v", str(PROJECT/"system/rc33-system-update/source")+":/update:ro",
         "-v", str(out)+":/out:rw",
         "-v", str(args.target_build.resolve())+":/target-build:ro",
         "-v", str(args.gate_tree.resolve())+":/gate-tree:ro",
         "-v", str(args.gate_table.resolve())+":/table:ro",
         IMAGE, "python3", "/lane/tests/run_fps_tests.py", "/out",
         "--target-build", "/target-build", "--gate-tree", "/gate-tree",
         "--gate-table", "/table", "--inside"])

if __name__ == "__main__":
    main()
