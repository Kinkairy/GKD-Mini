/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-opk-plan.h"
#include "gkd-app-loop.h"
#include "gkd-app-payload.h"
#include "gkd-app-fps-launch.h"
#include "gkd-app-game-control.h"
#include "gkd-app-menu-launch.h"
#include "gkd-input-owner.h"
#include "gkd-app-display.h"
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifndef GKD_APP_LAUNCH_OWNER
#define GKD_APP_LAUNCH_OWNER "/var/run/gkd-app/loop-owner"
#endif
#ifndef GAME_DIR
#define GAME_DIR "/var/run/gkd-mini"
#endif
int gkd_app_game_self(unsigned long long *start);
static volatile sig_atomic_t stopping;
static void stop(int s){(void)s;stopping=1;}
static int read_owner(char owner[41])
{
    char data[42];struct stat st;
    int fd=open(GKD_APP_LAUNCH_OWNER,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -1;
    ssize_t n=read(fd,data,sizeof(data));
    int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&!st.st_uid&&!(st.st_mode&0022);
    close(fd);
    if(!ok||n!=41||data[40]!='\n'||memcmp(data,"gkd-app-",8)){errno=EPROTO;return -1;}
    for(unsigned i=8;i<40;i++)if(!((data[i]>='0'&&data[i]<='9')||(data[i]>='a'&&data[i]<='f'))){errno=EPROTO;return -1;}
    memcpy(owner,data,40);owner[40]=0;return 0;
}
static int registry(int dir,unsigned long long start,char expected[128])
{
    char temp[80];int n=snprintf(expected,128,"pid=%ld\npgid=%ld\nstarttime=%llu\n",(long)getpid(),(long)getpgrp(),start);
    if(n<0||n>=128){errno=EOVERFLOW;return -1;}
    snprintf(temp,sizeof(temp),"active-game.%ld.%llu",(long)getpid(),start);
    int fd=openat(dir,temp,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return -1;
    int rc=write(fd,expected,n)==n&&fsync(fd)==0?0:-1,e=errno;close(fd);
    if(!rc)rc=renameat(dir,temp,dir,"active-game");
    if(rc){e=errno;unlinkat(dir,temp,0);errno=e;}return rc;
}
static void registry_remove(int dir,const char *expected)
{
    char data[128];struct stat st;
    int fd=openat(dir,"active-game",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return;
    ssize_t n=read(fd,data,sizeof(data));int valid=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&!st.st_uid&&st.st_nlink==1;
    close(fd);
    if(valid&&n==(ssize_t)strlen(expected)&&!memcmp(data,expected,n))unlinkat(dir,"active-game",0);
}
static int restore(void)
{
    struct gkd_input_owner input;
    gkd_input_owner_init(&input);
    if(gkd_input_owner_open_menu(&input))return -1;
    /* The frontend invoker is waiting for us and cannot present yet.
     * Payload and loop cleanup grant sole display ownership; reuse the same
     * native A preparation as startup, then let the invoker resume the menu. */
    int rc=gkd_app_display_prepare(1000),e=errno;
    int closed=gkd_input_owner_close(&input);
    if(!rc&&closed)return -1;
    errno=e;return rc;
}
int main(int argc,char **argv)
{
    static const struct option options[]={{"metadata",required_argument,0,'m'},{"help",no_argument,0,'h'},{0,0,0,0}};
    const char *metadata=NULL;int c;
    while((c=getopt_long(argc,argv,"+hm:",options,NULL))!=-1){
        if(c=='h'){puts("gkd-app-launcher [-m desktop] OPK [ARGS...]");return 0;}
        if(c!='m')return 64;
        metadata=optarg;
    }
    if(geteuid()||optind>=argc||getpid()!=getpgrp()||getsid(0)!=getpid())return 64;
    /* The outer wrapper transfers preloads as data. No lifecycle helper may
     * load the game's SDL interposers into itself. */
    if(getenv("LD_PRELOAD")){errno=EPERM;perror("GKD_GAME=helper-preload");return 65;}
    unsigned long long start;char owner[41],expected[128]={0},target[300];
    if(gkd_app_game_self(&start)||read_owner(owner))return 65;
    int dir=open(GAME_DIR,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);struct stat st;
    if(dir<0)return 65;
    if(fstat(dir,&st)||!S_ISDIR(st.st_mode)||st.st_uid||(st.st_mode&0022)){close(dir);return 65;}
    int lock=openat(dir,"launcher.lock",O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
    if(lock<0||fstat(lock,&st)||!S_ISREG(st.st_mode)||st.st_uid||st.st_nlink!=1||(st.st_mode&0077)||flock(lock,LOCK_EX|LOCK_NB)){if(lock>=0)close(lock);close(dir);return 65;}
    int listener=gkd_app_game_listen(start);
    if(listener<0||registry(dir,start,expected)){if(listener>=0)close(listener);close(lock);close(dir);return 65;}
    struct sigaction sa;memset(&sa,0,sizeof(sa));sa.sa_handler=stop;sigemptyset(&sa.sa_mask);
    if(sigaction(SIGTERM,&sa,NULL)||sigaction(SIGINT,&sa,NULL)||sigaction(SIGHUP,&sa,NULL)||sigaction(SIGQUIT,&sa,NULL)){
        perror("GKD_GAME=signal");registry_remove(dir,expected);close(listener);close(lock);close(dir);return 65;
    }
    int notice=!gkd_app_game_wait(1);
    struct gkd_opk_plan plan={0};
    struct gkd_app_loop lease=GKD_APP_LOOP_INIT;
    struct gkd_app_payload_result result={0,-1,0,1};
    struct gkd_app_fps_launch fps=GKD_APP_FPS_LAUNCH_INIT;
    struct gkd_app_menu_launch menu=GKD_APP_MENU_LAUNCH_INIT;
    int image=-1,error=0,reaped=0,mounted=0,made=0,rc=125;
    image=open(argv[optind],O_RDONLY|O_CLOEXEC);
    if(image<0||fstat(image,&st)||!S_ISREG(st.st_mode)){error=errno?errno:EINVAL;goto done;}
    char image_path[64];snprintf(image_path,sizeof(image_path),"/proc/self/fd/%d",image);
    if(gkd_opk_plan_open(image_path,metadata,argc-optind-1,argv+optind+1,&plan)){error=errno;goto done;}
    if(gkd_app_menu_prepare(&menu,image,plan.desktop,plan.argv[0],argc-optind-1,argv+optind+1)){error=errno;goto done;}
    /* Native A uses the already enabled 320x240 software presentation and
     * dummy VT console; it has no optional legacy scaling/console switch.
     * Joystick remapping and gsensor are absent on this fixed board. */
    if(plan.needs_joystick||plan.needs_gsensor){error=ENOTSUP;goto done;}
    fprintf(stderr,"GKD_GAME=metadata terminal=%d downscaling=%d presentation=native-a\n",plan.needs_terminal,plan.needs_downscaling);
    if(snprintf(target,sizeof(target),"/mnt/%s",plan.mount_name)>=(int)sizeof(target)){error=ENAMETOOLONG;goto done;}
    if(stopping){error=ECANCELED;goto done;}
    if(gkd_app_loop_owned_count(owner)!=1){error=EBUSY;goto done;}
    if(unshare(CLONE_NEWNS)||mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL)){error=errno;goto done;}
    if(mkdir(target,0700)){error=errno;goto done;}made=1;
    if(gkd_app_loop_mount_owned(image,target,owner,&lease)){error=errno;goto done;}mounted=1;
    close(image);image=-1;
    (void)gkd_app_fps_launch_prepare(&fps,plan.argv[0],target,getenv("GKD_PAYLOAD_PRELOAD"));
    if(notice){(void)gkd_app_game_wait(0);notice=0;}
    int ran=gkd_app_payload_run_menu(plan.argv,target,listener,start,&stopping,&fps,&menu,&result);
    reaped=result.reaped;
    if(ran){error=errno;goto done;}
    rc=WIFEXITED(result.wait_status)?WEXITSTATUS(result.wait_status):128+WTERMSIG(result.wait_status);
done:;
    if(!notice)notice=!gkd_app_game_wait(1);
    /* A failed clear must keep the original lease fd and the launcher lock.
     * Holding it prevents a later launch from taking over an unresolved loop.
     * There is no alternate launcher or owner-scan cleanup in this path. */
    int cleanup_error=0;
    do {
        cleanup_error=0;
        if(mounted){
            if(!result.reaped)cleanup_error=ECHILD;
            else if(umount(target))cleanup_error=errno;
            else mounted=0;
        }
        if(!mounted&&lease.fd>=0&&gkd_app_loop_release(&lease))
            cleanup_error=errno;
        if(!mounted&&lease.fd<0)break;
        if(!error)error=cleanup_error?cleanup_error:EIO;
        if(notice){(void)gkd_app_game_wait(-1);notice=0;}
        fprintf(stderr,"GKD_GAME=cleanup-held errno=%d lease=%d mounted=%d\n",
                cleanup_error,lease.fd,mounted);
        (void)gkd_app_game_reply(result.client,cleanup_error?cleanup_error:EIO);
        result.client=-1;
        struct pollfd pending={listener,POLLIN,0};
        int polled=poll(&pending,1,500);
        if(polled>0&&(pending.revents&POLLIN))
        {
            unsigned operation;int client=gkd_app_game_accept_operation(listener,start,&operation);
            if(client>=0&&operation==GKD_GAME_MENU)(void)gkd_app_game_reply_operation(client,operation,EBUSY);
            else result.client=client;
        }
    } while(1);
    if(made&&rmdir(target)&&!error)error=errno;
    if(image>=0)close(image);
    gkd_app_fps_launch_close(&fps);
    gkd_app_menu_close(&menu);
    if(!notice)notice=!gkd_app_game_wait(1);
    if(reaped&&restore()&&!error)error=errno;
    gkd_opk_plan_close(&plan);
    fprintf(stderr,"GKD_GAME=complete reaped=%d forced=%d exit=%d cleanup_errno=%d\n",reaped,result.forced,rc,error);
    (void)gkd_app_game_reply(result.client,error);
    if(notice)(void)gkd_app_game_wait(error?-1:0);
    registry_remove(dir,expected);
    close(listener);close(lock);close(dir);
    return error?125:rc;
}
