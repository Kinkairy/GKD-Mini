#!/usr/bin/env python3
"""Isolated parser, producer, lifetime and existing FPS regressions."""
import argparse
import fcntl
import importlib.util
import io
import json
import mmap
import os
from pathlib import Path
import struct
import subprocess
import zipfile
import zlib

IMAGE='local/c-builder:2026.08.02-kernel'
IMAGE_ID='sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1'
def run(cmd,**kwargs):
    return subprocess.run([str(x) for x in cmd],check=True,**kwargs)
def inside(out):
    app=Path('/lane')
    spec=importlib.util.spec_from_file_location('fps',app/'tests/run_fps_tests.py')
    fps=importlib.util.module_from_spec(spec);spec.loader.exec_module(fps)
    fps.compile_inside(out)
    results=[]
    for variant,flags in [('normal',[]),('san',['-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        cc=['cc','-std=gnu99','-O2','-Wall','-Wextra','-Werror','-I/lane/include',*flags]
        probe=out/('orientation-'+variant)
        run([*cc,app/'source/gkd-app-orientation.c',app/'tests/orientation_fixture.c','-lz','-o',probe])
        present=out/('orientation-present-'+variant)
        run([*cc,'-rdynamic',app/'tests/orientation_present_fixture.c','-L'+str(out),'-l:libfake-sdl-'+variant+'.so','-o',present])
        env=os.environ.copy()
        env.update(ASAN_OPTIONS='detect_leaks=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        run([probe],env=env)
        cases=0
        def check(name,data,lynx,aspect,source='unknown',suffix='.lnx'):
            nonlocal cases
            path=out/(name+suffix);path.write_bytes(data)
            result=run([probe,int(lynx),path],env=env,text=True,capture_output=True).stdout
            assert 'aspect='+aspect+' ' in result,(name,result)
            assert 'source='+source+' ' in result,(name,result)
            path.unlink();cases+=1
        def lnx(rotation=0):
            p=bytearray(64+131072);p[:4]=b'LYNX';struct.pack_into('<H',p,4,0x200);struct.pack_into('<H',p,8,1);p[58]=rotation;return p
        for rotation,aspect in [(0,'landscape'),(1,'portrait'),(2,'portrait')]:
            data=lnx(rotation);check('lnx'+str(rotation),data,True,aspect,'lnx-header')
            for method in [zipfile.ZIP_STORED,zipfile.ZIP_DEFLATED]:
                stream=io.BytesIO()
                with zipfile.ZipFile(stream,'w',compression=method) as z:z.writestr('nested/game.lnx',data)
                check('zip'+str(method),stream.getvalue(),True,aspect,'lnx-header','.zip')
        check('bad-rotation',lnx(3),True,'unknown')
        check('truncated',lnx()[:100],True,'unknown')
        check('bad-magic',b'xxxx'+lnx()[4:],True,'unknown')
        for direction in [0,1]:
            data=bytearray(65536);data[-16]=0xea;data[-4]=4|direction;struct.pack_into('<H',data,len(data)-2,sum(data[:-2])&65535)
            check('ws'+str(direction),data,False,'portrait' if direction else 'landscape','ws-footer','.wsc')
            data[20]^=1;check('bad-checksum',data,False,'unknown',suffix='.ws')
        stream=io.BytesIO()
        with zipfile.ZipFile(stream,'w',compression=8) as z:
            z.writestr('a.lnx',lnx());z.writestr('b.lnx',lnx(1))
        check('ambiguous',stream.getvalue(),True,'unknown',suffix='.zip')
        stream=io.BytesIO()
        with zipfile.ZipFile(stream,'w',compression=8) as z:z.writestr('a.lnx',lnx(1))
        good=stream.getvalue()
        for cut in [1,10,22,40,len(good)//2]:check('truncated-zip',good[:-cut],True,'unknown',suffix='.zip')
        for offset in [6,14,18,22]:
            bad=bytearray(good);bad[offset]^=1;check('bad-local',bad,True,'unknown',suffix='.zip')
        # Every bounded malformed input must remain a valid unknown record.
        for n in [0,1,15,16,21,22,64,128,1024]:check('junk',b'\x00'*n,True,'unknown',suffix='.zip')
        for offset in [good.index(b'PK\x01\x02')+16,good.index(b'PK\x01\x02')+24]:
            bad=bytearray(good);bad[offset]^=1;check('bad-central',bad,True,'unknown',suffix='.zip')
        bad=bytearray(good);struct.pack_into('<I',bad,good.index(b'PK\x01\x02')+24,0xffffffff)
        check('oversize',bad,True,'unknown',suffix='.zip')
        stream=io.BytesIO()
        with zipfile.ZipFile(stream,'w',compression=8) as z:
            z.writestr('valid.lnx',lnx());z.comment=b'x'*65535
        check('long-comment',stream.getvalue(),True,'landscape','lnx-header','.zip')
        link=out/'symlink.lnx';link.symlink_to('/etc/passwd')
        result=run([probe,1,link],env=env,text=True,capture_output=True).stdout
        assert 'aspect=unknown ' in result;link.unlink();cases+=1
        page=os.memfd_create('orientation',os.MFD_ALLOW_SEALING);os.ftruncate(page,80)
        memory=mmap.mmap(page,80);memory[:]=struct.pack('<4I64s',0x474f5231,1,0,0,b'truxton')
        fcntl.fcntl(page,fcntl.F_ADD_SEALS,fcntl.F_SEAL_SEAL|fcntl.F_SEAL_SHRINK|fcntl.F_SEAL_GROW)
        observer=os.dup(page)
        counter=os.memfd_create('counter',0);os.ftruncate(counter,64);counts=mmap.mmap(counter,64)
        counts[:]=struct.pack('<4I2Q8I',0x47504653,1,64,0,0x0123456789abcdef,0xfedcba9876543210,*([0]*8))
        rd,wr=os.pipe2(os.O_NONBLOCK|os.O_CLOEXEC)
        preload=out/('libgkd-fps-present-'+variant+'.so')
        before=[]
        if variant=='san':before=[subprocess.check_output(['gcc','-print-file-name=libasan.so'],text=True).strip()]
        env.update(LD_LIBRARY_PATH=str(out),LD_PRELOAD=':'.join(before+[str(preload)]),GKD_FPS_PRELOAD_PATH=str(preload),
            GKD_FPS_COUNTER_FD=str(counter),GKD_FPS_LIFETIME_FD=str(wr),GKD_FPS_SESSION='0123456789abcdeffedcba9876543210',
            GKD_ORIENTATION_FD=str(page),GKD_TEST_OBSERVER_FD=str(observer))
        run([present],env=env,pass_fds=(page,observer,counter,wr))
        assert struct.unpack_from('<I',memory,8)[0]==0,'exit must withdraw orientation'
        memory.close();counts.close()
        for fd in [page,observer,counter,rd,wr]:os.close(fd)
        fps.present_inside(out,variant);fps.broker_inside(out,variant)
        run([out/('fps-launch-nonblock-'+variant)])
        results.append({'variant':variant,'parser_cases':cases,'session':True,'producer':True,'fps_regression':True})
    (out/'result.json').write_text(json.dumps({'status':'PASS','results':results},indent=2)+'\n')
    print('GKD_ORIENTATION_TESTS=PASS '+json.dumps(results))
def main():
    parser=argparse.ArgumentParser();parser.add_argument('output',type=Path);parser.add_argument('--inside',action='store_true');args=parser.parse_args()
    if args.inside:inside(args.output);return
    if args.output.exists() or not str(args.output).startswith('/tmp/gkd-mini-public/gkd-orientation-test-'):raise SystemExit('new scoped output required')
    if subprocess.check_output(['docker','image','inspect',IMAGE,'--format','{{.Id}}'],text=True).strip()!=IMAGE_ID:raise SystemExit('builder mismatch')
    args.output.mkdir(mode=0o700)
    app=Path(__file__).resolve().parents[1];project=app.parents[1]
    result=subprocess.run(['docker','run','--rm','--network','none','-v',str(app)+':/lane:ro',
        '-v',str(project/'system/rc33-system-update/source')+':/update:ro','-v',str(args.output)+':/out:rw',
        IMAGE,'python3','/lane/tests/run_orientation_tests.py','/out','--inside'],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (args.output/'tests.log').write_text(result.stdout);print(result.stdout);result.check_returncode()
if __name__=='__main__':main()
