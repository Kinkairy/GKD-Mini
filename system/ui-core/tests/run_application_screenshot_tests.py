#!/usr/bin/env python3
"""Actual A screenshot implementation with normal and sanitized syscall fixtures."""
import pathlib,re,subprocess,sys
project=pathlib.Path(__file__).resolve().parents[3]
out=pathlib.Path(sys.argv[1])
assert str(out).startswith("/tmp/gkd-mini-public/gkd-app-screenshot-tests-") and not out.exists()
out.mkdir(mode=0o700)
image="local/c-builder:2026.08.02-kernel"
assert subprocess.check_output(["docker","image","inspect",image,"--format","{{.Id}}"],text=True).strip()=="sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1"
fixture=project/"system/ui-core/tests/application_screenshot_fixture.c"
wrappers=sorted(set(re.findall(r"\b__wrap_([a-zA-Z0-9_]+)\(",fixture.read_text())))
base=["docker","run","--rm","--network","none","-v",str(project)+":/src:ro","-v",str(out)+":/out:rw",image]
for name,extra in [("normal",[]),("sanitized",["-O1","-g","-fsanitize=address,undefined"])]:
 # Disable host fortify aliases only in syscall-injection fixtures; production flags are unchanged.
 flags=["-U_FORTIFY_SOURCE","-D_FORTIFY_SOURCE=0","-std=gnu99","-Os","-Wall","-Wextra","-Werror"]+extra
 subprocess.run(base+["cc"]+flags+["-DGKD_APPLICATION_UI=1","-Dmain=gkd_screenshot_actual_main",
  "-c","/src/system/ui-core/source/gkd-screenshot.c","-o","/out/"+name+".o"],check=True)
 subprocess.run(base+["cc"]+flags+["/src/system/ui-core/tests/application_screenshot_fixture.c","/out/"+name+".o",
  "-Wl,"+",".join("--wrap="+w for w in wrappers),"-o","/out/"+name],check=True)
 subprocess.run(base+["/out/"+name],check=True)
print("GKD_APP_SCREENSHOT_TESTS=PASS normal/ASan/UBSan")
