/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-session.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#ifndef GKD_APP_HOST_EXECUTABLE
#define GKD_APP_HOST_EXECUTABLE "/usr/sbin/gkd-app-host"
#endif
static int signal_child(struct gkd_app_session *s,int sig)
{
    if(s->pid<=0){errno=EINVAL;return -1;}
    /* Our unreaped direct child cannot have its PID reused, including the
     * exceptional pidfd-open failure path. */
    if(s->pidfd<0)return kill(s->pid,sig);
    return (int)syscall(SYS_pidfd_send_signal,s->pidfd,sig,NULL,0U);
}
int gkd_app_session_start(struct gkd_app_session *s,unsigned timeout,uint64_t now)
{
    int pair[2],one=1;pid_t owner=getpid(),child;
    char wait[16];
    if(!s||s->pid>0||s->pidfd>=0||s->channel>=0||
       s->state!=GKD_SESSION_IDLE||timeout<5000U||timeout>120000U||!now||geteuid()){
        errno=EINVAL;return -1;
    }
    if(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0,pair))return -1;
    if(setsockopt(pair[0],SOL_SOCKET,SO_PASSCRED,&one,sizeof(one))){
        int saved=errno;close(pair[0]);close(pair[1]);errno=saved;return -1;
    }
    snprintf(wait,sizeof(wait),"%u",timeout);
    child=fork();
    if(child<0){int saved=errno;close(pair[0]);close(pair[1]);errno=saved;return -1;}
    if(!child){
        struct sigaction action;sigset_t mask;
        memset(&action,0,sizeof(action));action.sa_handler=SIG_DFL;
        if(sigemptyset(&action.sa_mask)||sigaction(SIGTERM,&action,NULL)||
           sigemptyset(&mask)||sigprocmask(SIG_SETMASK,&mask,NULL))_exit(125);
        close(pair[0]);
        if(prctl(PR_SET_PDEATHSIG,SIGTERM)||getppid()!=owner)_exit(125);
        if(pair[1]!=3){if(dup2(pair[1],3)!=3)_exit(125);close(pair[1]);}
        if(fcntl(3,F_SETFD,0)||syscall(SYS_close_range,4U,~0U,0U))_exit(125);
        execl(GKD_APP_HOST_EXECUTABLE,GKD_APP_HOST_EXECUTABLE,wait,"--service-fd","3",(char *)NULL);
        _exit(126);
    }
    close(pair[1]);
    s->pid=child;s->channel=pair[0];
    s->pidfd=(int)syscall(SYS_pidfd_open,child,0U);
    s->last_clock=now;
    /* Host has separately bounded entropy, root, release, frame and controls
     * phases. Do not impose one shorter service deadline on their sum. */
    s->deadline=now+(uint64_t)timeout*5U+10000U;
    s->state=GKD_SESSION_STARTING;
    if(s->pidfd<0){
        s->error=errno;
        (void)gkd_app_session_stop(s,now);
        return -1; /* Caller must continue polling to reap the owned child. */
    }
    return 0;
}
static int ready_packet(struct gkd_app_session *s)
{
    struct gkd_app_service_ready packet;
    struct iovec io={&packet,sizeof(packet)};
    union {struct cmsghdr align;char data[CMSG_SPACE(sizeof(struct ucred))];} control;
    struct msghdr message;struct cmsghdr *c;struct ucred peer;
    memset(&message,0,sizeof(message));memset(&control,0,sizeof(control));
    message.msg_iov=&io;message.msg_iovlen=1;
    message.msg_control=control.data;message.msg_controllen=sizeof(control.data);
    ssize_t count=recvmsg(s->channel,&message,MSG_DONTWAIT);
    if(count<0&&(errno==EAGAIN||errno==EINTR))return 0;
    if(count!=(ssize_t)sizeof(packet)||(message.msg_flags&(MSG_TRUNC|MSG_CTRUNC))){errno=EPROTO;return -1;}
    c=CMSG_FIRSTHDR(&message);
    if(!c||c->cmsg_level!=SOL_SOCKET||c->cmsg_type!=SCM_CREDENTIALS||
       c->cmsg_len!=CMSG_LEN(sizeof(peer))||CMSG_NXTHDR(&message,c)){errno=EPROTO;return -1;}
    memcpy(&peer,CMSG_DATA(c),sizeof(peer));
    if(peer.pid!=s->pid||peer.uid!=0||packet.magic!=GKD_APP_SERVICE_MAGIC||
       packet.version!=1U||packet.host!=s->pid||packet.init<=0||packet.application<=0||
       packet.init==packet.host||packet.application==packet.host||packet.init==packet.application){
        errno=EPROTO;return -1;
    }
    s->ready=packet;
    return 1;
}
int gkd_app_session_stop(struct gkd_app_session *s,uint64_t now)
{
    if(!s||s->pid<=0||!now){errno=EINVAL;return -1;}
    if(s->state==GKD_SESSION_STOPPING)return 0;
    if(signal_child(s,SIGTERM)&&errno!=ESRCH){s->error=errno;return -1;}
    s->state=GKD_SESSION_STOPPING;s->deadline=now+6000U;s->last_clock=now;
    return 0;
}
enum gkd_app_session_state gkd_app_session_poll(struct gkd_app_session *s,uint64_t now)
{
    int status;pid_t got;
    if(!s)return GKD_SESSION_FAILED;
    if(s->pid<=0)return s->state;
    got=waitpid(s->pid,&status,WNOHANG);
    if(got==s->pid){
        s->status=status;s->pid=-1;
        s->state=(s->state==GKD_SESSION_STOPPING&&!s->forced&&!s->error&&
            WIFEXITED(status)&&WEXITSTATUS(status)==0)?GKD_SESSION_STOPPED:GKD_SESSION_FAILED;
        return s->state;
    }
    if(got<0&&errno!=EINTR){s->error=errno;s->state=GKD_SESSION_FAILED;return s->state;}
    if(!now||now<s->last_clock){
        s->error=EIO;
        (void)signal_child(s,SIGKILL);s->forced=1;s->state=GKD_SESSION_STOPPING;
        /* No advancing clock is needed to reap a killed direct child. */
        return s->state;
    }
    s->last_clock=now;
    if(s->state==GKD_SESSION_STARTING){
        int r=ready_packet(s);
        if(r>0)s->state=GKD_SESSION_READY;
        else if(r<0||now>=s->deadline){
            s->error=r<0?errno:ETIMEDOUT;
            if(gkd_app_session_stop(s,now)){(void)signal_child(s,SIGKILL);s->forced=1;s->state=GKD_SESSION_STOPPING;}
        }
    }
    if(s->state==GKD_SESSION_STOPPING&&now>=s->deadline&&!s->forced){
        s->error=ETIMEDOUT;
        (void)signal_child(s,SIGKILL);s->forced=1;
    }
    return s->state;
}
int gkd_app_session_close(struct gkd_app_session *s)
{
    if(!s){errno=EINVAL;return -1;}
    if(s->pid>0){errno=EBUSY;return -1;}
    if(s->pidfd>=0)close(s->pidfd);
    if(s->channel>=0)close(s->channel);
    *s=(struct gkd_app_session)GKD_APP_SESSION_INIT;
    return 0;
}
