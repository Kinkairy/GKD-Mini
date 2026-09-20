/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-release.h"
#include "gkd-app-exec.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"release line=%d errno=%d\n",__LINE__,errno); _exit(91); } } while(0)
static uint64_t milliseconds(void)
{
    struct timespec t; CHECK(!clock_gettime(CLOCK_MONOTONIC, &t));
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static int descriptors(void)
{
    DIR *d = opendir("/proc/self/fd"); struct dirent *e; int n = 0; CHECK(d);
    while ((e = readdir(d))) if (e->d_name[0] != '.') ++n;
    CHECK(!closedir(d)); return n;
}
static void packet(int fd, const unsigned char token[16], unsigned kind, int ordinary)
{
    unsigned char bytes[25] = {'G','K','D','R','E','L','1',0};
    size_t size = 24; memcpy(bytes+8, token, 16);
    if (kind == 1) bytes[8] ^= 1;
    if (kind == 2) bytes[0] = 'X';
    if (kind == 3) size = 23;
    if (kind == 4) size = 25;
    if (kind == 5) size = 0;
    if (kind == 6 || kind == 7) {
        int rights[16]; for (size_t i=0; i<16; ++i) rights[i]=ordinary;
        size_t n = kind == 6 ? 1 : 16;
        union {struct cmsghdr alignment; unsigned char bytes[CMSG_SPACE(sizeof(rights))];} control;
        struct iovec v = {bytes, size}; struct msghdr m; memset(&m,0,sizeof(m));
        memset(&control,0,sizeof(control)); m.msg_iov=&v; m.msg_iovlen=1;
        m.msg_control=control.bytes; m.msg_controllen=CMSG_SPACE(n*sizeof(int));
        struct cmsghdr *c=CMSG_FIRSTHDR(&m); c->cmsg_level=SOL_SOCKET; c->cmsg_type=SCM_RIGHTS;
        c->cmsg_len=CMSG_LEN(n*sizeof(int)); memcpy(CMSG_DATA(c),rights,n*sizeof(int));
        CHECK(sendmsg(fd,&m,MSG_NOSIGNAL)==(ssize_t)size);
    } else CHECK(send(fd,bytes,size,MSG_NOSIGNAL)==(ssize_t)size);
}

static void receiver_cases(void)
{
    const char *names[] = {"valid", "wrong-token", "bad-magic", "short", "oversize", "zero",
        "rights", "truncated-rights", "wrong-uid", "wrong-pid", "zero-not-wildcard",
        "EOF", "deadline", "backwards-clock", "fork-owner"};
    unsigned char token[16], zero[16]={0}; CHECK(!gkd_app_launch_token(token));
    int ordinary=open("/dev/null",O_RDWR|O_CLOEXEC); CHECK(ordinary>=3);
    int initial=descriptors();
    for (unsigned kind=0; kind<sizeof(names)/sizeof(names[0]); ++kind) {
        int pair[2]; struct gkd_app_release_rx rx=GKD_APP_RELEASE_RX_INIT;
        CHECK(!gkd_app_release_channel(pair));
        CHECK(gkd_app_release_rx_init(&rx,pair[1],getpid(),getuid(),zero,100,100)==-1);
        CHECK(gkd_app_release_rx_init(&rx,pair[1],getpid(),getuid(),token,100,0)==-1);
        CHECK(gkd_app_release_rx_init(&rx,pair[1],getpid(),getuid(),token,UINT64_MAX,1)==-1);
        CHECK(gkd_app_release_rx_init(&rx,pair[1],-1,getuid(),token,100,100)==-1);
        CHECK(rx.fd==-1 && fcntl(pair[1],F_GETFD)>=0);
        CHECK(!gkd_app_release_rx_init(&rx,pair[1],kind==9?getpid()+1:kind==10?0:getpid(),
            kind==8?getuid()+1:getuid(),token,100,100));
        CHECK(gkd_app_release_rx_init(&rx,pair[1],getpid(),getuid(),token,100,100)==-1 && errno==EBUSY);
        CHECK(gkd_app_release_poll(&rx,100)==GKD_RELEASE_WAITING);
        enum gkd_app_release_state expected=GKD_RELEASE_BAD_EVENT;
        if (kind==0) expected=GKD_RELEASE_GRANTED;
        if (kind==11) { CHECK(!shutdown(pair[0],SHUT_WR)); expected=GKD_RELEASE_CANCELLED; }
        else if (kind==12) expected=GKD_RELEASE_TIMED_OUT;
        else if (kind==13) expected=GKD_RELEASE_CHANNEL_ERROR;
        else if (kind==14) {
            pid_t child=fork(); CHECK(child>=0);
            if (!child) _exit(gkd_app_release_poll(&rx,100)==GKD_RELEASE_CHANNEL_ERROR && errno==ECHILD?0:1);
            int status; CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
            expected=GKD_RELEASE_GRANTED; packet(pair[0],token,0,ordinary);
        } else packet(pair[0],token,kind,ordinary);
        uint64_t now=kind==12?200:kind==13?99:101;
        CHECK(gkd_app_release_poll(&rx,now)==expected);
        CHECK(gkd_app_release_poll(&rx,now+1)==expected);
        CHECK(fcntl(pair[1],F_GETFD)>=0 && fcntl(ordinary,F_GETFD)>=0);
        CHECK(!close(pair[0]) && !close(pair[1]) && descriptors()==initial);
        printf("release rx %s PASS\n",names[kind]);
    }
    /* Refusal must not consume the caller FD; credentials and blocking flags
     * are part of the boundary, not silently repaired by the receiver. */
    int pair[2], disabled=0; CHECK(!gkd_app_release_channel(pair));
    struct gkd_app_release_rx rx=GKD_APP_RELEASE_RX_INIT;
    CHECK(!setsockopt(pair[1],SOL_SOCKET,SO_PASSCRED,&disabled,sizeof(disabled)));
    CHECK(gkd_app_release_rx_init(&rx,pair[1],getpid(),getuid(),token,0,1)==-1);
    int enabled=1; CHECK(!setsockopt(pair[1],SOL_SOCKET,SO_PASSCRED,&enabled,sizeof(enabled)));
    CHECK(!fcntl(pair[1],F_SETFD,0));
    CHECK(gkd_app_release_rx_init(&rx,pair[1],getpid(),getuid(),token,0,1)==-1);
    CHECK(!fcntl(pair[1],F_SETFD,FD_CLOEXEC));
    int flags=fcntl(pair[1],F_GETFL); CHECK(flags>=0 && !fcntl(pair[1],F_SETFL,flags&~O_NONBLOCK));
    CHECK(gkd_app_release_rx_init(&rx,pair[1],getpid(),getuid(),token,0,1)==-1);
    CHECK(gkd_app_release_rx_init(&rx,ordinary,getpid(),getuid(),token,0,1)==-1);
    CHECK(gkd_app_release_channel(NULL)==-1 && gkd_app_release_poll(NULL,0)==GKD_RELEASE_CHANNEL_ERROR);
    CHECK(!close(pair[0]) && !close(pair[1]) && !close(ordinary));
}

static void sender_cases(void)
{
    const char *names[]={"exec-frame", "init-mismatch-retry", "duplicate", "deadline", "early-frame",
                         "full-queue-retry", "shutdown-write"};
    int initial=descriptors();
    int image=open("/proc/self/exe",O_RDONLY|O_CLOEXEC), initfd=syscall(SYS_pidfd_open,getpid(),0);
    int executable=open("/test/exec-fixture",O_RDONLY|O_CLOEXEC), terminal=open("/dev/null",O_RDWR|O_CLOEXEC);
    int output[2]; CHECK(image>=3 && initfd>=3 && executable>=3 && terminal>=3 && !pipe2(output,O_NONBLOCK|O_CLOEXEC));
    for (unsigned kind=0; kind<sizeof(names)/sizeof(names[0]); ++kind) {
        unsigned char token[16]; int ready[2], errors[2], release[2], start[2], status;
        CHECK(!gkd_app_launch_token(token) && !gkd_app_ready_channel(ready) && !gkd_app_watch_error_channel(errors));
        CHECK(!gkd_app_release_channel(release));
        CHECK(!socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,start));
        pid_t preparer=getpid(), child=fork(); CHECK(child>=0);
        if (!child) {
            char begin; close(start[1]); close(errors[0]); close(ready[0]); close(release[0]);
            if (kind==4) {
                CHECK(!gkd_app_ready_send(ready[1],token));
                CHECK(write(start[0],"r",1)==1);
            }
            CHECK(read(start[0],&begin,1)==1); close(start[0]);
            struct gkd_app_release_rx rx=GKD_APP_RELEASE_RX_INIT;
            CHECK(!gkd_app_release_rx_init(&rx,release[1],preparer,getuid(),token,milliseconds(),3000));
            enum gkd_app_release_state state;
            do { state=gkd_app_release_poll(&rx,milliseconds()); if (!state) usleep(1000); } while (!state);
            if (kind==3 || kind==4 || kind==6) _exit(state!=GKD_RELEASE_GRANTED?0:1);
            CHECK(state==GKD_RELEASE_GRANTED);
            CHECK(!close(release[1]));
            char *argv[]={"exec-fixture","--watch-hold",NULL};
            char *env[]={"HOME=/fixture-home","PATH=/bin",NULL};
            struct gkd_app_exec_request request={preparer,executable,terminal,output[1],ready[1],
                "/test/libgkd-sm-present.so",token,argv,env};
            gkd_app_exec_replace_report(&request,errors[1]); _exit(92);
        }
        close(start[0]);
        int appfd=syscall(SYS_pidfd_open,child,0); CHECK(appfd>=3);
        struct gkd_app_watch watch=GKD_APP_WATCH_INIT;
        struct gkd_app_release_tx tx=GKD_APP_RELEASE_TX_INIT;
        uint64_t now=milliseconds();
        CHECK(!gkd_app_watch_init(&watch,appfd,errors[0],ready[0],child,getuid(),token,now,3000));
        if (kind==1) {
            CHECK(gkd_app_release_send(&tx,&watch,release[0],initfd,getpid(),executable,now)==-1 && errno==EAGAIN && !tx.sent);
            char data[32]; CHECK(recv(release[1],data,sizeof(data),MSG_DONTWAIT)==-1 && errno==EAGAIN);
        }
        if (kind==5) {
            unsigned char fill[24]={0};
            while (send(release[0],fill,sizeof(fill),MSG_DONTWAIT|MSG_NOSIGNAL)==sizeof(fill)) {}
            CHECK(errno==EAGAIN || errno==EWOULDBLOCK);
            CHECK(gkd_app_release_send(&tx,&watch,release[0],initfd,getpid(),image,now)==-1 && errno==EAGAIN && !tx.sent);
            while (recv(release[1],fill,sizeof(fill),MSG_DONTWAIT)>0) {}
            CHECK(errno==EAGAIN || errno==EWOULDBLOCK);
        }
        if (kind==4) { char receipt; CHECK(read(start[1],&receipt,1)==1 && receipt=='r'); }
        CHECK(!close(errors[1]) && !close(ready[1]) && !close(release[1]));
        if (kind==6) CHECK(!shutdown(release[0],SHUT_WR));
        int result=gkd_app_release_send(&tx,&watch,release[0],initfd,getpid(),image,kind==3?now+3000:now);
        if (kind==3 || kind==4 || kind==6) {
            CHECK(result==-1 && !tx.sent);
            CHECK(errno==(kind==3?ETIMEDOUT:kind==4?EPROTO:EPIPE));
            if (kind==4) CHECK(watch.state==GKD_WATCH_WAITING && watch.ready.state==GKD_APP_FRAME_SUBMITTED);
        } else CHECK(result==0 && tx.sent==1);
        if (kind==2) CHECK(gkd_app_release_send(&tx,&watch,release[0],initfd,getpid(),image,now)==-1 && errno==EALREADY);
        CHECK(write(start[1],"x",1)==1 && !close(start[1]));
        CHECK(!close(release[0]));
        if (kind==3 || kind==4 || kind==6) gkd_app_watch_close(&watch);
        else {
            enum gkd_app_watch_state state;
            do { state=gkd_app_watch_poll(&watch,milliseconds()); if (!state) usleep(1000); } while (!state);
            CHECK(state==GKD_WATCH_FRAME_SUBMITTED && watch.error_eof);
        }
        CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
        gkd_app_watch_close(&watch);
        CHECK(descriptors()==initial+6);
        printf("release tx %s PASS\n",names[kind]);
    }
    close(image); close(initfd); close(executable); close(terminal); close(output[0]); close(output[1]); CHECK(descriptors()==initial);
}
int main(void)
{
    /* Send failures must not terminate a process through SIGPIPE. */
    signal(SIGPIPE,SIG_DFL);
    int initial=descriptors(); receiver_cases(); sender_cases(); CHECK(descriptors()==initial);
    puts("GKD_APP_RELEASE=PASS cases=22 descriptor-leaks=0 native-exec-frame=PASS");
    return 0;
}
