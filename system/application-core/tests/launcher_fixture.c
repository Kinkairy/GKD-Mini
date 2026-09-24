/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#define GAME_DIR "/tmp/gkd-native-game-test"
#define GKD_APP_LAUNCH_OWNER GAME_DIR "/owner"
#define gkd_app_menu_prepare fixture_menu_prepare
#define gkd_app_game_wait fixture_game_wait
#include <stdlib.h>
#include "gkd-app-storage.h"
/* Storage uses a separate real-mount fixture; this fixture owns lifecycle. */
static int fixture_storage_game(void){return 0;}
#define gkd_storage_game fixture_storage_game
#define main launcher_main
#include "../source/gkd-app-launcher.c"
#undef main
#undef gkd_storage_game
#undef gkd_app_game_wait
#undef gkd_app_menu_prepare
#include <assert.h>
#include <time.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <stdarg.h>
static int waiting_active;
int fixture_game_wait(int active)
{
 assert(active>=-1&&active<=1);assert((active>0)!=waiting_active);
 waiting_active=active>0;return 0;
}
/* This existing fixture isolates mount/display/lifetime failures. The MENU
 * loader/VT backend has separate tests; no real kernel device exists here. */
int fixture_menu_prepare(struct gkd_app_menu_launch *m,int opk,const char *desktop,const char *exec,int argc,char *const argv[])
{(void)argc;(void)argv;(void)m;(void)desktop;assert(waiting_active&&opk>=0&&exec);return 0;}
static int mode, clears;
static int fixture_file(const char *name)
{
    char path[256];snprintf(path,sizeof(path),GAME_DIR "/%s",name);
    return open(path,O_WRONLY|O_CREAT|O_TRUNC,0600);
}
int gkd_app_game_self(unsigned long long *start)
{
    FILE *f=fopen("/proc/self/stat","r");char buf[4096];
    if(!f)return -1;
    char *read=fgets(buf,sizeof(buf),f);fclose(f);if(!read)return -1;
    char *s=strrchr(buf,')')+2,*save=NULL,*token=strtok_r(s," ",&save);
    for(int i=3;i<22&&token;i++)token=strtok_r(NULL," ",&save);
    if(!token)return -1;
    *start=strtoull(token,NULL,10);return 0;
}
static int input_held, display_fds[1024], active_vt=1;
static int vt_graphics[3]={KD_TEXT,KD_GRAPHICS,KD_TEXT}, switch_locked=1;
void gkd_input_owner_init(struct gkd_input_owner *o){memset(o,0,sizeof(*o));}
int gkd_input_owner_open_menu(struct gkd_input_owner *o){(void)o;assert(!input_held);input_held=1;return 0;}
int gkd_input_owner_close(struct gkd_input_owner *o){(void)o;assert(input_held);input_held=0;return 0;}
/* Run the production display function without a frontend process.
 * Only tty syscalls are modeled; retain actual ordering, deadline and errors. */
static int display_open(const char *path,int flags)
{
    assert(waiting_active && input_held && !access(GAME_DIR "/released",F_OK));
    assert((flags&O_CLOEXEC) && (flags&O_NOCTTY));
    assert(!strncmp(path,"/dev/tty",8));
    int tty=atoi(path+8);assert(tty>=0&&tty<=2);
    int fd=open("/dev/null",O_RDWR|O_CLOEXEC);assert(fd>=0&&fd<1024);
    display_fds[fd]=tty+1;return fd;
}
static int display_close(int fd)
{
    assert(fd>=0&&fd<1024&&display_fds[fd]);
    display_fds[fd]=0;return close(fd);
}
static int display_ioctl(int fd,unsigned long request,...)
{
    assert(input_held&&fd>=0&&fd<1024&&display_fds[fd]);
    int tty=display_fds[fd]-1;
    va_list args;va_start(args,request);
    if(request==VT_GETSTATE){
        struct vt_stat *s=va_arg(args,struct vt_stat *);memset(s,0,sizeof(*s));s->v_active=active_vt;
    }else if(request==VT_GETMODE){
        struct vt_mode *m=va_arg(args,struct vt_mode *);memset(m,0,sizeof(*m));
        m->mode=mode>=5?VT_PROCESS:VT_AUTO;
    }else if(request==KDGETMODE){
        int *d=va_arg(args,int *);*d=vt_graphics[tty];
    }else{
        unsigned long value=va_arg(args,unsigned long);
        if(request==KDSETMODE){assert(value==KD_TEXT);vt_graphics[tty]=KD_TEXT;}
        else if(request==VT_UNLOCKSWITCH){
            assert(!tty&&!value&&vt_graphics[active_vt]==KD_TEXT&&vt_graphics[2]==KD_TEXT);
            switch_locked=0;
        }else if(request==VT_ACTIVATE){
            assert(!tty&&value==2&&!switch_locked);active_vt=2;
            int mark=fixture_file("restored");assert(mark>=0);close(mark);
        }else assert(0);
    }
    va_end(args);return 0;
}
#define open display_open
#define close display_close
#define ioctl display_ioctl
#include "../source/gkd-app-display.c"
#undef ioctl
#undef close
#undef open
int gkd_app_loop_owned_count(const char *owner){assert(strlen(owner)==40);return 1;}
int gkd_app_loop_mount_owned(int image,const char *target,const char *owner,struct gkd_app_loop *l)
{
    (void)owner;l->fd=dup(image);assert(l->fd>=0);
    if(mode==3){errno=EIO;return -1;}
    return mount("tmpfs",target,"tmpfs",MS_NOSUID|MS_NODEV,"size=1m");
}
int gkd_app_loop_release(struct gkd_app_loop *l)
{
    assert(l->fd>=0);
    if((mode==3||mode==4)&&!clears++){errno=EBUSY;return -1;}
    close(l->fd);l->fd=-1;
    int fd=fixture_file("released");assert(fd>=0);close(fd);return 0;
}
int gkd_opk_plan_open(const char *path,const char *metadata,int argc,char *const argv[],struct gkd_opk_plan *plan)
{
    (void)metadata;(void)argv;assert(argc==0);
    int fd=open(path,O_RDONLY);assert(fd>=0);close(fd);
    plan->mount_name=strdup("gkd-native-fixture");
    plan->argv[0]=strdup(mode==2?"/missing-gkd-executable":"/bin/sh");
    if(mode!=2){
        plan->argv[1]=strdup("-c");
        plan->argv[2]=strdup((mode==1||mode==5)?
            "setsid sh -c 'echo ready >" GAME_DIR "/ready; sleep 30' & wait":
            "sleep 0.2 & (sleep 0.3 &) ; exit 7");
    }
    return 0;
}
void gkd_opk_plan_close(struct gkd_opk_plan *p)
{
    for(int i=0;p->argv[i];i++)free(p->argv[i]);
    free(p->mount_name);
}
static long long now(void){struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return t.tv_sec*1000LL+t.tv_nsec/1000000;}
static void remove_outputs(void)
{
    const char *names[]={"restored","released","ready"};
    for(unsigned i=0;i<3;i++){char path[256];snprintf(path,sizeof(path),GAME_DIR "/%s",names[i]);unlink(path);}
}
int main(void)
{
    assert(!mkdir(GAME_DIR,0700));
    int fd=fixture_file("owner");assert(fd>=0);
    assert(dprintf(fd,"gkd-app-0123456789abcdef0123456789abcdef%c",10)==41);close(fd);
    fd=fixture_file("image");assert(fd>=0);close(fd);
    for(mode=0;mode<7;mode++){
        remove_outputs();
        long long began=now();pid_t pid=fork();assert(pid>=0);
        if(!pid){assert(setsid()>0);char *args[]={"opkrun",GAME_DIR "/image",NULL};_exit(launcher_main(2,args));}
        int pin=(int)syscall(SYS_pidfd_open,pid,0);assert(pin>=0);
        if(mode==1||mode==5){
            for(int i=0;access(GAME_DIR "/ready",F_OK)&&i<500;i++)usleep(10000);
            assert(!access(GAME_DIR "/ready",F_OK));
            unsigned long long start=0;long actual,pgid;
            FILE *f=fopen(GAME_DIR "/active-game","r");assert(f);
            assert(fscanf(f,"pid=%ld pgid=%ld starttime=%llu",&actual,&pgid,&start)==3);fclose(f);
            assert(actual==pid&&pgid==pid);
            int requested=gkd_app_game_request(pid,start,pin);
            if(mode==5)assert(requested==-1&&errno==EBUSY);
            else assert(requested==0);
        }
        int status;assert(waitpid(pid,&status,0)==pid&&WIFEXITED(status));
        assert(WEXITSTATUS(status)==(mode==0?7:mode==1?137:mode==2?127:125));
        close(pin);
        assert(access(GAME_DIR "/active-game",F_OK)&&errno==ENOENT);
        assert(!access(GAME_DIR "/released",F_OK));
        if(mode!=3&&mode<5)assert(!access(GAME_DIR "/restored",F_OK));
        else assert(access(GAME_DIR "/restored",F_OK)&&errno==ENOENT);
        if(mode==3||mode==4)assert(now()-began>=450);
        printf("GKD_LAUNCHER_REAL=PASS mode=%d duration_ms=%lld\n",mode,now()-began);
    }
    return 0;
}
