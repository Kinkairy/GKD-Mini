#!/usr/bin/env python3
"""Exercise real patched framebuffer mode, pan and mmap functions."""
from pathlib import Path
import subprocess, tempfile, re
root=Path(__file__).resolve().parents[3]
driver="drivers/video/fbdev/ingenic-x1830-dpu-fb.c"
def extract(text,name):
 m=re.search(r"^static [^;{]*\b"+name+r"\([^;{]*\)\s*\{",text,re.M)
 assert m,name
 end=m.end();depth=1
 while depth:
  depth+=(text[end]=="{")-(text[end]=="}");end+=1
 return text[m.start():end]
with tempfile.TemporaryDirectory(prefix="gkd-fb-contract-",dir="/tmp/gkd-mini-public") as tmp:
 out=Path(tmp)
 for patch in sorted((root/"kernel/current/patches").glob("*.patch")):
  args=["git","apply","--include="+driver]
  if patch.name.startswith("0002"):args+=["--recount"]
  subprocess.run(args+[str(patch)],cwd=tmp,check=True)
 text=(out/driver).read_text()
 defines="\n".join(re.findall(r"^#define X1830_FB_(?:XRES|YRES|BYTES_PER_PIXEL|LINE_LENGTH|FRAME_SIZE|FRAME_COUNT|SIZE)\s+.*",text,re.M))
 fixture=(root/"kernel/current/tests/framebuffer_fixture.c").read_text()
 fixture=fixture.replace("/* DRIVER_DEFINES */",defines).replace("/* DRIVER_FUNCTIONS */","\n\n".join(extract(text,n) for n in ["x1830_fb_check_var","x1830_fb_pan_display","x1830_fb_mmap"]))
 (out/"fixture.c").write_text(fixture)
 subprocess.run(["docker","run","--rm","--network","none","--user",str(__import__("os").getuid())+":"+str(__import__("os").getgid()),"-v",tmp+":/test","local/c-builder:2026.08.02-kernel","sh","-c","set -eu; gcc -Wall -Wextra -Werror -fsanitize=address,undefined /test/fixture.c -o /test/test; /test/test"],check=True)
