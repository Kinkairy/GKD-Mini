#!/usr/bin/env python3
"""Exercise production authorize against real processes and a distinct app root."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

IMAGE = "local/c-builder:2026.08.02-kernel"
IMAGE_ID = "sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1"
LANE = Path(__file__).resolve().parents[1]

def inside():
    out = Path("/out")
    probe = Path("/lane/tests/fps_authorize_probe.c")
    helper = out / "peer.c"
    helper.write_text('#include <stdio.h>\n#include <unistd.h>\nint main(void){char c;puts("READY");fflush(stdout);return read(0,&c,1)<0;}\n')
    subprocess.run(["cc","-static",str(helper),"-o",str(out/"peer")],check=True)
    trusted = Path("/usr/libexec/gkd-app-launcher")
    trusted.parent.mkdir(parents=True,exist_ok=True)
    assert not trusted.exists()
    shutil.copyfile(out/"peer",trusted);trusted.chmod(0o555)
    root = Path("/tmp/gkd-fps-app-root")
    (root/"usr/bin").mkdir(parents=True)
    (root/"var/run/gkd-mini").mkdir(parents=True)
    os.link(trusted,root/"usr/bin/opkrun")
    shutil.copyfile(trusted,root/"usr/bin/untrusted");(root/"usr/bin/untrusted").chmod(0o555)
    assert not (root/"usr/libexec/gkd-app-launcher").exists()
    assert trusted.stat().st_ino==(root/"usr/bin/opkrun").stat().st_ino
    def enter():
        os.chroot(root);os.chdir("/")
    children=[]
    def child(name,leader):
        p=subprocess.Popen(["/usr/bin/"+name],stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                           preexec_fn=enter,start_new_session=leader)
        children.append(p);assert p.stdout.readline()==b"READY\n"
        return p
    def marker(peer,offset=0):
        fields=Path(f"/proc/{peer.pid}/stat").read_text().rsplit(")",1)[1].split()
        assert int(fields[2])==peer.pid and int(fields[3])==peer.pid
        f=root/"var/run/gkd-mini/active-game"
        f.write_text(f"pid={peer.pid}\npgid={peer.pid}\nstarttime={int(fields[19])+offset}\n")
        f.chmod(0o600)
    old=out/"old-probe.c"
    old.write_text(probe.read_text().replace('#include "../source/gkd-app-fps.c"',
                                            '#include "/old-source.c"'))
    try:
        init=child("opkrun",False);peer=child("opkrun",True);marker(peer)
        for variant,src,flags in [
            ("old",old,[]),("normal",probe,[]),
            ("sanitized",probe,["-O1","-g","-fsanitize=address,undefined","-fno-sanitize-recover=all"])]:
            exe=out/("probe-"+variant)
            subprocess.run(["cc","-std=gnu99","-O2","-Wall","-Wextra","-Werror",
                            "-ffunction-sections","-fdata-sections","-I/lane/include",
                            *flags,str(src),"/lane/source/gkd-app-namespace.c",
                            "-Wl,--gc-sections","-o",str(exe)],check=True)
            def check(who,expected,case):
                result=subprocess.run([str(exe),str(init.pid),str(init.pid),str(who.pid)],
                                      capture_output=True,text=True,timeout=10)
                assert result.returncode==expected,(variant,case,result.stdout,result.stderr)
                with (out/(variant+".log")).open("a") as log:
                    log.write(case+"\n"+result.stdout+result.stderr)
            marker(peer)
            check(peer,1 if variant=="old" else 0,"native launcher bound into distinct app root")
            if variant=="old":
                continue
            marker(peer,1);check(peer,1,"stale starttime");marker(peer)
            other=child("untrusted",True);marker(other)
            check(other,1,"same executable bytes but untrusted inode")
            marker(peer);check(peer,0,"restored valid identity")
        print("FPS_REAL_AUTH_PASS old-path-reproduced/real-proc/distinct-root/native-bind/nonroot/lifecycle/unrelated/stale-marker/untrusted-inode normal+ASan+UBSan")
    finally:
        for p in children:
            p.stdin.close()
        for p in children:
            assert p.wait(timeout=5)==0

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("output",type=Path)
    parser.add_argument("--outgoing-source",type=Path,required=True)
    parser.add_argument("--inside",action="store_true")
    args=parser.parse_args()
    if args.inside:
        inside();return
    assert not args.output.exists() and args.output.is_relative_to("/tmp/gkd-mini-public")
    assert args.outgoing_source.is_file()
    args.output.mkdir(mode=0o700)
    actual=subprocess.check_output(["docker","image","inspect",IMAGE,"--format","{{.Id}}"],text=True).strip()
    assert actual==IMAGE_ID
    subprocess.run(["docker","run","--rm","--network","none","-v",str(LANE)+":/lane:ro",
                    "-v",str(args.output)+":/out:rw","-v",str(args.outgoing_source)+":/old-source.c:ro",
                    IMAGE,"python3","/lane/tests/run_fps_authorize_tests.py","/out",
                    "--outgoing-source","/old-source.c","--inside"],check=True)

if __name__=="__main__":
    main()
