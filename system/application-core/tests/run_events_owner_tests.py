#!/usr/bin/env python3
"""Event observer's actual system-hotkey owner lifecycle, with real owned FDs."""
import json, pathlib, subprocess, sys
root=pathlib.Path(__file__).resolve().parents[3]
out=pathlib.Path(sys.argv[1])
if out.exists() or out.parent!=pathlib.Path("/tmp/gkd-mini-public") or not out.name.startswith("gkd-events-owner-test-"):
    raise SystemExit("new scoped NUC output required")
out.mkdir(mode=0o700)
image="local/c-builder:2026.08.02-kernel"
assert subprocess.check_output(["docker","image","inspect",image,"--format","{{.Id}}"],text=True).strip()=="sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1"
base=["docker","run","--rm","--network","none","-v",str(root)+":/src:ro","-v",str(out)+":/out:rw",image]
for name,flags in (("normal",[]),("sanitized",["-fsanitize=address,undefined","-fno-sanitize-recover=all"])):
    subprocess.run(base+["cc","-std=gnu99","-O1","-g","-Wall","-Wextra","-Werror",
        "-I/src/system/application-core/include","-I/src/system/ui-core/include"]+flags+
        ["/src/system/application-core/tests/events_owner_fixture.c","-o","/out/"+name],check=True)
    result=subprocess.run(base+["/out/"+name],capture_output=True,text=True)
    (out/(name+".log")).write_text(result.stdout+result.stderr)
    print(result.stdout+result.stderr);result.check_returncode()
(out/"result.json").write_text(json.dumps({"status":"PASS","scope":"actual event owner code, real FD cleanup, mocked device discovery; no device execution"})+"\n")
