/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-payload.h"
#include "gkd-app-fps-launch.h"
#include "gkd-fps-counter.h"
#include "gkd-app-game-control.h"
#include "gkd-app-menu-launch.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
struct context { char *const *argv;const char *directory;int parent_guard;struct gkd_app_fps_launch fps; };
static int fps_valid(const struct gkd_app_fps_launch *f)
{
    if(!f)return 0;
    if(f->executable_fd<3||f->counter_fd<3||f->lifetime_fd<3||!f->preload||
       strlen(f->session)!=32U)return 0;
    if(f->executable_fd==f->counter_fd||f->executable_fd==f->lifetime_fd||
       f->counter_fd==f->lifetime_fd)return 0;
    return 1;
}
static int fps_stage(const struct gkd_app_fps_launch *f)
{
    int staged[3]={-1,-1,-1},saved;
    for(unsigned i=0;i<3;i++){
        int source=i?i==1?f->counter_fd:f->lifetime_fd:f->executable_fd;
        staged[i]=fcntl(source,F_DUPFD_CLOEXEC,16);if(staged[i]<0)goto fail;
    }
    if(dup3(staged[0],3,O_CLOEXEC)<0||dup3(staged[1],4,O_CLOEXEC)<0||
       dup3(staged[2],5,O_CLOEXEC)<0)goto fail;
    for(unsigned i=0;i<3;i++)close(staged[i]);
    return syscall(SYS_close_range,6U,~0U,0U);
fail:saved=errno;for(unsigned i=0;i<3;i++)if(staged[i]>=0)close(staged[i]);errno=saved;return -1;
}
static void fps_environment(const struct gkd_app_fps_launch *f)
{
    char counter[24],lifetime[24];
    snprintf(counter,sizeof(counter),"%d",4);snprintf(lifetime,sizeof(lifetime),"%d",5);
    if(fcntl(4,F_SETFD,0)||fcntl(5,F_SETFD,0)||
       setenv("LD_PRELOAD",f->preload,1)||
       setenv(GKD_FPS_COUNTER_FD_ENV,counter,1)||
       setenv(GKD_FPS_LIFETIME_FD_ENV,lifetime,1)||
       setenv(GKD_FPS_SESSION_ENV,f->session,1)||
       setenv(GKD_FPS_PRELOAD_PATH_ENV,GKD_APP_FPS_INTERPOSER,1))_exit(126);
    unsetenv("GKD_PAYLOAD_PRELOAD");
}
static int worker(void *opaque)
{
    struct context *c=opaque;int fps=fps_valid(&c->fps);
    if(getpid()!=1||prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()!=0)return 125;
    struct pollfd parent={c->parent_guard,POLLIN,0};
    if(poll(&parent,1,0)!=0)return 125;
    close(c->parent_guard);
    if((fps?fps_stage(&c->fps):syscall(SYS_close_range,3U,~0U,0U)))return 125;
    if(mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL)||
       mount("proc","/proc","proc",MS_NOSUID|MS_NODEV|MS_NOEXEC,NULL)||
       chdir(c->directory))return 125;
    sigset_t none;sigemptyset(&none);
    if(sigprocmask(SIG_SETMASK,&none,NULL))return 125;
    struct sigaction sa;memset(&sa,0,sizeof(sa));sa.sa_handler=SIG_DFL;sigemptyset(&sa.sa_mask);
    for(int s=1;s<NSIG;s++)if(s!=SIGKILL&&s!=SIGSTOP)(void)sigaction(s,&sa,NULL);
    pid_t payload=fork();if(payload<0){if(fps){close(3);close(4);close(5);}return 125;}
    if(!payload){
        if(setsid()<0||seteuid(getuid()))_exit(126);
        if(fps){
            fps_environment(&c->fps);
            (void)syscall(SYS_execveat,3,"",c->argv,environ,AT_EMPTY_PATH);
        }else{
            const char *preload=getenv("GKD_PAYLOAD_PRELOAD");
            if(preload&&*preload){if(setenv("LD_PRELOAD",preload,1))_exit(126);}
            else unsetenv("LD_PRELOAD");
            unsetenv("GKD_PAYLOAD_PRELOAD");
            if(!access(c->argv[0],X_OK))execv(c->argv[0],c->argv);
            else execvp(c->argv[0],c->argv);
        }
        _exit(errno==ENOENT?127:126);
    }
    if(fps){close(3);close(4);close(5);}
    int primary=125,status;pid_t got;
    for(;;){
        got=waitpid(-1,&status,0);
        if(got<0){if(errno==EINTR)continue;if(errno==ECHILD)break;return 125;}
        if(got==payload)primary=WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
    }
    return primary;
}
int gkd_app_payload_run_menu(char *const argv[],const char *directory,int listener,
                        unsigned long long start,volatile sig_atomic_t *stopping,
                        struct gkd_app_fps_launch *fps,
                        struct gkd_app_menu_launch *menu,struct gkd_app_payload_result *r)
{
    if(!argv||!argv[0]||!directory||directory[0]!='/'||listener<0||
       fcntl(listener,F_GETFD)<0||!stopping||!r){errno=EINVAL;return -1;}
    memset(r,0,sizeof(*r));r->client=-1;r->reaped=1;
    int parent_guard=(int)syscall(SYS_pidfd_open,getpid(),0);if(parent_guard<0)return -1;
    void *stack=malloc(256*1024);if(!stack){close(parent_guard);return -1;}
    struct context c={argv,directory,parent_guard,GKD_APP_FPS_LAUNCH_INIT};
    if(fps_valid(fps))c.fps=*fps;
    pid_t init=clone(worker,(char*)stack+256*1024,CLONE_NEWPID|CLONE_NEWNS|SIGCHLD,&c);
    int saved=errno;close(parent_guard);
    if(init<0){free(stack);errno=saved;return -1;}
    if(fps_valid(fps))gkd_app_fps_launch_close(fps);
    r->reaped=0;
    int child=(int)syscall(SYS_pidfd_open,init,0);
    if(child<0){
        saved=errno;kill(init,SIGKILL);pid_t got;
        do got=waitpid(init,NULL,0);while(got<0&&errno==EINTR);
        r->reaped=got==init;free(stack);errno=saved;return -1;
    }
    int failure=0,sent=0,menu_client=-1;
    for(;;){
        struct pollfd fds[4]={{child,POLLIN,0},{listener,POLLIN,0},{menu?menu->fd:-1,POLLIN,0},{menu_client,0,0}};
        int rc=poll(fds,4,*stopping?0:-1);
        if(rc<0&&errno!=EINTR){failure=errno;*stopping=1;}
        if(rc>0&&((fds[0].revents&(POLLNVAL|POLLERR))||
           (fds[1].revents&(POLLNVAL|POLLERR|POLLHUP)))){failure=EBADF;*stopping=1;}
        if(rc>0&&(fds[2].revents&(POLLERR|POLLHUP|POLLNVAL))){failure=EIO;*stopping=1;}
        if(rc>0&&(fds[2].revents&POLLIN)&&menu_client<0){
            /* An unsolicited receipt is a protocol error, not a busy-poll loop. */
            failure=EPROTO;*stopping=1;
        }
        if(rc>0&&(fds[1].revents&POLLIN)){
            unsigned operation;int client=gkd_app_game_accept_operation(listener,start,&operation);
            if(client>=0){
                if(operation==GKD_GAME_EXIT){if(r->client<0){r->client=client;*stopping=1;}else (void)gkd_app_game_reply(client,EBUSY);}
                else if(*stopping||(fds[0].revents&POLLIN)) (void)gkd_app_game_reply_operation(client,GKD_GAME_MENU,ECANCELED);
                else if(menu_client>=0) (void)gkd_app_game_reply_operation(client,GKD_GAME_MENU,EBUSY);
                else if(gkd_app_menu_pulse(menu)) (void)gkd_app_game_reply_operation(client,GKD_GAME_MENU,errno);
                else menu_client=client;
            }
        }
        if(menu_client>=0){
            int error=0,finish=0;
            if(*stopping||(fds[0].revents&POLLIN)||(fds[3].revents&(POLLHUP|POLLERR|POLLNVAL))){
                error=ECANCELED;finish=1;
                if(gkd_app_menu_cancel(menu)){failure=errno;gkd_app_menu_close(menu);*stopping=1;}
            }else if(fds[2].revents){
                finish=1;
                if(fds[2].revents&(POLLERR|POLLHUP|POLLNVAL)){error=EIO;*stopping=1;}
                else if(gkd_app_menu_receipt(menu)){error=errno;*stopping=1;}
            }
            if(finish){(void)gkd_app_game_reply_operation(menu_client,GKD_GAME_MENU,error);menu_client=-1;}
        }
        if(*stopping&&!sent){
            if(syscall(SYS_pidfd_send_signal,child,SIGKILL,NULL,0)&&errno!=ESRCH)failure=errno;
            sent=1;r->forced=1;
        }
        pid_t got=waitpid(init,&r->wait_status,WNOHANG);
        if(got==init){r->reaped=1;break;}
        if(got<0&&errno!=EINTR){failure=errno;break;}
        if(sent){struct pollfd p={child,POLLIN,0};(void)poll(&p,1,20);}
    }
    if(gkd_app_menu_cancel(menu)&&!failure)failure=errno;
    if(menu_client>=0)(void)gkd_app_game_reply_operation(menu_client,GKD_GAME_MENU,ECANCELED);
    close(child);free(stack);
    if(failure){errno=failure;return -1;}return 0;
}

int gkd_app_payload_run(char *const argv[],const char *directory,int listener,
 unsigned long long start,volatile sig_atomic_t *stopping,struct gkd_app_fps_launch *fps,
 struct gkd_app_payload_result *result)
{return gkd_app_payload_run_menu(argv,directory,listener,start,stopping,fps,NULL,result);}
