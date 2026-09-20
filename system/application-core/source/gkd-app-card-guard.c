/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
/* Caller owns the card-operation lock and has stopped local card users. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/loop.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#ifndef GKD_CARD_PROC
#define GKD_CARD_PROC "/proc"
#define GKD_CARD_SYS "/sys"
#define GKD_CARD_DEV "/dev"
#endif
struct card { dev_t devices[32]; unsigned count; };
static int matches(const struct card *c, dev_t d)
{
    unsigned i;
    for(i=0;i<c->count;++i) if(c->devices[i]==d) return 1;
    return 0;
}
static int devfile(const char *path,dev_t *dev)
{
    unsigned maj,min; char extra; FILE *f=fopen(path,"r"); int n,error;
    if(!f)return -1;
    n=fscanf(f,"%u:%u %c",&maj,&min,&extra);error=ferror(f);
    if(fclose(f)||error||n!=2||maj>4095U||min>1048575U){errno=EPROTO;return -1;}
    *dev=makedev(maj,min);return 0;
}
static int discover(struct card *c,const char *name)
{
    char path[512]; struct stat st; DIR *d; struct dirent *e; int result=-1;
    dev_t sysdev;
    snprintf(path,sizeof(path),GKD_CARD_DEV "/%s",name);
    if(stat(path,&st)||!S_ISBLK(st.st_mode)){errno=ENODEV;return -1;}
    c->count=1;c->devices[0]=st.st_rdev;
    snprintf(path,sizeof(path),GKD_CARD_SYS "/class/block/%s/dev",name);
    if(devfile(path,&sysdev)||sysdev!=st.st_rdev){errno=ENODEV;return -1;}
    if(!strcmp(name,"mmcblk0p3")){
        snprintf(path,sizeof(path),GKD_CARD_DEV "/mmcblk0");
        if(stat(path,&st)||!S_ISBLK(st.st_mode)){errno=ENODEV;return -1;}
        snprintf(path,sizeof(path),GKD_CARD_SYS "/class/block/mmcblk0/dev");
        if(devfile(path,&sysdev)||sysdev!=st.st_rdev){errno=ENODEV;return -1;}
        c->devices[c->count++]=st.st_rdev;
    }
    snprintf(path,sizeof(path),GKD_CARD_SYS "/class/block/%s",name);
    d=opendir(path);if(!d)return -1;errno=0;
    while((e=readdir(d))){
        const char *p=e->d_name;size_t len=strlen(name);
        if(strncmp(p,name,len)||p[len]!='p')continue;
        const char *q=p+len+1;
        if(!*q){errno=EPROTO;goto out;}
        for(;*q;q++)if(*q<'0'||*q>'9'){errno=EPROTO;goto out;}
        if(c->count==32U){errno=E2BIG;goto out;}
        if(snprintf(path,sizeof(path),GKD_CARD_SYS "/class/block/%s/%s/dev",name,p)>=(int)sizeof(path)){errno=ENAMETOOLONG;goto out;}
        if(devfile(path,&c->devices[c->count]))goto out;
        ++c->count;errno=0;
    }
    if(errno)goto out;
    result=0;
out:
    {int saved=errno;closedir(d);errno=saved;}return result;
}
static int exited(int fd)
{
    struct pollfd p={fd,POLLIN,0};int n=poll(&p,1,0);
    if(n<0)return -1;
    if(p.revents&(POLLERR|POLLNVAL)){errno=EIO;return -1;}
    return n&&(p.revents&POLLIN);
}
static int mount_stream(FILE *f,const struct card *c)
{
    char *line=NULL;size_t cap=0;ssize_t n;int result=0;
    while((n=getline(&line,&cap,f))>=0){
        unsigned maj,min;unsigned long id,parent;
        if(!n||line[n-1]!='\n'||sscanf(line,"%lu %lu %u:%u",&id,&parent,&maj,&min)!=4){errno=EPROTO;result=-1;break;}
        if(matches(c,makedev(maj,min))){errno=EBUSY;result=-1;break;}
    }
    if(ferror(f))result=-1;
    free(line);return result;
}
static int mounts(const struct card *c)
{
    DIR *d=opendir(GKD_CARD_PROC);struct dirent *e;int result=-1;
    if(!d)return -1;
    errno=0;
    while((e=readdir(d))){
        char *end,path[128];long pid=strtol(e->d_name,&end,10);
        if(!*e->d_name||*end||pid<1||pid>2147483647L){errno=0;continue;}
        int pfd=(int)syscall(SYS_pidfd_open,(pid_t)pid,0U);
        if(pfd<0){if(errno==ESRCH){errno=0;continue;}goto out;}
        int dead=exited(pfd);
        if(dead){close(pfd);if(dead<0)goto out;errno=0;continue;}
        snprintf(path,sizeof(path),GKD_CARD_PROC "/%ld/mountinfo",pid);
        FILE *f=fopen(path,"r");int rc=f?mount_stream(f,c):-1;
        int saved=errno;
        if(f&&fclose(f)&&rc==0){rc=-1;saved=errno;}
        if(rc&&saved!=EBUSY){dead=exited(pfd);if(dead>0)rc=0;}
        close(pfd);errno=saved;
        if(rc)goto out;
        errno=0;
    }
    if(errno)goto out;
    result=0;
out:
    {int saved=errno;closedir(d);errno=saved;}return result;
}
static int swaps(const struct card *c)
{
    char line[2048],name[1024];struct stat st;FILE *f=fopen(GKD_CARD_PROC "/swaps","r");
    int result=-1;if(!f)return -1;
    if(!fgets(line,sizeof(line),f)||strncmp(line,"Filename",8)){errno=EPROTO;goto out;}
    while(fgets(line,sizeof(line),f)){
        if(!strchr(line,'\n')||sscanf(line,"%1023s",name)!=1||strchr(name,'\\')){errno=EPROTO;goto out;}
        if(stat(name,&st))goto out;
        if(matches(c,S_ISBLK(st.st_mode)?st.st_rdev:st.st_dev)){errno=EBUSY;goto out;}
    }
    if(ferror(f))goto out;
    result=0;
out:
    {int saved=errno;fclose(f);errno=saved;}return result;
}
static int loops(const struct card *c)
{
    DIR *d=opendir(GKD_CARD_SYS "/block");struct dirent *e;int result=-1;
    if(!d)return -1;
    errno=0;
    while((e=readdir(d))){
        char path[512],*end;struct loop_info64 info;int fd,rc,saved;
        if(strncmp(e->d_name,"loop",4)){errno=0;continue;}
        (void)strtoul(e->d_name+4,&end,10);
        if(!e->d_name[4]||*end){errno=EPROTO;goto out;}
        if(snprintf(path,sizeof(path),GKD_CARD_DEV "/%s",e->d_name)>=(int)sizeof(path)){errno=ENAMETOOLONG;goto out;}
        fd=open(path,O_RDONLY|O_CLOEXEC|O_NONBLOCK|O_NOFOLLOW);
        if(fd<0)goto out;
        rc=ioctl(fd,LOOP_GET_STATUS64,&info);saved=errno;close(fd);errno=saved;
        if(rc&&errno!=ENXIO)goto out;
        if(!rc&&(matches(c,(dev_t)info.lo_device)||
            (info.lo_rdevice&&matches(c,(dev_t)info.lo_rdevice)))){errno=EBUSY;goto out;}
        errno=0;
    }
    if(errno)goto out;
    result=0;
out:
    {int saved=errno;closedir(d);errno=saved;}return result;
}
int main(int argc,char **argv)
{
    struct card c={0};const char *stage="arguments";
    if(argc!=2||(strcmp(argv[1],"mmcblk0")&&strcmp(argv[1],"mmcblk1")&&
        strcmp(argv[1],"mmcblk0p3")))return 64;
    stage="identity";if(discover(&c,argv[1]))goto fail;
    stage="mounts";if(mounts(&c))goto fail;
    stage="swaps";if(swaps(&c))goto fail;
    stage="loops";if(loops(&c))goto fail;
    printf("GKD_APP_CARD=IDLE card=%s\n",argv[1]);return 0;
fail:
    fprintf(stderr,"GKD_APP_CARD=BLOCKED stage=%s errno=%d\n",stage,errno);return 1;
}
