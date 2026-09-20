/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-job.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
static int time_ok(struct gkd_app_job *j,uint64_t now)
{
    if(!now||(j->last_clock&&now<j->last_clock)||now>UINT64_MAX-600000U){errno=EINVAL;return 0;}
    j->last_clock=now;return 1;
}
static int terminate(struct gkd_app_job *j,int sig)
{
    /* Direct child remains unreaped: PID is reserved even if pidfd allocation failed. */
    return j->pidfd>=0?(int)syscall(SYS_pidfd_send_signal,j->pidfd,sig,NULL,0):kill(j->pid,sig);
}
static void drain(struct gkd_app_job *j)
{
    if(j->outputfd<0)return;
    for(;;){
        char bytes[128];ssize_t n=read(j->outputfd,bytes,sizeof(bytes));
        if(n>0){
            if(memchr(bytes,0,(size_t)n)||j->used+(unsigned)n>=sizeof(j->output)){
                if(!j->error)j->error=EOVERFLOW;
            }else{
                memcpy(j->output+j->used,bytes,(size_t)n);j->used+=(unsigned)n;j->output[j->used]=0;
            }
        }else if(!n){close(j->outputfd);j->outputfd=-1;return;}
        else if(errno==EINTR)continue;
        else if(errno==EAGAIN)return;
        else {if(!j->error)j->error=errno;return;}
    }
}
static int start_job(struct gkd_app_job *j,char *const argv[],unsigned timeout,uint64_t now,int inherited,int transaction)
{
    int fds[2];pid_t parent=getpid(),pid;
    if(!j||j->state!=GKD_JOB_IDLE||j->pid>0||!argv||!argv[0]||argv[0][0]!='/'||
       (!timeout&&!transaction)||timeout>600000U||inherited < -1||!time_ok(j,now)){errno=EINVAL;return -1;}
    if(pipe2(fds,O_CLOEXEC))return -1;
    if(fcntl(fds[0],F_SETFL,O_NONBLOCK)){int e=errno;close(fds[0]);close(fds[1]);errno=e;return -1;}
    pid=fork();
    if(pid<0){int e=errno;close(fds[0]);close(fds[1]);errno=e;return -1;}
    if(!pid){
        struct sigaction a;sigset_t mask;int copy=-1;
        if(inherited>=0&&(copy=fcntl(inherited,F_DUPFD_CLOEXEC,5))<0)_exit(125);
        memset(&a,0,sizeof(a));a.sa_handler=SIG_DFL;sigemptyset(&a.sa_mask);sigemptyset(&mask);
        if(sigaction(SIGTERM,&a,NULL)||sigaction(SIGINT,&a,NULL)||sigaction(SIGHUP,&a,NULL)||
           sigprocmask(SIG_SETMASK,&mask,NULL)||prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()!=parent||
           dup2(fds[1],STDOUT_FILENO)<0)_exit(125);
        if(copy>=0&&(dup2(copy,3)!=3||fcntl(3,F_SETFD,0)))_exit(125);
        if(syscall(SYS_close_range,copy>=0?4U:3U,~0U,0U)<0)_exit(125);
        execv(argv[0],argv);_exit(127);
    }
    close(fds[1]);j->pid=pid;j->outputfd=fds[0];j->deadline=transaction?0:now+timeout;j->transaction=transaction;j->state=GKD_JOB_RUNNING;
    j->pidfd=(int)syscall(SYS_pidfd_open,pid,0);
    if(j->pidfd<0){j->error=errno;(void)gkd_app_job_cancel(j,now);}
    /* Fork succeeded: caller owns this child even if subsequent setup failed. */
    return 0;
}
int gkd_app_job_start_fd(struct gkd_app_job *j,char *const argv[],unsigned timeout,uint64_t now,int inherited)
{return start_job(j,argv,timeout,now,inherited,0);}
int gkd_app_job_start_transaction(struct gkd_app_job *j,char *const argv[],uint64_t now,int inherited)
{return start_job(j,argv,0U,now,inherited,1);}
int gkd_app_job_start(struct gkd_app_job *j,char *const argv[],unsigned timeout,uint64_t now)
{
    return gkd_app_job_start_fd(j,argv,timeout,now,-1);
}
int gkd_app_job_cancel(struct gkd_app_job *j,uint64_t now)
{
    if(!j||j->pid<=0||!time_ok(j,now)){errno=EINVAL;return -1;}
    if(j->transaction){errno=EBUSY;return -1;}
    if(j->state==GKD_JOB_STOPPING)return 0;
    if(terminate(j,SIGTERM)&&errno!=ESRCH)return -1;
    if(!j->error)j->error=ECANCELED;
    j->state=GKD_JOB_STOPPING;j->deadline=now+1000U;return 0;
}
enum gkd_app_job_state gkd_app_job_poll(struct gkd_app_job *j,uint64_t now)
{
    pid_t got;int status;
    if(!j)return GKD_JOB_FAILED;
    if(j->pid<=0)return j->state;
    if(!time_ok(j,now)){j->error=EINVAL;return j->state;}
    drain(j);
    do{got=waitpid(j->pid,&status,WNOHANG);}while(got<0&&errno==EINTR);
    if(got==j->pid){
        j->pid=-1;j->status=status;drain(j);
        if(j->outputfd>=0&&!j->error)j->error=EPROTO; /* descendant kept output: not a completed job */
        if((!WIFEXITED(status)||WEXITSTATUS(status))&&!j->error)j->error=ECHILD;
        j->state=j->error?GKD_JOB_FAILED:GKD_JOB_DONE;return j->state;
    }
    if(got<0){j->error=errno;j->state=GKD_JOB_FAILED;return j->state;}
    if(j->error&&!j->transaction&&j->state==GKD_JOB_RUNNING)(void)gkd_app_job_cancel(j,now);
    if(j->deadline&&now>=j->deadline){
        if(j->state==GKD_JOB_RUNNING){
            if(!j->error)j->error=ETIMEDOUT;
            (void)gkd_app_job_cancel(j,now);
        }else{
            if(terminate(j,SIGKILL)&&errno!=ESRCH)j->error=errno;
            j->deadline=now+1000U; /* ownership retained until an actual reap */
        }
    }
    return j->state;
}
int gkd_app_job_close(struct gkd_app_job *j)
{
    if(!j){errno=EINVAL;return -1;}
    if(j->pid>0){errno=EBUSY;return -1;}
    if(j->pidfd>=0)close(j->pidfd);
    if(j->outputfd>=0)close(j->outputfd);
    *j=(struct gkd_app_job)GKD_APP_JOB_INIT;return 0;
}
