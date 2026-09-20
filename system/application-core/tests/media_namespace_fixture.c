/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-media.h"
#include <assert.h>
#include <dirent.h>
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
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define CARD "/out/card"
#define MAX_STACK (256U*1024U)

static int ready_pipe[2];

static void set_name(const char *name)
{assert(!prctl(PR_SET_NAME,name,0,0,0));}
static void read_exact(int fd,void *buffer,size_t length)
{
 size_t offset=0;
 while(offset<length){ssize_t got=read(fd,(char *)buffer+offset,length-offset);
  if(got<0&&errno==EINTR)continue;
  assert(got>0);offset+=(size_t)got;}
}
static void read_state(pid_t pid,char *state)
{
 char path[64],line[256];FILE *file;*state=0;
 snprintf(path,sizeof(path),"/proc/%ld/status",(long)pid);file=fopen(path,"r");assert(file);
 while(fgets(line,sizeof(line),file))if(!strncmp(line,"State:",6))*state=line[7];
 assert(!fclose(file)&&*state);
}
static void read_name(pid_t pid,char name[32])
{
 char path[64];FILE *file;snprintf(path,sizeof(path),"/proc/%ld/comm",(long)pid);
 file=fopen(path,"r");assert(file&&fgets(name,32,file));assert(!fclose(file));name[strcspn(name,"\n")]=0;
}
static int same_object(int left,int right)
{
 struct stat a,b;assert(!fstat(left,&a)&&!fstat(right,&b));return a.st_dev==b.st_dev&&a.st_ino==b.st_ino;
}
static int pidfd_dead(int fd)
{struct pollfd p={fd,POLLIN,0};int rc=poll(&p,1,0);assert(rc>=0);return rc>0;}

static void churner(void)
{
 set_name("gkdchurn");
 for(;;){pid_t child=fork();
  if(!child){set_name("gkdshort");usleep(500);_exit(0);}
  if(child>0){while(waitpid(child,NULL,0)<0&&errno==EINTR){}}
  else if(errno!=EAGAIN)_exit(121);
 }
}
static int nested_worker(void *unused)
{
 pid_t holder;(void)unused;set_name("gkdgameinit");assert(!mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL));
 holder=fork();assert(holder>=0);
 if(!holder){set_name("gkdbusy");assert(!chdir(CARD));assert(write(ready_pipe[1],"B",1)==1);for(;;)pause();}
 assert(write(ready_pipe[1],"N",1)==1);
 for(;;){pid_t got=waitpid(-1,NULL,0);if(got<0&&errno==EINTR)continue;if(got<0&&errno==ECHILD)pause();}
}
static int frontend_worker(void *unused)
{
 pid_t pre,churn,nested;void *stack;(void)unused;set_name("gkdfrontinit");
 assert(!mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL));
 pre=fork();assert(pre>=0);if(!pre){set_name("gkdprestop");raise(SIGSTOP);for(;;)pause();}
 assert(waitpid(pre,NULL,WUNTRACED)==pre);
 churn=fork();assert(churn>=0);if(!churn)churner();
 stack=malloc(MAX_STACK);assert(stack);
 nested=clone(nested_worker,(char *)stack+MAX_STACK,CLONE_NEWPID|CLONE_NEWNS|SIGCHLD,NULL);assert(nested>0);
 assert(write(ready_pipe[1],"F",1)==1);
 for(;;){pid_t got=waitpid(-1,NULL,0);if(got<0&&errno==EINTR)continue;if(got<0&&errno==ECHILD)pause();}
}
static int find_task(struct gkd_app_media *media,const char *wanted,int *pidfd)
{
 unsigned i;for(i=0;i<media->count;i++)if(!pidfd_dead(media->pidfds[i])){
  char name[32];read_name(media->pids[i],name);if(!strcmp(name,wanted)){if(pidfd)*pidfd=media->pidfds[i];return (int)media->pids[i];}
 }return -1;
}
static unsigned exact_namespace_count(pid_t root,int rootns,int *nested_seen)
{
 DIR *dir=opendir("/proc");struct dirent *entry;unsigned count=0;(void)root;assert(dir);*nested_seen=0;
 while((entry=readdir(dir))){char *end,path[64];long value=strtol(entry->d_name,&end,10);int fd;
  if(!entry->d_name[0]||*end||value<=0)continue;
  snprintf(path,sizeof(path),"/proc/%ld/ns/pid",value);fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)continue;
  if(same_object(fd,rootns)){char name[32];count++;read_name((pid_t)value,name);
   if(!strcmp(name,"gkdgameinit")||!strcmp(name,"gkdbusy"))*nested_seen=1;}
  close(fd);
 }
 assert(!closedir(dir));return count;
}
static int mount_present(pid_t pid)
{
 char path[64],line[4096];FILE *file;int found=0;
 snprintf(path,sizeof(path),"/proc/%ld/mounts",(long)pid);file=fopen(path,"r");assert(file);
 while(fgets(line,sizeof(line),file)){
  char source[256],target[4096],type[64],options[2048],extra;unsigned dump,pass;
  assert(sscanf(line,"%255s %4095s %63s %2047s %u %u %c",source,target,type,options,&dump,&pass,&extra)==6);
  if(!strcmp(target,CARD)){assert(!strcmp(source,"gkd-proof")&&!strcmp(type,"tmpfs"));found++;}
 }
 assert(!fclose(file));assert(found<=1);return found;
}

int main(void)
{
 void *stack;pid_t frontend,pre,busy;char ready[3],state;char pidns_path[64];int rootns,nested_seen,busy_pidfd;
 unsigned exact_count,owned_count,i;struct gkd_app_media media;
 assert(!geteuid());assert(!mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL));
 assert(!mkdir(CARD,0700)||errno==EEXIST);
 assert(!mount("gkd-proof",CARD,"tmpfs",MS_NOSUID|MS_NODEV|MS_NOEXEC,"size=1m"));
 assert(!pipe2(ready_pipe,O_CLOEXEC));stack=malloc(MAX_STACK);assert(stack);
 frontend=clone(frontend_worker,(char *)stack+MAX_STACK,CLONE_NEWPID|CLONE_NEWNS|SIGCHLD,NULL);assert(frontend>0);
 read_exact(ready_pipe[0],ready,sizeof(ready));usleep(20000);
 snprintf(pidns_path,sizeof(pidns_path),"/proc/%ld/ns/pid",(long)frontend);rootns=open(pidns_path,O_RDONLY|O_CLOEXEC);assert(rootns>=0);
 exact_count=exact_namespace_count(frontend,rootns,&nested_seen);assert(exact_count>=3&&!nested_seen);

 gkd_app_media_init(&media);assert(!gkd_app_media_pause(&media,frontend,getpid(),2000));
 assert(media.count>=5&&media.view_count==2);owned_count=media.count;
 pre=(pid_t)find_task(&media,"gkdprestop",NULL);assert(pre>0);
 busy=(pid_t)find_task(&media,"gkdbusy",&busy_pidfd);assert(busy>0);
 assert(find_task(&media,"gkdgameinit",NULL)>0&&find_task(&media,"gkdchurn",NULL)>0);
 for(i=0;i<media.count;i++){read_state(media.pids[i],&state);assert(state=='T'||state=='t');}

 errno=0;assert(gkd_app_media_game_unmount(&media)<0&&errno==EBUSY&&media.mounted==1);
 assert(!gkd_app_media_probe(&media)&&media.mounted==1);
 for(i=0;i<media.count;i++)if(!pidfd_dead(media.pidfds[i]))assert(mount_present(media.pids[i])==1);
 assert(!kill(busy,SIGKILL));
 for(i=0;i<2000&&!pidfd_dead(busy_pidfd);i++)usleep(1000);
 assert(pidfd_dead(busy_pidfd));

 assert(!gkd_app_media_game_unmount(&media)&&media.mounted==0);
 for(i=0;i<media.count;i++)if(!pidfd_dead(media.pidfds[i]))assert(mount_present(media.pids[i])==0);
 assert(mount_present(getpid())==1);
 assert(!gkd_app_media_game_mount(&media)&&media.mounted==1);
 for(i=0;i<media.count;i++)if(!pidfd_dead(media.pidfds[i]))assert(mount_present(media.pids[i])==1);
 assert(!gkd_app_media_resume(&media));gkd_app_media_close(&media);usleep(20000);
 read_state(pre,&state);assert(state=='T'||state=='t');

 printf("GKD_MEDIA_NS_PROOF exact=%u nested_omitted=1 owned=%u views=2 churn=1 prestopped=1 partial_errno=%d rollback_all_mounted=1 all_unmounted=1 remounted=1 foreign_view_retained=1\n",
        exact_count,owned_count,EBUSY);
 assert(!kill(frontend,SIGKILL));assert(waitpid(frontend,NULL,0)==frontend);
 close(rootns);close(ready_pipe[0]);close(ready_pipe[1]);free(stack);
 assert(!umount2(CARD,0));puts("GKD_APP_MEDIA_NAMESPACE_FIXTURE=PASS");return 0;
}
