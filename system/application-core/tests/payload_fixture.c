/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-payload.h"
#include "gkd-app-orientation.h"
#include "gkd-app-fps-launch.h"
#include "gkd-app-game-control.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static long long now(void){struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return t.tv_sec*1000LL+t.tv_nsec/1000000;}
static int pin(pid_t pid){int fd=syscall(SYS_pidfd_open,pid,0);assert(fd>=0);return fd;}
static char byte(int fd){char b;struct pollfd p={fd,POLLIN,0};assert(poll(&p,1,3000)>0);assert(read(fd,&b,1)==1);return b;}
int main(void)
{
    setbuf(stdout,NULL);
    volatile sig_atomic_t stopping=0;
    struct gkd_app_payload_result r;
    char *empty[]={"/bin/true",NULL};
    assert(gkd_app_payload_run(empty,"/",-1,123,&stopping,NULL,&r)==-1&&errno==EINVAL);
    for(int mode=0;mode<6;mode++){
        int ready[2],output[2];assert(!pipe(ready)&&!pipe(output));
        long long began=now();pid_t server=fork();assert(server>=0);
        if(!server){
            assert(setsid()>0);close(ready[0]);close(output[0]);
            assert(dup2(output[1],1)==1);close(output[1]);
            int listener=gkd_app_game_listen(123456);assert(listener>=0);
            assert(write(ready[1],"r",1)==1);close(ready[1]);
            char *normal[]={"/bin/sh","-c","sleep 0.1 & (sleep 0.25 &) ; exit 7",NULL};
            char *force[]={"/bin/sh","-c","trap '' TERM; setsid sh -c 'trap "" TERM; echo R; sleep 30' & setsid sh -c 'trap "" TERM; echo R; sleep 30' & echo R; wait",NULL};
            char *failed[]={"/no-gkd-executable",NULL};
            char *fdcheck[]={"/bin/sh","-c","test ! -e /proc/self/fd/3",NULL};
            char *instrumented[]={"/bin/sh","-c","test -e /proc/self/fd/4 && test -e /proc/self/fd/5 && test -e /proc/self/fd/6 && test ! -e /proc/self/fd/7",NULL};
            struct gkd_app_fps_launch fps=GKD_APP_FPS_LAUNCH_INIT;
            if(mode==5){int lifetime[2];assert(!pipe(lifetime));close(lifetime[0]);
                fps.executable_fd=open("/bin/sh",O_RDONLY|O_CLOEXEC);fps.counter_fd=open("/dev/null",O_RDWR|O_CLOEXEC);
                fps.lifetime_fd=lifetime[1];fps.orientation_fd=open("/dev/zero",O_RDWR|O_CLOEXEC);
                strcpy(fps.session,"0123456789abcdef0123456789abcdef");fps.preload=strdup("");}
            char **args=mode==5?instrumented:mode==0?normal:(mode==1||mode==4)?force:mode==2?failed:fdcheck;
            struct gkd_app_orientation orientation=GKD_APP_ORIENTATION_INIT;
            if(mode==1){orientation.launch.aspect=GKD_ASPECT_PORTRAIT;orientation.launch.hint=GKD_HINT_RIGHT;
                orientation.launch.source=GKD_ORIGIN_LYNX_CRC;orientation.launch.scope=GKD_SCOPE_LAUNCH_ROM;}
            assert(!gkd_app_payload_run_orientation(args,"/",listener,123456,&stopping,mode==5?&fps:NULL,NULL,&orientation,&r));
            int code=WIFEXITED(r.wait_status)?WEXITSTATUS(r.wait_status):128+WTERMSIG(r.wait_status);
            assert(code==(mode==0?7:mode==1?137:mode==2?127:0));
            assert(r.forced==(mode==1));printf("COMPLETE %d\n",code);
            assert(!gkd_app_game_reply(r.client,0));close(listener);_exit(0);
        }
        close(ready[1]);close(output[1]);assert(byte(ready[0])=='r');close(ready[0]);
        int server_fd=pin(server);
        if(mode==1||mode==4){
            int count=0;while(count<3){char c=byte(output[0]);if(c=='R')count++;}
            if(mode==4){assert(!kill(server,SIGKILL));}
            else {
            assert(gkd_app_game_request(server,123457,server_fd)==-1); /* wrong incarnation: no listener */
            struct pollfd p={server_fd,POLLIN,0};assert(poll(&p,1,0)==0);
            struct gkd_game_orientation orientation=GKD_GAME_ORIENTATION_INIT;
            for(unsigned query=0;query<3;query++){
                assert(!gkd_app_game_request_orientation(server,123456,server_fd,&orientation));
                assert(orientation.aspect==GKD_ASPECT_PORTRAIT&&orientation.hint==GKD_HINT_RIGHT&&orientation.scope==GKD_SCOPE_LAUNCH_ROM);
            }
            assert(!gkd_app_game_request(server,123456,server_fd));
            }
        }
        char data[256];size_t n=0;ssize_t got;
        while((got=read(output[0],data+n,sizeof(data)-1-n))>0){n+=got;assert(n<sizeof(data)-1);}
        assert(got==0);data[n]=0;assert(mode==4?!strstr(data,"COMPLETE "):!!strstr(data,"COMPLETE "));
        int status;assert(waitpid(server,&status,0)==server);
        assert(mode==4?(WIFSIGNALED(status)&&WTERMSIG(status)==SIGKILL):(WIFEXITED(status)&&!WEXITSTATUS(status)));
        if(mode==0)assert(now()-began>=200);
        struct gkd_game_orientation dead=GKD_GAME_ORIENTATION_INIT;
        assert(gkd_app_game_request_orientation(server,123456,server_fd,&dead)==-1);
        close(server_fd);close(output[0]);
        printf("GKD_PAYLOAD_REAL=PASS case=%d eof=1 duration_ms=%lld\n",mode,now()-began);
    }
    return 0;
}
