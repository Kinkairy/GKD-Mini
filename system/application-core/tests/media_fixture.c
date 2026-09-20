#define _GNU_SOURCE
#include "gkd-app-media.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <linux/nsfs.h>
#include <poll.h>
#include <unistd.h>
#define R "/tmp/gkd-app-media-fixture"
#define MOCK_PID_LIMIT 128
static int fo,fs,fc,mut,cont[MOCK_PID_LIMIT];
static int mount_fail, mount_writer=-1, wait_eintr,readdir_fail,stop_signals,scan_churn,scan_next;
#include <sys/mount.h>
#include <sys/wait.h>
#include <sched.h>
int __real_poll(struct pollfd *,nfds_t,int); int __real_close(int);
static void w(char*p,char*s){int f=open(p,O_CREAT|O_TRUNC|O_WRONLY,0600);assert(f>=0);assert(write(f,s,strlen(s))==(ssize_t)strlen(s));close(f);}
static void t(int n,int pp,char st){char p[128],s[80];snprintf(p,sizeof(p),R "/%d",n);mkdir(p,0700);snprintf(s,sizeof(s),"State:\t%c\nPPid:\t%d\n",st,pp);snprintf(p,sizeof(p),R "/%d/status",n);w(p,s);snprintf(p,sizeof(p),R "/%d/mounts",n);w(p,"");snprintf(p,sizeof(p),R "/%d/root",n);mkdir(p,0700);snprintf(p,sizeof(p),R "/%d/ns",n);mkdir(p,0700);snprintf(p,sizeof(p),R "/%d/ns/pid",n);assert(!link(R "/ns",p)||errno==EEXIST);snprintf(p,sizeof(p),R "/%d/ns/mnt",n);assert(!link(R "/mntns",p)||errno==EEXIST);}
static void set(char st){if(mount_writer>=0){close(mount_writer);mount_writer=-1;}assert(!system("rm -rf " R));mkdir(R,0700);w(R "/ns","x");w(R "/mntns","m");t(10,1,'S');t(11,10,st);fo=fs=fc=mut=mount_fail=readdir_fail=stop_signals=scan_churn=0;scan_next=12;memset(cont,0,sizeof(cont));}
static void state(int n,char x){char p[128],s[64];snprintf(p,sizeof(p),R "/%d/status",n);snprintf(s,sizeof(s),"State:\t%c\nPPid:\t%d\n",x,n==10?1:10);w(p,s);}
static char observed_state(int n){char p[128],line[80],value=0;FILE *f;snprintf(p,sizeof(p),R "/%d/status",n);f=fopen(p,"r");assert(f);while(fgets(line,sizeof(line),f))if(!strncmp(line,"State:",6))value=line[7];assert(!fclose(f)&&value);return value;}
static void card_mount(void)
{
 char p[128];snprintf(p,sizeof(p),R "/10/mounts");
 w(p,"/dev/mmcblk1p1 /media/sdcard vfat rw,nosuid,nodev,noexec 0 0\n");
 mount_writer=open(p,O_WRONLY|O_CLOEXEC);assert(mount_writer>=0);
}
static void mount_text(const char *text)
{
 assert(mount_writer>=0);assert(!ftruncate(mount_writer,0));
 assert(pwrite(mount_writer,text,strlen(text),0)==(ssize_t)strlen(text));
}
long __wrap_syscall(long n,...){va_list v;int fd;char p[128];va_start(v,n);fd=va_arg(v,int);if(n==SYS_pidfd_open){va_end(v);if(fd==fo){errno=EMFILE;return-1;}return fd+100;}if(n==SYS_ioctl){unsigned long request=va_arg(v,unsigned long);struct stat a,b;va_end(v);if(request==NS_GET_NSTYPE){assert(!fstat(fd,&a)&&!stat(R "/mntns",&b));return a.st_dev==b.st_dev&&a.st_ino==b.st_ino?CLONE_NEWNS:CLONE_NEWPID;}errno=EPERM;return-1;}if(n==SYS_pidfd_send_signal){int sig=va_arg(v,int);va_end(v);fd-=100;snprintf(p,sizeof(p),R "/%d/status",fd);if(access(p,F_OK)){errno=ESRCH;return-1;}if(sig==19){stop_signals++;if(fd==fs){errno=EIO;return-1;}state(fd,'T');if(mut==1){mut=0;t(12,10,'S');}if(mut==2){mut=0;assert(!system("rm -rf " R "/11"));t(12,10,'S');}}if(sig==18){if(fd==fc){errno=EIO;return-1;}cont[fd]=1;state(fd,'S');}return 0;}va_end(v);errno=ENOSYS;return-1;}
struct dirent *__real_readdir(DIR *);
struct dirent *__wrap_readdir(DIR *directory)
{if(readdir_fail&&stop_signals){readdir_fail=0;errno=EIO;return NULL;}return __real_readdir(directory);}
int __real_closedir(DIR *);
int __wrap_closedir(DIR *directory)
{int result=__real_closedir(directory);if(!result&&scan_churn){t(scan_next++,10,'S');scan_churn--;}return result;}
int __wrap_poll(struct pollfd *p,nfds_t n,int timeout){if(n==1&&p[0].fd>=100&&p[0].fd<100+MOCK_PID_LIMIT){char x[128];(void)timeout;snprintf(x,sizeof(x),R "/%d/status",p[0].fd-100);p[0].revents=access(x,F_OK)?POLLIN:0;return p[0].revents?1:0;}return __real_poll(p,n,timeout);}
int __wrap_close(int fd){return fd>=100&&fd<100+MOCK_PID_LIMIT?0:__real_close(fd);}
int __wrap_setns(int fd,int flags){assert(fd>=0&&flags==CLONE_NEWNS);return 0;}
int __wrap_mount(const char *src,const char *target,const char *type,unsigned long flags,const void *data)
{
 assert(!strcmp(src,"/dev/mmcblk1p1")&&!strcmp(target,"/media/sdcard")&&!strcmp(type,"vfat"));
 assert(flags==(MS_NOSUID|MS_NODEV|MS_NOEXEC)&&!strcmp(data,"utf8"));
 if(mount_fail){errno=EIO;return -1;}
 mount_text("/dev/mmcblk1p1 /media/sdcard vfat rw,nosuid,nodev,noexec 0 0\n");return 0;
}
int __wrap_umount2(const char *target,int flags)
{assert(!strcmp(target,"/media/sdcard")&&!flags);mount_text("");return 0;}
pid_t __real_waitpid(pid_t,int *,int);
pid_t __wrap_waitpid(pid_t pid,int *status,int flags)
{if(wait_eintr){wait_eintr=0;errno=EINTR;return -1;}return __real_waitpid(pid,status,flags);}
static void bad(void){struct gkd_app_media m;gkd_app_media_init(&m);assert(gkd_app_media_pause(&m,10,1,10)<0);}
int main(void)
{
 struct gkd_app_media m;
 set('S');fo=11;bad();
 set('S');readdir_fail=1;gkd_app_media_init(&m);errno=0;
 assert(gkd_app_media_pause(&m,10,1,10)<0&&errno==EIO&&stop_signals==1);
 assert((cont[10]||cont[11])&&m.count==0&&!m.paused);
 set('S');for(int pid=12;pid<=73;pid++)t(pid,10,'S');gkd_app_media_init(&m);
 assert(!gkd_app_media_pause(&m,10,1,500)&&m.count==GKD_APP_MEDIA_MAX_TASKS&&m.view_count==1);
 assert(!gkd_app_media_resume(&m));for(int pid=10;pid<=73;pid++)assert(cont[pid]);gkd_app_media_close(&m);
 set('S');for(int pid=12;pid<=74;pid++)t(pid,10,'S');gkd_app_media_init(&m);errno=0;
 assert(gkd_app_media_pause(&m,10,1,500)<0&&errno==E2BIG&&m.count==0&&!m.paused);
 {int continued=0;for(int pid=10;pid<=74;pid++){continued+=!!cont[pid];assert(observed_state(pid)=='S');}assert(continued==64);}
 set('S');scan_churn=32;gkd_app_media_init(&m);errno=0;
 assert(gkd_app_media_pause(&m,10,1,500)<0&&errno==EAGAIN&&m.count==0&&!m.paused);
 for(int pid=10;pid<scan_next;pid++)assert(observed_state(pid)=='S');
 set('S');mut=1;gkd_app_media_init(&m);assert(!gkd_app_media_pause(&m,10,1,10));
 assert(m.count==3);assert(!gkd_app_media_resume(&m)&&cont[12]);gkd_app_media_close(&m);
 set('S');mut=2;gkd_app_media_init(&m);assert(!gkd_app_media_pause(&m,10,1,10));
 assert(m.count==2);assert(!gkd_app_media_resume(&m)&&cont[12]);gkd_app_media_close(&m);
 set('S');fs=11;bad();
 set('T');fs=10;bad();assert(!cont[11]);
 set('T');gkd_app_media_init(&m);
 assert(!gkd_app_media_pause(&m,10,1,10));
 assert(!gkd_app_media_resume(&m));assert(!cont[11]);gkd_app_media_close(&m);
 set('S');gkd_app_media_init(&m);assert(!gkd_app_media_pause(&m,10,1,10));
 fc=10;assert(gkd_app_media_resume(&m)<0&&m.paused);
 gkd_app_media_close(&m);assert(m.paused&&m.count);
 fc=0;assert(!gkd_app_media_resume(&m));gkd_app_media_close(&m);
 set('S');card_mount();gkd_app_media_init(&m);
 assert(!gkd_app_media_pause(&m,10,1,10));
 wait_eintr=1;assert(!gkd_app_media_game_unmount(&m)&&!wait_eintr);assert(m.mounted==0);
 assert(!gkd_app_media_game_mount(&m));assert(m.mounted==1);
 assert(!gkd_app_media_resume(&m));gkd_app_media_close(&m);
 set('S');card_mount();gkd_app_media_init(&m);assert(!gkd_app_media_pause(&m,10,1,10));
 mount_text("/dev/foreign /media/sdcard vfat rw,nosuid,nodev,noexec 0 0\n");
 errno=0;assert(gkd_app_media_game_unmount(&m)<0&&errno==EXDEV&&m.mounted==-1);
 errno=0;assert(gkd_app_media_probe(&m)<0&&errno==EXDEV);assert(gkd_app_media_resume(&m)<0);
 mount_text("/dev/mmcblk1p1 /media/sdcard vfat rw,nosuid,nodev,noexec 0 0\n");
 assert(!gkd_app_media_probe(&m)&&m.mounted==1);
 assert(!gkd_app_media_game_unmount(&m));mount_fail=1;
 errno=0;assert(gkd_app_media_game_mount(&m)<0&&errno==EIO&&m.mounted==-1);assert(gkd_app_media_resume(&m)<0);
 mount_fail=0;assert(!gkd_app_media_probe(&m)&&m.mounted==0);
 gkd_app_media_close(&m);assert(m.paused&&m.mounted==0&&m.view_count==1&&m.views[0].mountsfd>=0);
 assert(!gkd_app_media_game_mount(&m));assert(!gkd_app_media_resume(&m));gkd_app_media_close(&m);
 set('S');card_mount();gkd_app_media_init(&m);assert(!gkd_app_media_pause(&m,10,1,10));
 assert(!gkd_app_media_game_unmount(&m));
 assert(gkd_app_media_release_dead(&m)<0&&errno==EBUSY&&m.paused);
 assert(!unlink(R "/10/status"));
 assert(gkd_app_media_release_dead(&m)<0&&errno==EBUSY&&m.paused);
 assert(!unlink(R "/11/status"));
 assert(!gkd_app_media_release_dead(&m)&&!m.paused&&!m.count&&!m.view_count&&m.init_pidfd==-1);
 assert(!cont[10]&&!cont[11]);
 puts("GKD_APP_MEDIA_FIXTURE=PASS partial/replacement/readdir-error/task64/scan32/fixed-point-fork/stop/resume/original-stop/chroot/pinned-mounts/unmount/remount/foreign/unknown/wait-EINTR");
 return 0;
}
int __wrap___poll_chk(struct pollfd *p,nfds_t n,int timeout,size_t bytes) { (void)bytes;return __wrap_poll(p,n,timeout); }
