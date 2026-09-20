/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-game-control.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/syscall.h>
#include <unistd.h>
#ifndef GKD_APP_GAME_CLIENT
#define GKD_APP_GAME_CLIENT "/var/run/gkd-app/gkd-app-game"
#endif
static int live(int fd)
{
    if(fd<0){errno=EBADF;return -1;}
    struct pollfd p={fd,POLLIN,0};
    if(poll(&p,1,0)==0)return 0;
    errno=ESRCH;return -1;
}
static int authorized_peer(pid_t pid)
{
    int pin=(int)syscall(SYS_pidfd_open,pid,0);if(pin<0)return -1;
    char path[64];snprintf(path,sizeof(path),"/proc/%ld/exe",(long)pid);
    int exe=open(path,O_PATH|O_CLOEXEC);
    int trusted=open(GKD_APP_GAME_CLIENT,O_PATH|O_CLOEXEC);
    struct stat a,b;
    int rc=exe>=0&&trusted>=0&&!fstat(exe,&a)&&!fstat(trusted,&b)&&
        a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&S_ISREG(b.st_mode)&&
        b.st_uid==0&&!(b.st_mode&0022)&&!live(pin)?0:-1;
    if(exe>=0)close(exe);
    if(trusted>=0)close(trusted);
    close(pin);if(rc)errno=EPERM;return rc;
}
#define GAME_MAGIC UINT32_C(0x474b4447)
struct packet { uint32_t magic, version, operation, reserved; uint64_t value; };
static int address(struct sockaddr_un *a, pid_t pid, unsigned long long start, socklen_t *size)
{
    struct stat st;
    if(pid<=1 || !start || stat("/proc/self/ns/pid",&st)) { errno=EINVAL;return -1; }
    memset(a,0,sizeof(*a));a->sun_family=AF_UNIX;
    int n=snprintf(a->sun_path+1,sizeof(a->sun_path)-1,"gkd-game.%llu.%ld.%llu",
                   (unsigned long long)st.st_ino,(long)pid,start);
    if(n<0 || (size_t)n>=sizeof(a->sun_path)-1) {errno=ENAMETOOLONG;return -1;}
    *size=offsetof(struct sockaddr_un,sun_path)+1+n;return 0;
}
static int wait_read(int fd,int milliseconds)
{
    struct pollfd p={fd,POLLIN,0};int rc;
    do rc=poll(&p,1,milliseconds);while(rc<0&&errno==EINTR);
    if(rc<=0){if(!rc)errno=ETIMEDOUT;return -1;}
    if(!(p.revents&POLLIN)){errno=ECONNRESET;return -1;}return 0;
}
int gkd_app_game_listen(unsigned long long start)
{
    struct sockaddr_un a;socklen_t n;
    if(address(&a,getpid(),start,&n))return -1;
    int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
    if(fd<0)return -1;
    if(bind(fd,(struct sockaddr*)&a,n)||listen(fd,1)){int e=errno;close(fd);errno=e;return -1;}
    return fd;
}
int gkd_app_game_accept_operation(int listener,unsigned long long start,unsigned *operation)
{
    if(!operation){errno=EINVAL;return -1;}
    int fd=accept4(listener,NULL,NULL,SOCK_CLOEXEC|SOCK_NONBLOCK);
    if(fd<0)return -1;
    struct ucred peer;socklen_t n=sizeof(peer);struct packet p;
    if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&n)||peer.uid!=0||peer.pid<=0||authorized_peer(peer.pid)||
       wait_read(fd,100)||recv(fd,&p,sizeof(p),MSG_TRUNC)!=(ssize_t)sizeof(p)||
       p.magic!=GAME_MAGIC||p.version!=2||p.reserved||
       (p.operation!=GKD_GAME_EXIT&&p.operation!=GKD_GAME_MENU)||p.value!=start) {
        int e=errno?errno:EPROTO;close(fd);errno=e;return -1;
    }
    *operation=p.operation;return fd;
}
int gkd_app_game_reply_operation(int client,unsigned operation,int error)
{
    if(client<0)return 0;
    struct packet p={GAME_MAGIC,2,operation,0,(uint64_t)(unsigned)error};
    int rc=send(client,&p,sizeof(p),MSG_NOSIGNAL)==(ssize_t)sizeof(p)?0:-1;
    int e=errno;close(client);errno=e;return rc;
}
int gkd_app_game_request_operation(pid_t pid,unsigned long long start,int pidfd,unsigned operation)
{
    struct sockaddr_un a;socklen_t n;int fd,rc=-1,e;
    if(operation!=GKD_GAME_EXIT&&operation!=GKD_GAME_MENU){errno=EINVAL;return -1;}
    if(live(pidfd)||address(&a,pid,start,&n))return -1;
    fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);if(fd<0)return -1;
    if(connect(fd,(struct sockaddr*)&a,n))goto out;
    struct ucred peer;socklen_t sz=sizeof(peer);
    if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&sz)||peer.uid||peer.pid!=pid||live(pidfd)){errno=EPERM;goto out;}
    struct packet p={GAME_MAGIC,2,operation,0,start};
    if(send(fd,&p,sizeof(p),MSG_NOSIGNAL)!=(ssize_t)sizeof(p)||wait_read(fd,4000))goto out;
    if(recv(fd,&p,sizeof(p),MSG_TRUNC)!=(ssize_t)sizeof(p)||p.magic!=GAME_MAGIC||p.version!=2||p.operation!=operation||p.reserved||p.value>4095){errno=EPROTO;goto out;}
    if(p.value){errno=(int)p.value;goto out;}rc=0;
out:e=errno;close(fd);errno=e;return rc;
}

int gkd_app_game_accept(int listener,unsigned long long start)
{
 unsigned operation;int fd=gkd_app_game_accept_operation(listener,start,&operation);
 if(fd>=0&&operation!=GKD_GAME_EXIT){close(fd);errno=EPROTO;return -1;}return fd;
}
int gkd_app_game_reply(int client,int error)
{return gkd_app_game_reply_operation(client,GKD_GAME_EXIT,error);}
int gkd_app_game_request(pid_t pid,unsigned long long start,int pidfd)
{return gkd_app_game_request_operation(pid,start,pidfd,GKD_GAME_EXIT);}

int gkd_app_game_wait(int active)
{
    struct sockaddr_un a={.sun_family=AF_UNIX};
    static const char path[]="/var/run/gkd-application/control.sock";
    const char *request=active>0?"game-wait-begin":active<0?"game-wait-fail":"game-wait-end";
    char reply[96];int result=-1,saved,fd;
    memcpy(a.sun_path,path,sizeof(path));
    fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
    if(fd<0)return -1;
    if(connect(fd,(struct sockaddr *)&a,sizeof(a))||
       send(fd,request,strlen(request),MSG_NOSIGNAL)!=(ssize_t)strlen(request)||wait_read(fd,500))goto done;
    ssize_t n=recv(fd,reply,sizeof(reply)-1,MSG_TRUNC);
    if(n<=0||n>=(ssize_t)sizeof(reply)){errno=EPROTO;goto done;}
    reply[n]=0;
    if(strcmp(reply,"GKD_APPLICATION_COMMAND=ACCEPTED errno=0\n")){errno=EPROTO;goto done;}
    result=0;
done:saved=errno;close(fd);errno=saved;return result;
}
