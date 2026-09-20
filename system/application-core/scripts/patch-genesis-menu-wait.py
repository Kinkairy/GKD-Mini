#!/usr/bin/env python3
"""Exact Genesis-SX v0.98 repair; source OPK and every other member are preserved.

The main thread sets gotomenu in sdl_input_update. The timer stops posting
sdl_sync in that state. Skip only the following main-thread wait, before
entering the existing menu; normal frame pacing and timer behavior stay intact.
This produces an offline candidate, never installs it.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

OPK_SHA256 = "30407a643357d4c5698c9e3afebc686d482bbf5c49c6732b898aee54d85bdb56"
ELF_SHA256 = "df93f844f01f53d4b6cca42eb6bf3fd8cbecc1172af77880352894968e0e6a2a"
SITE = 0x52514c
STUB = 0x5d4a20
BASE = 0x400000
SEM_WAIT = 0x5d4600

def sha(data):
    return hashlib.sha256(data).hexdigest()

def patch(original):
    if sha(original) != ELF_SHA256:
        raise ValueError("unapproved Genesis executable")
    elf = bytearray(original)
    if elf[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("ELF32 little endian contract")
    phoff = struct.unpack_from("<I", elf, 28)[0]
    entsize, count = struct.unpack_from("<HH", elf, 42)
    if (phoff, entsize, count) != (52, 32, 9):
        raise ValueError("program header contract")
    ph = phoff + 4 * entsize
    if struct.unpack_from("<8I", elf, ph) != (1, 0, BASE, BASE, 0x1d4a20, 0x1d4a20, 5, 0x10000):
        raise ValueError("executable segment contract")
    if elf[SITE-BASE:SITE-BASE+8] != struct.pack("<II", 0x0c175180, 0):
        raise ValueError("original wait/delay slot contract")
    # t0 is caller-clobbered; a0 (the semaphore) and ra are preserved.
    # Tail-call the original PLT only when the main-thread menu flag is zero.
    words = (0x3c08012d, 0x8d084810, 0x15000003, 0,
             0x08000000 | (SEM_WAIT >> 2), 0, 0x03e00008, 0)
    code = struct.pack("<8I", *words)
    offset = STUB - BASE
    if elf[offset:offset+len(code)] != bytes(len(code)):
        raise ValueError("executable padding is not empty")
    elf[offset:offset+len(code)] = code
    struct.pack_into("<I", elf, SITE-BASE, 0x0c000000 | (STUB >> 2))
    struct.pack_into("<II", elf, ph+16, offset+len(code), offset+len(code))
    allowed = set(range(ph+16,ph+24)) | set(range(SITE-BASE,SITE-BASE+4)) | set(range(offset,offset+len(code)))
    assert len(elf) == len(original)
    assert all(i in allowed for i,(a,b) in enumerate(zip(original,elf)) if a != b)
    return bytes(elf)

def manifest(root):
    return {str(p.relative_to(root)): ("link:"+str(p.readlink()) if p.is_symlink() else sha(p.read_bytes()))
            for p in sorted(root.rglob("*")) if p.is_symlink() or p.is_file()}

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--source",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    if sha(args.source.read_bytes()) != OPK_SHA256:
        raise ValueError("unapproved source package")
    if args.output.exists() or args.output.parent != Path("/tmp/gkd-mini-public") or not args.output.name.startswith("gkd-emulator-"):
        raise ValueError("new scoped output required")
    args.output.mkdir(mode=0o700)
    root=args.output/"root"
    subprocess.run(["unsquashfs","-no-xattrs","-processors","1","-no-progress","-d",str(root),str(args.source)],check=True)
    before=manifest(root)
    binary=root/"gen_mini"
    binary.write_bytes(patch(binary.read_bytes()))
    after=manifest(root)
    assert set(before)==set(after)
    assert [k for k in before if before[k]!=after[k]]==["gen_mini"]
    output=args.output/"genesis-menu-fixed.opk"
    subprocess.run(["mksquashfs",str(root),str(output),"-noappend","-comp","gzip","-b","131072",
        "-all-root","-no-exports","-no-xattrs","-mkfs-time","0","-all-time","0","-processors","1","-no-progress"],check=True)
    check=args.output/"readback"
    subprocess.run(["unsquashfs","-no-xattrs","-processors","1","-no-progress","-d",str(check),str(output)],check=True)
    assert manifest(check)==after
    receipt=dict(source_opk_sha256=OPK_SHA256,source_elf_sha256=ELF_SHA256,
        candidate_opk_sha256=sha(output.read_bytes()),candidate_elf_sha256=after["gen_mini"],
        site=hex(SITE),trampoline=hex(STUB),changed_members=["gen_mini"],
        unchanged_members=len(before)-1,readback="PASS",device_executed=False)
    (args.output/"receipt.json").write_text(json.dumps(receipt,indent=2)+"\n")
    print(json.dumps(receipt))

if __name__=="__main__":
    main()
