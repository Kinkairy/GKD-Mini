#!/usr/bin/env python3
import mmap, os, select, signal, struct, subprocess
OUT=os.path.dirname(os.path.abspath(__file__))
MAGIC=0x47504653; VERSION=1; SIZE=64; COUNT_OFF=32; STATE_OFF=36
SESSION="0123456789abcdeffedcba9876543210"
HI=int(SESSION[:16],16); LO=int(SESSION[16:],16)
PRELOAD=os.environ.get("GKD_TEST_PRELOAD",os.path.join(OUT,"libgkd-fps-present.so"))
FIXTURE=os.environ.get("GKD_TEST_FIXTURE",os.path.join(OUT,"fixture"))
NOSDL=os.environ.get("GKD_TEST_NOSDL",os.path.join(OUT,"non-sdl"))
UNRELATED=os.environ.get("GKD_TEST_UNRELATED",os.path.join(OUT,"libunrelated.so"))
SANITIZER=os.environ.get("GKD_SANITIZER_PRELOAD","")
def page(fd,count=0,magic=MAGIC,version=VERSION,size=SIZE,hi=HI,lo=LO,flags=0,producer=0,reserved=None):
    os.ftruncate(fd,size); m=mmap.mmap(fd,size)
    struct.pack_into("<IIIIQQII6I",m,0,magic,version,SIZE,flags,hi,lo,count,producer,*(reserved or [0]*6))
    return m
def count(m): return struct.unpack_from("<I",m,COUNT_OFF)[0]
def state(m): return struct.unpack_from("<I",m,STATE_OFF)[0]
def valid(initial=0):
    fd=os.memfd_create("gkd-fps-counter",os.MFD_ALLOW_SEALING); return fd,page(fd,initial)
def close_page(fd,m): m.close(); os.close(fd)
def launch_env(fd,session,life,extra=()):
    env=os.environ.copy(); before=([SANITIZER] if SANITIZER else [])+[PRELOAD]+list(extra)
    after=([SANITIZER] if SANITIZER else [])+list(extra)
    env["LD_PRELOAD"]=":".join(before); env["GKD_TEST_EXPECT_PRELOAD"]=":".join(after)
    env["GKD_FPS_COUNTER_FD"]=str(fd); env["GKD_FPS_LIFETIME_FD"]=str(life)
    env["GKD_FPS_SESSION"]=session; env["GKD_FPS_PRELOAD_PATH"]=PRELOAD
    return env
def run(name,args,fd=None,session=SESSION,pass_fd=True,binary=None,extra=(),bad_lifetime=False):
    if fd is None:
        env=os.environ.copy(); before=([SANITIZER] if SANITIZER else [])+[PRELOAD]
        after=([SANITIZER] if SANITIZER else [])
        env["LD_PRELOAD"]=":".join(before); env["GKD_TEST_EXPECT_PRELOAD"]=":".join(after)
        env["GKD_FPS_PRELOAD_PATH"]=PRELOAD
        for key in ("GKD_FPS_COUNTER_FD","GKD_FPS_LIFETIME_FD","GKD_FPS_SESSION"): env.pop(key,None)
        passfds=(); readfd=None; writefd=None
    elif bad_lifetime:
        writefd=os.memfd_create("bad-lifetime",0); os.ftruncate(writefd,1); readfd=None
        env=launch_env(fd,session,writefd,extra); passfds=tuple(x for x in (fd if pass_fd else -1,writefd) if x>=0)
    else:
        readfd,writefd=os.pipe2(os.O_NONBLOCK|os.O_CLOEXEC)
        env=launch_env(fd,session,writefd,extra); passfds=tuple(x for x in (fd if pass_fd else -1,writefd) if x>=0)
    p=subprocess.Popen([binary or FIXTURE]+args,env=env,pass_fds=passfds,
                       stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    if writefd is not None: os.close(writefd)
    stdout,stderr=p.communicate(timeout=20)
    if p.returncode: raise AssertionError(f"{name}: rc={p.returncode} stdout={stdout!r} stderr={stderr!r}")
    if readfd is not None:
        if os.read(readfd,1)!=b"": raise AssertionError(name+": lifetime writer leaked")
        os.close(readfd)
def bad_read_lifetime():
    fd,m=valid(); readfd,writefd=os.pipe2(os.O_NONBLOCK|os.O_CLOEXEC)
    env=launch_env(fd,SESSION,readfd)
    p=subprocess.Popen([FIXTURE,"sequence"],env=env,pass_fds=(fd,readfd),
                       stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    os.close(readfd); stdout,stderr=p.communicate(timeout=20)
    os.close(writefd)
    if p.returncode: raise AssertionError(f"bad-read-lifetime: rc={p.returncode} stdout={stdout!r} stderr={stderr!r}")
    assert state(m)==0 and count(m)==0
    close_page(fd,m)

def exec_stale():
    fd,m=valid(); readfd,writefd=os.pipe2(os.O_NONBLOCK|os.O_CLOEXEC)
    env=launch_env(fd,SESSION,writefd); env["GKD_TEST_REPLACE"]=NOSDL
    p=subprocess.Popen([FIXTURE,"replace"],env=env,pass_fds=(fd,writefd),
                       stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    os.close(writefd); poller=select.poll(); poller.register(readfd,select.POLLHUP)
    events=poller.poll(1200)
    if not events or p.poll() is not None or state(m)!=1:
        p.kill(); out,err=p.communicate(); raise AssertionError(f"exec-stale events={events} rc={p.returncode} out={out!r} err={err!r}")
    p.send_signal(signal.SIGTERM); p.communicate(timeout=5)
    os.close(readfd); close_page(fd,m)
def main():
    cases=0
    fd,m=valid(); run("success-failure",["sequence"],fd); assert state(m)==1 and count(m)==3; close_page(fd,m); cases+=1
    run("disabled",["sequence"]); cases+=1
    run("bad-fd",["sequence"],99999,pass_fd=False); cases+=1
    fd,m=valid(); run("bad-lifetime",["sequence"],fd,bad_lifetime=True); assert state(m)==0 and count(m)==0; close_page(fd,m); cases+=1
    bad_read_lifetime(); cases+=1
    fd=os.memfd_create("bad-size",0); os.ftruncate(fd,63); run("bad-size",["sequence"],fd); os.close(fd); cases+=1
    for field,kw in [("magic",{"magic":0}),("version",{"version":2}),("bytes",{"size":64}),
                     ("flags",{"flags":1}),("reserved",{"reserved":[1]+[0]*5})]:
        fd,m=valid()
        if field=="bytes": struct.pack_into("<I",m,8,63)
        else: m.close(); os.ftruncate(fd,0); m=page(fd,**kw)
        run("bad-"+field,["sequence"],fd); assert state(m)==0 and count(m)==0; close_page(fd,m); cases+=1
    fd=os.memfd_create("bad-producer",0); m=page(fd,producer=1); run("bad-producer",["sequence"],fd); assert state(m)==1 and count(m)==0; close_page(fd,m); cases+=1
    fd,m=valid(); run("bad-session",["sequence"],fd,"1123456789abcdeffedcba9876543210"); assert state(m)==0 and count(m)==0; close_page(fd,m); cases+=1
    for bad in ["0"*32,"short","xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"]:
        fd,m=valid(); run("malformed-session",["sequence"],fd,bad); assert state(m)==0 and count(m)==0; close_page(fd,m); cases+=1
    fd,m=valid(); run("unsupported",[],fd,binary=NOSDL); assert state(m)==0 and count(m)==0; close_page(fd,m); cases+=1
    fd,m=valid(); run("preserve-preloads",["sequence"],fd,extra=(UNRELATED,UNRELATED)); assert state(m)==1 and count(m)==3; close_page(fd,m); cases+=1
    fd,m=valid(0xfffffffe); run("wrap",["successes","3"],fd); assert state(m)==1 and count(m)==1; close_page(fd,m); cases+=1
    fd,m=valid(); run("threads",["threads"],fd); assert state(m)==1 and count(m)==80000; close_page(fd,m); cases+=1
    fd,m=valid(); run("fork",["fork"],fd); assert state(m)==1 and count(m)==1; close_page(fd,m); cases+=1
    fd,m=valid(); run("exec-child",["exec"],fd); assert state(m)==1 and count(m)==1; close_page(fd,m); cases+=1
    exec_stale(); cases+=1
    fd1,m1=valid(); run("session-a",["successes","2"],fd1); assert count(m1)==2
    fd2,m2=valid(); run("session-b",["successes","4"],fd2); assert state(m1)==1 and state(m2)==1 and count(m1)==2 and count(m2)==4
    close_page(fd1,m1); close_page(fd2,m2); cases+=1
    print(f"GKD_FPS_PRESENT=PASS cases={cases} successful-only=PASS forwarding-errno=PASS atomic-wrap=PASS fork-exec-session-isolation=PASS lifetime-hup=PASS preserve-preloads=PASS unsupported-state=PASS fail-open-counter=PASS")
if __name__=="__main__": main()
