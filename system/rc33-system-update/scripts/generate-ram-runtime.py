#!/usr/bin/env python3
import argparse, hashlib
from pathlib import Path

def digest(data): return hashlib.sha256(data).digest()

p=argparse.ArgumentParser(); p.add_argument("--p1",type=Path,required=True)
p.add_argument("--p1-sha256",required=True);p.add_argument("--components",type=Path,required=True)
p.add_argument("--output",type=Path,required=True);a=p.parse_args()
runtime=a.components/"gkd-update-runtime-static"
if (a.output.exists() or hashlib.sha256(a.p1.read_bytes()).hexdigest()!=a.p1_sha256
        or not runtime.is_file()):
    raise SystemExit("GKDSU_RAM_BUNDLE=BLOCKED")
data=runtime.read_bytes(); hs=",".join(f"0x{x:02x}" for x in digest(data))
lines=["/* Generated trusted recovery-runtime identity; do not edit. */",
       "#ifndef GKDU_RAM_RUNTIME_H","#define GKDU_RAM_RUNTIME_H",
       f"#define GKDU_RUNTIME_BYTES {len(data)}UL",
       f"static const unsigned char gkdu_runtime_sha256[32]={{{hs}}};",
       "#endif",""]
a.output.write_text("\n".join(lines),encoding="ascii")
print("GKDSU_RAM_BUNDLE=PASS embedded_bytes=0 runtime_bytes=%d"%len(data))
