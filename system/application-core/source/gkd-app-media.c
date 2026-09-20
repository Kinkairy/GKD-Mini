/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-media.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/nsfs.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef GKD_APP_MEDIA_PROC
#define GKD_APP_MEDIA_PROC "/proc"
#endif
#ifndef GKD_APP_MEDIA_SOURCE
#define GKD_APP_MEDIA_SOURCE "/dev/mmcblk1p1"
#endif
#ifndef GKD_APP_MEDIA_TARGET
#define GKD_APP_MEDIA_TARGET "/media/sdcard"
#endif
#ifndef GKD_APP_MEDIA_TYPE
#define GKD_APP_MEDIA_TYPE "vfat"
#endif
#ifndef GKD_APP_MEDIA_DATA
#define GKD_APP_MEDIA_DATA "utf8"
#endif
#ifndef GKD_APP_MEDIA_FLAGS
#define GKD_APP_MEDIA_FLAGS (MS_NOSUID|MS_NODEV|MS_NOEXEC)
#endif

#define GKD_APP_MEDIA_MAX_PIDNS_DEPTH 8U
#define GKD_APP_MEDIA_MAX_SCAN_PASSES 32U

enum media_action { MEDIA_UNMOUNT, MEDIA_MOUNT, MEDIA_PROBE };

static int pidfd_open_checked(pid_t p) { return (int)syscall(SYS_pidfd_open,p,0U); }
static int pidfd_signal(int fd,int s) { return (int)syscall(SYS_pidfd_send_signal,fd,s,NULL,0U); }
static int ns_ioctl(int fd,unsigned long request) { return (int)syscall(SYS_ioctl,fd,request); }

static int pidfd_dead(int fd)
{
 struct pollfd p={fd,POLLIN,0};int result;
 do result=poll(&p,1,0);while(result<0&&errno==EINTR);
 if(result<0)return -1;
 if(p.revents&(POLLERR|POLLNVAL)){errno=EIO;return -1;}
 return result>0&&!!(p.revents&(POLLIN|POLLHUP));
}
static int same_object(int a,int b)
{
 struct stat left,right;
 if(fstat(a,&left)||fstat(b,&right))return -1;
 return left.st_dev==right.st_dev&&left.st_ino==right.st_ino;
}
/* Linux nsfs gives PID namespaces NS_GET_PARENT ancestry. Mount namespaces
 * have no get_parent operation, so mount views are deduplicated by pinned
 * nsfs device/inode identity. */
static int owned_pidns(int candidate,int root)
{
 int current=dup(candidate);unsigned depth;
 if(current<0)return -1;
 for(depth=0;depth<=GKD_APP_MEDIA_MAX_PIDNS_DEPTH;depth++){
  int type=ns_ioctl(current,NS_GET_NSTYPE),same;
  if(type!=CLONE_NEWPID){int saved=type<0?errno:EPROTO;close(current);errno=saved;return -1;}
  same=same_object(current,root);
  if(same<0){int saved=errno;close(current);errno=saved;return -1;}
  if(same){close(current);return 1;}
  if(depth==GKD_APP_MEDIA_MAX_PIDNS_DEPTH){close(current);errno=E2BIG;return -1;}
  {int parent=ns_ioctl(current,NS_GET_PARENT),saved=errno;close(current);
   if(parent<0){if(saved==EPERM)return 0;errno=saved;return -1;}current=parent;}
 }
 close(current);errno=E2BIG;return -1;
}
static int status_ppid(pid_t p,pid_t *parent,char *state)
{
 char path[96],line[256];FILE *f;int have_parent=parent?0:1,have_state=state?0:1;
 snprintf(path,sizeof(path),GKD_APP_MEDIA_PROC "/%ld/status",(long)p);f=fopen(path,"r");if(!f)return -1;
 while(fgets(line,sizeof(line),f)){
  if(parent&&!strncmp(line,"PPid:",5)){*parent=(pid_t)strtol(line+5,NULL,10);have_parent=1;}
  if(state&&!strncmp(line,"State:",6)){*state=line[7];have_state=1;}
 }
 if(ferror(f)){int saved=errno;fclose(f);errno=saved;return -1;}
 if(fclose(f))return -1;
 if(!have_parent||!have_state){errno=EPROTO;return -1;}
 return 0;
}
static void close_tasks(struct gkd_app_media *m)
{unsigned i;for(i=0;i<m->count;i++)if(m->pidfds[i]>=0)close(m->pidfds[i]);m->count=0;}
static void close_views(struct gkd_app_media *m)
{
 unsigned i;for(i=0;i<m->view_count;i++){
  if(m->views[i].mountsfd>=0)close(m->views[i].mountsfd);
  if(m->views[i].mntnsfd>=0)close(m->views[i].mntnsfd);
  if(m->views[i].rootfd>=0)close(m->views[i].rootfd);
 }m->view_count=0;
}
void gkd_app_media_init(struct gkd_app_media *m)
{
 unsigned i;if(!m)return;*m=(struct gkd_app_media)GKD_APP_MEDIA_INIT;
 for(i=0;i<GKD_APP_MEDIA_MAX_TASKS;i++)m->pidfds[i]=-1;
 for(i=0;i<GKD_APP_MEDIA_MAX_VIEWS;i++)m->views[i]=(struct gkd_app_media_view){-1,-1,-1,0,0,-1};
}
static int task_index(struct gkd_app_media *m,pid_t pid)
{unsigned i;for(i=0;i<m->count;i++)if(m->pids[i]==pid)return (int)i;return -1;}
static int compact_dead(struct gkd_app_media *m)
{
 unsigned read,write=0;
 for(read=0;read<m->count;read++){
  int dead=pidfd_dead(m->pidfds[read]);if(dead<0)return -1;
  if(dead){close(m->pidfds[read]);continue;}
  if(write!=read){m->pids[write]=m->pids[read];m->pidfds[write]=m->pidfds[read];
   m->was_stopped[write]=m->was_stopped[read];m->stopped_by_us[write]=m->stopped_by_us[read];}
  write++;
 }
 m->count=write;return 0;
}
static int transient_dead(int pidfd,int saved)
{int dead;if(saved!=ENOENT&&saved!=ESRCH)return 0;dead=pidfd_dead(pidfd);return dead==1;}
static int scan_owned(struct gkd_app_media *m,unsigned *added)
{
 DIR *d;struct dirent *e;int scan_error=0;*added=0;if(compact_dead(m))return -1;d=opendir(GKD_APP_MEDIA_PROC);if(!d)return -1;
 for(;;){
  errno=0;e=readdir(d);if(!e){scan_error=errno;break;}
  char *end,path[96],state=0;long value=strtol(e->d_name,&end,10);pid_t pid;int pidfd,nsfd,owned,saved;
  if(!e->d_name[0]||*end||value<=0)continue;
  pid=(pid_t)value;
  if(task_index(m,pid)>=0)continue;
  pidfd=pidfd_open_checked(pid);
  if(pidfd<0){if(errno==ENOENT||errno==ESRCH)continue;saved=errno;closedir(d);errno=saved;return -1;}
  {int dead=pidfd_dead(pidfd);if(dead<0){saved=errno;close(pidfd);closedir(d);errno=saved;return -1;}if(dead){close(pidfd);continue;}}
  snprintf(path,sizeof(path),GKD_APP_MEDIA_PROC "/%ld/ns/pid",(long)pid);nsfd=open(path,O_RDONLY|O_CLOEXEC);
  if(nsfd<0){saved=errno;if(transient_dead(pidfd,saved)){close(pidfd);continue;}close(pidfd);closedir(d);errno=saved;return -1;}
  owned=owned_pidns(nsfd,m->pidnsfd);saved=errno;close(nsfd);
  if(owned<0){close(pidfd);closedir(d);errno=saved;return -1;}if(!owned){close(pidfd);continue;}
  if(status_ppid(pid,NULL,&state)){saved=errno;if(transient_dead(pidfd,saved)){close(pidfd);continue;}close(pidfd);closedir(d);errno=saved;return -1;}
  if(m->count==GKD_APP_MEDIA_MAX_TASKS){close(pidfd);closedir(d);errno=E2BIG;return -1;}
  if(state!='T'&&state!='t'&&pidfd_signal(pidfd,SIGSTOP)){
   saved=errno;if(saved==ESRCH){close(pidfd);continue;}close(pidfd);closedir(d);errno=saved;return -1;
  }
  m->pids[m->count]=pid;m->pidfds[m->count]=pidfd;
  m->was_stopped[m->count]=(state=='T'||state=='t');m->stopped_by_us[m->count]=!m->was_stopped[m->count];
  m->count++;(*added)++;
 }
 if(closedir(d)&&!scan_error)scan_error=errno;
 if(scan_error){errno=scan_error;return -1;}
 return 0;
}
static int deadline_expired(const struct timespec *start,unsigned timeout)
{
 struct timespec now;unsigned long long elapsed;
 if(clock_gettime(CLOCK_MONOTONIC,&now))return -1;
 elapsed=(unsigned long long)(now.tv_sec-start->tv_sec)*1000ULL;
 if(now.tv_nsec>=start->tv_nsec)elapsed+=(unsigned long long)(now.tv_nsec-start->tv_nsec)/1000000ULL;
 else elapsed-=(unsigned long long)(start->tv_nsec-now.tv_nsec)/1000000ULL;
 return elapsed>=timeout;
}
static int wait_stopped(struct gkd_app_media *m,const struct timespec *start,unsigned timeout)
{
 struct timespec nap={0,1000000};
 for(;;){
  unsigned i;int all=1,init_dead=pidfd_dead(m->init_pidfd);
  if(init_dead<0)return -1;
  if(init_dead){errno=ESRCH;return -1;}
  if(compact_dead(m))return -1;
  for(i=0;i<m->count;i++){
   char state;if(status_ppid(m->pids[i],NULL,&state)){
    int saved=errno;if(transient_dead(m->pidfds[i],saved)){all=0;break;}errno=saved;return -1;
   }
   if(state!='T'&&state!='t')all=0;
  }
  if(all)return 0;
  {int expired=deadline_expired(start,timeout);if(expired<0)return -1;if(expired){errno=ETIMEDOUT;return -1;}}
  while(nanosleep(&nap,&nap)&&errno==EINTR){}nap=(struct timespec){0,1000000};
 }
}
static int quiesce(struct gkd_app_media *m,unsigned timeout)
{
 struct timespec start;unsigned pass;if(clock_gettime(CLOCK_MONOTONIC,&start))return -1;
 for(pass=0;pass<GKD_APP_MEDIA_MAX_SCAN_PASSES;pass++){
  unsigned added;if(scan_owned(m,&added)||wait_stopped(m,&start,timeout))return -1;
  if(!added)return 0;
  {int expired=deadline_expired(&start,timeout);if(expired<0)return -1;if(expired){errno=ETIMEDOUT;return -1;}}
 }
 errno=EAGAIN;return -1;
}
static int has_option(const char *options,const char *key)
{
 size_t n=strlen(key);const char *p=options;
 while(*p){const char *end=strchr(p,',');size_t len=end?(size_t)(end-p):strlen(p);
  if(len==n&&!memcmp(p,key,n))return 1;
  if(!end)break;
  p=end+1;}
 return 0;
}
static int mounted_target(const struct gkd_app_media_view *v)
{
 int fd=dup(v->mountsfd),result=0;FILE *f;char *line=NULL;size_t cap=0;ssize_t n;
 if(fd<0)return -1;
 if(lseek(fd,0,SEEK_SET)<0){close(fd);return -1;}
 f=fdopen(fd,"r");if(!f){close(fd);return -1;}
 while((n=getline(&line,&cap,f))>=0){
  char source[256],target[4096],type[64],options[2048],extra;unsigned dump,pass;
  if(n>65536||!n||line[n-1]!='\n'||sscanf(line,"%255s %4095s %63s %2047s %u %u %c",source,target,type,options,&dump,&pass,&extra)!=6){errno=EPROTO;result=-1;break;}
  if(strcmp(target,GKD_APP_MEDIA_TARGET))continue;
  if(result||strcmp(source,GKD_APP_MEDIA_SOURCE)||strcmp(type,GKD_APP_MEDIA_TYPE)||
     !has_option(options,"rw")||!has_option(options,"nosuid")||!has_option(options,"nodev")||!has_option(options,"noexec")){
   errno=EXDEV;result=-1;break;
  }
  result=1;
 }
 if(ferror(f))result=-1;
 free(line);if(fclose(f))result=-1;
 return result;
}
static int collect_views(struct gkd_app_media *m)
{
 unsigned i=0;close_views(m);if(compact_dead(m))return -1;
 while(i<m->count){
  char path[96];struct stat st;int nsfd,rootfd,mountsfd,saved,type;unsigned v;
  snprintf(path,sizeof(path),GKD_APP_MEDIA_PROC "/%ld/ns/mnt",(long)m->pids[i]);nsfd=open(path,O_RDONLY|O_CLOEXEC);
  if(nsfd<0){saved=errno;if(transient_dead(m->pidfds[i],saved)){if(compact_dead(m))return -1;continue;}errno=saved;return -1;}
  type=ns_ioctl(nsfd,NS_GET_NSTYPE);
  if(type!=CLONE_NEWNS){saved=type<0?errno:EPROTO;close(nsfd);errno=saved;return -1;}
  if(fstat(nsfd,&st)){saved=errno;close(nsfd);errno=saved;return -1;}
  for(v=0;v<m->view_count;v++)if(m->views[v].ns_dev==st.st_dev&&m->views[v].ns_ino==st.st_ino)break;
  if(v<m->view_count){close(nsfd);i++;continue;}if(m->view_count==GKD_APP_MEDIA_MAX_VIEWS){close(nsfd);errno=E2BIG;return -1;}
  snprintf(path,sizeof(path),GKD_APP_MEDIA_PROC "/%ld/root",(long)m->pids[i]);rootfd=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
  if(rootfd<0){saved=errno;close(nsfd);if(transient_dead(m->pidfds[i],saved)){if(compact_dead(m))return -1;continue;}errno=saved;return -1;}
  snprintf(path,sizeof(path),GKD_APP_MEDIA_PROC "/%ld/mounts",(long)m->pids[i]);mountsfd=open(path,O_RDONLY|O_CLOEXEC);
  if(mountsfd<0){saved=errno;close(rootfd);close(nsfd);if(transient_dead(m->pidfds[i],saved)){if(compact_dead(m))return -1;continue;}errno=saved;return -1;}
  m->views[m->view_count]=(struct gkd_app_media_view){rootfd,nsfd,mountsfd,st.st_dev,st.st_ino,-1};m->view_count++;
  i++;
 }
 if(!m->view_count){errno=ESRCH;return -1;}
 return 0;
}
static int unstop(struct gkd_app_media *m)
{
 unsigned i;int bad=0;
 for(i=0;i<m->count;i++)if(m->stopped_by_us[i]){
  int dead=pidfd_dead(m->pidfds[i]);if(dead<0){bad=1;continue;}if(dead){m->stopped_by_us[i]=0;continue;}
  if(pidfd_signal(m->pidfds[i],SIGCONT)){bad=1;continue;}m->stopped_by_us[i]=0;
 }
 return bad?-1:0;
}
static void release(struct gkd_app_media *m)
{close_views(m);close_tasks(m);if(m->pidnsfd>=0)close(m->pidnsfd);if(m->init_pidfd>=0)close(m->init_pidfd);gkd_app_media_init(m);}
int gkd_app_media_pause(struct gkd_app_media *m,pid_t init,pid_t host,unsigned timeout)
{
 char path[96];pid_t parent;char state;int saved,type;
 if(!m||m->paused||m->init_pidfd>=0||init<=0||host<=0||!timeout){errno=EINVAL;return -1;}
 if(status_ppid(init,&parent,&state))return -1;
 if(parent!=host){errno=EPERM;return -1;}
 m->init_pidfd=pidfd_open_checked(init);if(m->init_pidfd<0)return -1;m->init_pid=init;m->host_pid=host;
 if(status_ppid(init,&parent,&state))goto fail;
 if(parent!=host){errno=EPERM;goto fail;}
 snprintf(path,sizeof(path),GKD_APP_MEDIA_PROC "/%ld/ns/pid",(long)init);m->pidnsfd=open(path,O_RDONLY|O_CLOEXEC);if(m->pidnsfd<0)goto fail;
 type=ns_ioctl(m->pidnsfd,NS_GET_NSTYPE);
 if(type!=CLONE_NEWPID){if(type>=0)errno=EPROTO;goto fail;}
 if(quiesce(m,timeout)||collect_views(m))goto fail;
 m->paused=1;m->mounted=1;return 0;
fail:
 saved=errno;if(unstop(m)){m->paused=1;m->mounted=1;errno=saved;return -1;}release(m);errno=saved;return -1;
}
struct action_result { int result,present,error; };
static int view_action(struct gkd_app_media *m,unsigned index,enum media_action action,int *present)
{
 int pipefd[2],status,saved;pid_t child,got;struct action_result result={-1,-1,0};ssize_t n;size_t off=0;
 if(pipe2(pipefd,O_CLOEXEC))return -1;
 child=fork();if(child<0){saved=errno;close(pipefd[0]);close(pipefd[1]);errno=saved;return -1;}
 if(!child){
  struct gkd_app_media_view *v=&m->views[index];close(pipefd[0]);
  if(setns(v->mntnsfd,CLONE_NEWNS)||fchdir(v->rootfd)||chroot(".")||chdir("/"))result.error=errno;
  else {
   result.present=mounted_target(v);
   if(result.present<0)result.error=errno;
   else if(action==MEDIA_PROBE)result.result=0;
   else if(action==MEDIA_MOUNT){
    if(result.present!=0)result.error=EINVAL;
    else if(mount(GKD_APP_MEDIA_SOURCE,GKD_APP_MEDIA_TARGET,GKD_APP_MEDIA_TYPE,GKD_APP_MEDIA_FLAGS,GKD_APP_MEDIA_DATA))result.error=errno;
    else if(mounted_target(v)!=1)result.error=errno?errno:EIO;else result.result=0;
   }else{
    if(result.present!=1)result.error=EINVAL;
    else if(umount2(GKD_APP_MEDIA_TARGET,0))result.error=errno;
    else if(mounted_target(v)!=0)result.error=errno?errno:EIO;else result.result=0;
   }
  }
  if(write(pipefd[1],&result,sizeof(result))!=(ssize_t)sizeof(result))_exit(127);
  _exit(0);
 }
 close(pipefd[1]);while(off<sizeof(result)){
  n=read(pipefd[0],(char *)&result+off,sizeof(result)-off);if(n<0&&errno==EINTR)continue;if(n<=0)break;off+=(size_t)n;
 }saved=errno;close(pipefd[0]);do got=waitpid(child,&status,0);while(got<0&&errno==EINTR);
 if(got!=child||!WIFEXITED(status)||WEXITSTATUS(status)||off!=sizeof(result)){errno=EIO;return -1;}
 if(result.result){errno=result.error?result.error:(saved?saved:EIO);return -1;}if(present)*present=result.present;return 0;
}
/* 0=all absent, 1=all exact mounts present, 2=mixed. */
static int audit_views(struct gkd_app_media *m,int *state)
{
 unsigned i;int first=-1;
 for(i=0;i<m->view_count;i++){
  int present;if(view_action(m,i,MEDIA_PROBE,&present))return -1;m->views[i].mounted=present;
  if(first<0)first=present;else if(first!=present)first=2;
 }
 if(first<0){errno=ESRCH;return -1;}*state=first;return 0;
}
static int repair_all_mounted(struct gkd_app_media *m)
{
 unsigned i;int state,first_error=0;
 if(audit_views(m,&state))return -1;
 if(state==1)return 0;
 for(i=0;i<m->view_count;i++)if(!m->views[i].mounted&&view_action(m,i,MEDIA_MOUNT,NULL)&&!first_error)first_error=errno;
 if(audit_views(m,&state)||state!=1){if(!first_error)first_error=errno?errno:EIO;errno=first_error;return -1;}
 return 0;
}
static int tasks_stopped(struct gkd_app_media *m)
{
 unsigned i;int dead=pidfd_dead(m->init_pidfd);if(dead<0)return -1;if(dead){errno=ESRCH;return -1;}
 for(i=0;i<m->count;i++){
  char state;dead=pidfd_dead(m->pidfds[i]);if(dead<0)return -1;if(dead)continue;
  if(status_ppid(m->pids[i],NULL,&state))return -1;
  if(state!='T'&&state!='t'){errno=EBUSY;return -1;}
 }
 return 0;
}
int gkd_app_media_probe(struct gkd_app_media *m)
{
 int state;if(!m||!m->paused){errno=EINVAL;return -1;}if(audit_views(m,&state)){m->mounted=-1;return -1;}
 if(state==0||state==1){m->mounted=state;return 0;}if(repair_all_mounted(m)){m->mounted=-1;return -1;}m->mounted=1;return 0;
}
int gkd_app_media_game_unmount(struct gkd_app_media *m)
{
 unsigned i;int state,saved;
 if(!m||!m->paused||m->mounted!=1){errno=EINVAL;return -1;}if(tasks_stopped(m))return -1;
 if(audit_views(m,&state)||state!=1){m->mounted=-1;if(!errno)errno=EXDEV;return -1;}
 for(i=0;i<m->view_count;i++)if(view_action(m,i,MEDIA_UNMOUNT,NULL)){
  saved=errno;if(repair_all_mounted(m))m->mounted=-1;else m->mounted=1;errno=saved;return -1;
 }
 if(audit_views(m,&state)||state!=0){m->mounted=-1;if(!errno)errno=EIO;return -1;}m->mounted=0;return 0;
}
int gkd_app_media_game_mount(struct gkd_app_media *m)
{
 unsigned i;int state,saved;
 if(!m||!m->paused||m->mounted!=0){errno=EINVAL;return -1;}
 if(audit_views(m,&state)||state!=0){m->mounted=-1;if(!errno)errno=EXDEV;return -1;}
 for(i=0;i<m->view_count;i++)if(view_action(m,i,MEDIA_MOUNT,NULL)){
  saved=errno;if(repair_all_mounted(m))m->mounted=-1;else m->mounted=1;errno=saved;return -1;
 }
 if(audit_views(m,&state)||state!=1){m->mounted=-1;if(!errno)errno=EIO;return -1;}m->mounted=1;return 0;
}
int gkd_app_media_resume(struct gkd_app_media *m)
{if(!m||!m->paused||m->mounted!=1){errno=EINVAL;return -1;}if(unstop(m))return -1;m->paused=0;close_tasks(m);return 0;}
void gkd_app_media_close(struct gkd_app_media *m)
{if(!m)return;if(m->paused&&m->mounted!=1)return;if(m->paused&&unstop(m))return;release(m);}

/* Card removal cannot resume an old game against replacement media. */
int gkd_app_media_release_dead(struct gkd_app_media *m)
{
 if(!m){errno=EINVAL;return -1;}
 if(m->init_pidfd<0)return 0;
 int dead=pidfd_dead(m->init_pidfd);
 if(dead!=1){if(!dead)errno=EBUSY;return -1;}
 for(unsigned i=0;i<m->count;i++){
  dead=pidfd_dead(m->pidfds[i]);
  if(dead!=1){if(!dead)errno=EBUSY;return -1;}
 }
 release(m);return 0;
}
