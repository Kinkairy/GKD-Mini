#!/usr/bin/env python3
"""Exact GKD Mini candidate removing an obsolete rg350_kbd board-signature probe."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

OPK_SHA256 = "c4b6fd4ffd38e5d7987b578ea771f058315c08c494e7f0b89f98b01b097f7049"
ELF_SHA256 = "506723f644bb40b268841903d07a2c797658532547b86cafa7335d5f48dbefd8"
SITE = 0xd140c8
STUB = 0xd1411c
BASE = 0x400000

def sha(data):
    return hashlib.sha256(data).hexdigest()

def patch(original):
    if sha(original) != ELF_SHA256:
        raise ValueError("unapproved FBA-SHOOT executable")
    elf=bytearray(original)
    site=SITE-BASE
    # Legacy rg350_kbd ioctl only supplies the 0xaaaa board signature;
    # its absence calls exit(1), before any ROM or video mode is started.
    # The fixed package is scoped to the confirmed GKD Mini runtime.
    if elf[site:site+8] != struct.pack("<II",0x3c060126,0x8f99c418):
        raise ValueError("board probe entry changed")
    elf[site:site+8]=struct.pack("<II",0x08000000|(STUB>>2),0)
    assert all(site<=i<site+8 for i,(a,b) in enumerate(zip(original,elf)) if a!=b)
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
    binary=root/"fbasdl.dge"
    binary.write_bytes(patch(binary.read_bytes()))
    after=manifest(root)
    assert set(before)==set(after)
    assert [k for k in before if before[k]!=after[k]]==["fbasdl.dge"]
    output=args.output/"fba-shoot-gkd-fixed.opk"
    subprocess.run(["mksquashfs",str(root),str(output),"-noappend","-comp","gzip","-b","131072",
        "-all-root","-no-exports","-no-xattrs","-mkfs-time","0","-all-time","0","-processors","1","-no-progress"],check=True)
    check=args.output/"readback"
    subprocess.run(["unsquashfs","-no-xattrs","-processors","1","-no-progress","-d",str(check),str(output)],check=True)
    assert manifest(check)==after
    receipt=dict(source_opk_sha256=OPK_SHA256,source_elf_sha256=ELF_SHA256,
        candidate_opk_sha256=sha(output.read_bytes()),candidate_elf_sha256=after["fbasdl.dge"],
        site=hex(SITE),trampoline=hex(STUB),changed_members=["fbasdl.dge"],
        unchanged_members=len(before)-1,readback="PASS",device_executed=False)
    (args.output/"receipt.json").write_text(json.dumps(receipt,indent=2)+"\n")
    print(json.dumps(receipt))

if __name__=="__main__":
    main()
