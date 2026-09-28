#!/usr/bin/env python3
"""Verbatim kernel source-block tests, plus MIPS compilation performed separately."""
import hashlib,json,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[3]
out=Path(sys.argv[1])
if out.exists() or out.parent!=Path('/tmp/gkd-mini-public') or not out.name.startswith('gkd-input-route-test-'):raise SystemExit('new NUC temporary output required')
out.mkdir(mode=0o700)
source=(root/'kernel/current/menu/gkd-menu-vt.inc').read_text()
blocks=[('static bool gkd_menu_target','static int gkd_menu_open'),('static long gkd_menu_ioctl_locked','static long gkd_menu_ioctl('),('static int gkd_menu_release','static const struct file_operations'),('static long gkd_system_ioctl','static const struct file_operations gkd_system_fops')]
extracted='\n'.join(source[source.index(a):source.index(b)] for a,b in blocks)
(out/'input-route-extracted.inc').write_text(extracted)
image='local/c-builder:2026.08.02-kernel'
assert subprocess.check_output(['docker','image','inspect',image,'--format','{{.Id}}'],text=True).strip()=='sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1'
base=['docker','run','--rm','--network','none','-v',str(root)+':/src:ro','-v',str(out)+':/out:rw',image]
for name,flags in [('normal',[]),('sanitized',['-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
 cmd=['cc','-std=gnu99','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I/out','-I/src/system/application-core/include']+flags+['/src/kernel/current/tests/input_route_fixture.c','-o','/out/test-'+name]
 subprocess.run(base+cmd,check=True)
 r=subprocess.run(base+['/out/test-'+name],capture_output=True,text=True)
 (out/(name+'.log')).write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);r.check_returncode()
(out/'result.json').write_text(json.dumps({'status':'PASS','kernel_source_sha256':hashlib.sha256(source.encode()).hexdigest(),'extracted_sha256':hashlib.sha256(extracted.encode()).hexdigest(),'scope':'actual kernel source logic; host stubs for locks/timer/TTY sink; no device execution'},indent=2)+'\n')
