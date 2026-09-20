#!/usr/bin/env python3
"""Exact approved Watara package repair: load argv ROM; guard unloaded Continue."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

OPK_SHA256 = "d1a48809eba108d75c6bb40fdf42ad14e9fe5d6de78597580860bc292b58d694"
ELF_SHA256 = "c11106176cb8f7cf6b43e8d86aceedf0b739c669ac687d13fdd67d02d8a79d2e"
SITE = 0x4105fc
STUB = 0x480000
BASE = 0x400000

def sha(data):
    return hashlib.sha256(data).hexdigest()

def patch(original):
    if sha(original) != ELF_SHA256:
        raise ValueError("unapproved Watara executable")
    elf=bytearray(original)
    if elf[:7] != b"\x7fELF\x01\x01\x01": raise ValueError("ELF32 LE")
    if struct.unpack_from("<I",elf,28)[0]!=52 or struct.unpack_from("<HH",elf,42)!=(32,7):
        raise ValueError("program header contract")
    ph=52+6*32
    if struct.unpack_from("<8I",elf,ph)!=(0,0,0,0,0,0,0,4):raise ValueError("unused program header changed")
    segments=[struct.unpack_from("<8I",elf,52+i*32) for i in range(7)]
    assert segments[2]==(1,0,0x400000,0x400000,85252,85252,5,65536)
    assert all(t!=1 or va+mem<=STUB for t,off,va,pa,size,mem,flags,align in segments)
    sites={0x4105fc:(0x24030002,0x8fa30128),
           0x410600:(0xac430000,0x08000000|(STUB>>2)),
           0x410810:(0x0c10403e,0x0c000000|((STUB+20)>>2))}
    # Startup: argc >= 2 -> LOAD(1); otherwise MENU(2). Existing delay slot
    # loads a0 for system_loadcfg; v0 still points to the emulator state.
    # Continue/reset without a ROM: return to MENU before reading emulated RAM.
    words=[0x28630002,0x24630001,0xac430000,0x08000000|(0x410608>>2),0,
           0x3c080042,0x8d085714,0x15000005,0,
           0x24090002,0xad095720,0x08000000|(0x410cb0>>2),0,
           0x08000000|(0x4100f8>>2),0]
    # The null branch needs the ROM pointer's address base, not its loaded value.
    words[6]=0x8d095714  # lw t1,rom_buffer(t0)
    words[7]=0x15200005 # bnez t1,timer
    words[9]=0x24090002 # li t1,MENU
    code=struct.pack("<%dI"%len(words),*words)
    offset=(len(elf)+4095)&~4095
    elf.extend(bytes(offset-len(elf))+code)
    struct.pack_into("<8I",elf,ph,1,offset,STUB,STUB,len(code),len(code),5,4096)
    for site,(before,after) in sites.items():
        assert struct.unpack_from("<I",elf,site-BASE)[0]==before
        struct.pack_into("<I",elf,site-BASE,after)
    allowed=set(range(ph,ph+32))
    for site in sites:allowed.update(range(site-BASE,site-BASE+4))
    assert all(i in allowed for i,(a,b) in enumerate(zip(original,elf)) if a!=b)
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
    binary=root/"potator"
    binary.write_bytes(patch(binary.read_bytes()))
    after=manifest(root)
    assert set(before)==set(after)
    assert [k for k in before if before[k]!=after[k]]==["potator"]
    output=args.output/"watara-load-fixed.opk"
    subprocess.run(["mksquashfs",str(root),str(output),"-noappend","-comp","gzip","-b","131072",
        "-all-root","-no-exports","-no-xattrs","-mkfs-time","0","-all-time","0","-processors","1","-no-progress"],check=True)
    check=args.output/"readback"
    subprocess.run(["unsquashfs","-no-xattrs","-processors","1","-no-progress","-d",str(check),str(output)],check=True)
    assert manifest(check)==after
    receipt=dict(source_opk_sha256=OPK_SHA256,source_elf_sha256=ELF_SHA256,
        candidate_opk_sha256=sha(output.read_bytes()),candidate_elf_sha256=after["potator"],
        site=hex(SITE),trampoline=hex(STUB),changed_members=["potator"],
        unchanged_members=len(before)-1,readback="PASS",device_executed=False)
    (args.output/"receipt.json").write_text(json.dumps(receipt,indent=2)+"\n")
    print(json.dumps(receipt))

if __name__=="__main__":
    main()
