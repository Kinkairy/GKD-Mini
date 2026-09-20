#!/usr/bin/env python3
from pathlib import Path
import subprocess,sys
project=Path(__file__).resolve().parents[3]
out=Path(sys.argv[1])
assert str(out).startswith("/tmp/gkd-mini-public/gkd-app-update-entry-") and not out.exists()
out.mkdir(mode=0o700)
image="local/c-builder:2026.08.02-kernel"
assert subprocess.check_output(["docker","image","inspect",image,"--format","{{.Id}}"],text=True).strip()=="sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1"
subprocess.run(["docker","run","--rm","--network","none","-v",str(project)+":/src:ro","-v",str(out)+":/out:rw",image,
 "cc","-std=gnu99","-Os","-Wall","-Wextra","-Werror","-static","-I/src/system/application-core/include",
 "/src/system/application-core/tests/update_entry_fixture.c","/src/system/application-core/source/gkd-app-profile.c","-o","/out/fixture"],check=True)
subprocess.run(["unshare","--user","--map-root-user","--mount",str(out/"fixture"),str(out)],check=True)
