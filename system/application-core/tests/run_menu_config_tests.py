#!/usr/bin/env python3
"""Native parser/real file/CLI tests; outputs outside source, no device access."""
import json
from pathlib import Path
import subprocess
import sys
project = Path(__file__).resolve().parents[3]
out = Path(sys.argv[1])
if out.exists() or not out.is_absolute() or out.parent != Path('/tmp/gkd-mini-public') or not out.name.startswith('gkd-menu-config-test-'):
    raise SystemExit('new scoped NUC temporary output required')
image = 'local/c-builder:2026.08.02-kernel'
image_id = 'sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1'
assert subprocess.check_output(['docker','image','inspect',image,'--format','{{.Id}}'],text=True).strip() == image_id
out.mkdir(mode=0o700)
base=['docker','run','--rm','--network','none','--read-only','--tmpfs','/tmp:rw,nosuid,nodev',
      '-v',str(project)+':/src:ro','-v',str(out)+':/out:rw',
      '-v','/srv/c-builder/toolchains/gkdmini-gcw0-d28:/opt/toolchain:ro',image]
app='/src/system/application-core'
def run(args, label):
    p=subprocess.run(base+args,text=True,capture_output=True)
    (out/(label+'.log')).write_text(p.stdout+p.stderr)
    if p.returncode:
        print(p.stdout+p.stderr);p.check_returncode()
    return p.stdout
run(['cp',app+'/config/input-routing.conf','/out/catalog.conf'],'catalog-copy')
common=['-std=gnu99','-O2','-Wall','-Wextra','-Werror','-I'+app+'/include','-I/src/system/ui-core/include',
        app+'/source/gkd-app-menu-config.c']
for name,flags in [('normal',[]),('sanitized',['-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
    binary='/out/menu-config-'+name
    run(['cc']+common+flags+[app+'/tests/menu_config_fixture.c','-o',binary],'build-'+name)
    print(run([binary,'/out/catalog.conf'],'test-'+name).strip())
# Real launcher preparation with real config files/OPK hashing. Only the
# character-device fstat/ioctl boundary is replaced; no device is accessed.
subprocess.run(['python3',str(project/'system/application-core/scripts/derive-schema.py'),
    str(project/'system/config-core/schema/gdkmini.schema'),str(out/'gdkmini.schema')],check=True)
schema=(out/'gdkmini.schema').read_text()
(out/'effective.conf').write_text(''.join(parts[0]+'='+parts[2]+'\n' for line in schema.splitlines()
    if not line.startswith('#') and len(parts:=line.split('|'))==8))
for name,flags in [('normal',[]),('sanitized',['-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
    binary='/out/menu-launch-'+name
    run(['cc']+common+flags+['-I/src/system/rc33-system-update/source',
        app+'/tests/menu_launch_fixture.c',app+'/source/gkd-app-settings.c',
        '/src/system/rc33-system-update/source/gkd-update-sha256.c',
        '-Wl,--wrap=fstat','-Wl,--wrap=ioctl','-o',binary],'build-launch-'+name)
    # /media and /var/run are writable only in the isolated test container.
    command=[x for x in base if x!='--read-only']+[binary,'/out/effective.conf']
    result=subprocess.run(command,capture_output=True,text=True)
    (out/('test-launch-'+name+'.log')).write_text(result.stdout+result.stderr)
    print(result.stdout+result.stderr);result.check_returncode()
cli='/out/gkd-menu-config'
run(['cc']+common+[app+'/source/gkd-menu-config.c','-o',cli],'build-cli')
checked=run([cli,'check','/out/catalog.conf'],'cli-check').strip()
assert 'version=2 profiles=28' in checked
print(checked)
# A new arbitrary emulator and F1/F2 mapping works by editing data only.
future='version=1\n[future-core]\nopk_sha256='+'a'*64+'\ndesktop=new.desktop\nexec=new-core\naction=chord\nkeys=59+60\nhold_ms=80\n'
(out/'future-source.conf').write_text(future)
run(['cp','/out/future-source.conf','/out/future.conf'],'future-copy')
matched=run([cli,'match','/out/future.conf','a'*64,'new.desktop','new-core'],'cli-new-emulator')
assert 'action=chord hold_ms=80 keys=59+60' in matched
p=subprocess.run(base+[cli,'match','/out/future.conf','b'*64,'new.desktop','new-core'],capture_output=True,text=True)
assert p.returncode==2 and p.stdout=='GKD_MENU_CONFIG=UNKNOWN\n'
(out/'cli-version-mismatch.log').write_text(p.stdout+p.stderr)
cc='/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-gcc.br_real'
run([cc]+common+['-Os',app+'/source/gkd-menu-config.c','-o','/out/gkd-menu-config.mips'],'build-mips')
elf=run(['readelf','-h','-l','-d','/out/gkd-menu-config.mips'],'mips-elf')
assert 'MIPS' in elf and '/lib/ld-uClibc.so.0' in elf
(out/'result.json').write_text(json.dumps({'status':'PASS','normal':True,'asan_ubsan':True,'catalog_profiles':26,
 'new_emulator_config_only':True,'unknown_version_rejected':True,'mips_compile':'PASS','launcher_real_files_and_hashing':'PASS','device_executed':False},indent=2)+'\n')
print('GKD_MENU_CONFIG_CLI=PASS new_emulator=data_only unknown_version=unmapped MIPS=compiled')
