#!/usr/bin/env python3
import argparse, hashlib, shutil, subprocess, tempfile
from pathlib import Path

FILES = {
    "gkd-update-engine": ("/usr/sbin/gkd-update-engine", "0100555"),
    "gkd-update-prepare": ("/usr/sbin/gkd-update-prepare", "0100555"),
    "gkd-update-request-tool": ("/usr/sbin/gkd-update-request-tool", "0100555"),
    "gkd-system-update": ("/usr/sbin/gkd-system-update", "0100555"),
    "S88gkd-update-trial": ("/etc/init.d/S88gkd-update-trial", "0100555"),
    "runtime-id": ("/etc/gkd-mini/runtime-id", "0100444"),
    "update-public.pem": ("/etc/gkd-mini/update-public.pem", "0100444"),
}

def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(8 << 20), b""): h.update(block)
    return h.hexdigest()

def debug(image, command, check=True):
    result = subprocess.run(["debugfs", "-w", "-R", command, str(image)],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if check and result.returncode: raise SystemExit("GKDSU_P1=BLOCKED debugfs=" + command)
    return result.stdout + result.stderr

p = argparse.ArgumentParser()
p.add_argument("--source", type=Path, required=True); p.add_argument("--source-sha256", required=True)
p.add_argument("--components", type=Path, required=True); p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
if (not a.source.is_file() or sha(a.source) != a.source_sha256 or a.output.exists() or
    any(not (a.components / name).is_file() for name in FILES)):
    raise SystemExit("GKDSU_P1=BLOCKED inputs")
check = subprocess.run(["e2fsck", "-fn", str(a.source)], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True)
if check.returncode not in (0, 1): raise SystemExit("GKDSU_P1=BLOCKED source-fs")
shutil.copyfile(a.source, a.output)
for name, (target, mode) in FILES.items():
    debug(a.output, "stat " + target, check=False)
    debug(a.output, "rm " + target, check=False)
    debug(a.output, f"write {a.components / name} {target}")
    for field, value in (("mode", mode), ("uid", "0"), ("gid", "0")):
        debug(a.output, f"set_inode_field {target} {field} {value}")
repair = subprocess.run(["e2fsck", "-fy", str(a.output)], stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, text=True)
if repair.returncode not in (0, 1): raise SystemExit("GKDSU_P1=BLOCKED target-fs")
for name, (target, _) in FILES.items():
    with tempfile.TemporaryDirectory() as directory:
        readback = Path(directory) / name
        result = subprocess.run(["debugfs", "-R", f"dump {target} {readback}", str(a.output)],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if result.returncode or sha(readback) != sha(a.components / name):
            raise SystemExit("GKDSU_P1=BLOCKED readback=" + target)
print("GKDSU_P1=PASS sha256=" + sha(a.output))
