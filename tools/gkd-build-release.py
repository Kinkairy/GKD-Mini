#!/usr/bin/env python3
import argparse, hashlib, json, subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
def digest(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1048576), b''): h.update(b)
    return h.hexdigest()
def main():
    p=argparse.ArgumentParser(description='Build RC3.7 A and R; never install.')
    p.add_argument('--inputs',type=Path,required=True)
    p.add_argument('--name',required=True)
    p.add_argument('--evidence',type=Path,required=True)
    a=p.parse_args()
    if not a.name or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in a.name): p.error('invalid name')
    a.evidence.mkdir(parents=True,exist_ok=False)
    m=json.loads((ROOT/'build/rc3.7-inputs.json').read_text())
    if (ROOT/'VERSION').read_text().strip()!=m['release'] or m['release']!='RC3.7':
        raise ValueError('release identity mismatch')
    for n,v in m['files'].items():
        f=a.inputs/n
        if f.is_symlink() or not f.is_file() or f.stat().st_size!=v['bytes'] or digest(f)!=v['sha256']:
            raise ValueError('input integrity mismatch: '+n)
    if subprocess.check_output(['docker','image','inspect','local/c-builder:2026.08.02-kernel','--format','{{.Id}}'],text=True).strip()!=m['builder_image']:
        raise ValueError('builder image mismatch')
    source={}
    for top in ('kernel/current','system','tools','build'):
        for f in sorted((ROOT/top).rglob('*')):
            if f.is_file() and not any(x in ('.git','__pycache__') for x in f.parts) and f.suffix not in ('.pyc','.md','.txt'):
                source[f.relative_to(ROOT).as_posix()]=digest(f)
    source['VERSION']=digest(ROOT/'VERSION')
    identity=json.dumps(source,sort_keys=True,separators=(',',':')).encode()
    ra=hashlib.sha256(b'RC3.7 A'+identity).hexdigest(); rr=hashlib.sha256(b'RC3.7 R'+identity).hexdigest()
    t=Path('/tmp/gkd-mini-public'); app=t/('gkd-app-components-'+a.name); oa=t/('gkd-app-minimal-'+a.name); cr=t/('gkd-r-components-'+a.name); ore=t/('gkd-kernel-current-r-'+a.name)
    p1=a.inputs/'p1.img'; h=a.inputs/'slot-header-template.bin'; up='system/rc33-system-update/scripts/'
    commands=[
      ['sh','system/application-core/scripts/build-components.sh',str(app)],
      ['sh','tools/gkd-build-all','--p1',str(p1),'--p1-sha256',m['files']['p1.img']['sha256'],'--runtime-id',ra,'--source-slot',str(h),'--app-components',str(app),'--busybox-archive',str(a.inputs/'busybox-1.22.1.tar.bz2'),'--output',str(oa)],
      ['sh',up+'build-components.sh',str(p1),rr,str(cr),'dedicated-r'],
      ['sh',up+'build-kernel-candidate.sh',str(p1),m['files']['p1.img']['sha256'],str(cr),str(ore),'dedicated-recovery'],
      ['python3',up+'build-kernel-slot.py','--source-slot',str(h),'--kernel-build',str(ore),'--name','gkd-rc3.7-r','--output',str(ore/'recovery-r-slot.bin'),'--companion',str(ore/'recovery-capsule.bin'),'--companion-header',str(ore/'round84-recovery-capsule.generated.h'),'--slot-offset','0x300000'],
      ['python3',up+'verify-kernel-slot.py','--slot',str(ore/'recovery-r-slot.bin'),'--raw',str(ore/'vmlinux.bin'),'--name','gkd-rc3.7-r','--companion',str(ore/'recovery-capsule.bin'),'--companion-header',str(ore/'round84-recovery-capsule.generated.h'),'--slot-offset','0x300000']]
    plan={'release':'RC3.7','source':source,'runtime_a':ra,'runtime_r':rr,'commands':commands,'status':'BUILDING','deployed':False}
    def save(): (a.evidence/'build-plan.json').write_text(json.dumps(plan,indent=2)+chr(10))
    save()
    try:
        for i,c in enumerate(commands):
            with (a.evidence/('build-%d.log'%i)).open('w') as log: subprocess.run(c,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
            plan['completed_commands']=i+1; save()
        if not all(digest(ROOT/n)==v for n,v in source.items()):
            raise ValueError('source drift')
        plan['outputs']={str(f):digest(f) for f in (oa/'application-a-slot.bin',ore/'recovery-r-slot.bin')}
        plan['status']='BUILD_PASS'
    except BaseException:
        plan['status']='BUILD_FAILED'; raise
    finally: save()
    print(json.dumps({'status':plan['status'],'outputs':plan['outputs']}))
if __name__=='__main__': main()
