/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-controls.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line=%d errno=%d: %s\n",__LINE__,errno,#x); return 1; } } while (0)
static int spawn(struct gkd_app_controls *c, int hung)
{
    int fds[2];
    CHECK(pipe2(fds,O_CLOEXEC|O_NONBLOCK)==0);
    c->pid=fork(); CHECK(c->pid>=0);
    if (!c->pid) {
        close(fds[0]);
        if (hung && signal(SIGTERM,SIG_IGN)==SIG_ERR) _exit(125);
        if (write(fds[1],"R",1)!=1) _exit(125);
        for (;;) pause();
    }
    close(fds[1]); c->readyfd=fds[0];
    c->pidfd=(int)syscall(SYS_pidfd_open,c->pid,0); CHECK(c->pidfd>=0);
    c->state=GKD_APP_CONTROLS_WAITING; c->last_now_ms=100; c->deadline_ms=200;
    { struct pollfd p={c->readyfd,POLLIN,0}; CHECK(poll(&p,1,1000)==1); }
    return 0;
}
static int reap(struct gkd_app_controls *c, uint64_t now, int expected)
{
    for (unsigned i=0;i<100 && c->pid>0;++i) {
        (void)gkd_app_controls_stop(c,now+i);
        if (c->pid>0) (void)poll(NULL,0,2);
    }
    CHECK(c->pid<0 && c->state==GKD_APP_CONTROLS_REAPED);
    CHECK(WIFSIGNALED(c->exit_status) && WTERMSIG(c->exit_status)==expected);
    gkd_app_controls_close(c); return 0;
}
int main(void)
{
    struct gkd_app_controls c=GKD_APP_CONTROLS_INIT;
    CHECK(spawn(&c,0)==0);
    CHECK(gkd_app_controls_poll(&c,200)==GKD_APP_CONTROLS_FAILED && errno==ETIMEDOUT);
    CHECK(reap(&c,201,SIGTERM)==0); /* queued R cannot beat an expired deadline */
    CHECK(spawn(&c,0)==0);
    CHECK(gkd_app_controls_poll(&c,99)==GKD_APP_CONTROLS_FAILED && errno==EINVAL);
    CHECK(reap(&c,201,SIGTERM)==0);
    CHECK(spawn(&c,1)==0);
    CHECK(gkd_app_controls_poll(&c,199)==GKD_APP_CONTROLS_READY);
    CHECK(gkd_app_controls_stop(&c,200)==GKD_APP_CONTROLS_STOPPING);
    CHECK(gkd_app_controls_stop(&c,1200)==GKD_APP_CONTROLS_STOPPING);
    CHECK(reap(&c,1201,SIGKILL)==0);
    CHECK(spawn(&c,0)==0);
    close(c.pidfd); c.pidfd=-1; c.state=GKD_APP_CONTROLS_FAILED;
    CHECK(reap(&c,201,SIGTERM)==0); /* still own/reap child when pidfd allocation fails */
    CHECK(spawn(&c,0)==0);
    { pid_t pid=c.pid; int deadfd=c.pidfd; c.pidfd=-1;
      CHECK(kill(pid,SIGKILL)==0 && waitpid(pid,NULL,0)==pid);
      c.pid=-1; gkd_app_controls_close(&c);
      CHECK(gkd_app_controls_start(&c,deadfd,pid,100)<0);
      CHECK(c.pid<0 && c.statefd<0); close(deadfd);
      CHECK(gkd_app_controls_start(&c,-1,pid,100)<0);
    }
    puts("GKD_CONTROLS_LIFECYCLE=PASS deadline clock forced-reap missing-pidfd stale-init");
    return 0;
}
