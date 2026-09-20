/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-fps-gate.h"
#include "gkd-app-fps-launch.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static int force_eagain,gate_calls;
long __real_syscall(long number,...);
long __wrap_syscall(long number,...)
{
    va_list args;va_start(args,number);
    if(number==SYS_getrandom){
        void *data=va_arg(args,void *);size_t bytes=va_arg(args,size_t);
        unsigned flags=va_arg(args,unsigned);va_end(args);
        assert(flags==1U);
        if(force_eagain){errno=EAGAIN;return -1;}
        memset(data,0x42,bytes);return (long)bytes;
    }
    if(number==SYS_memfd_create){
        const char *name=va_arg(args,const char *);unsigned flags=va_arg(args,unsigned);
        va_end(args);return __real_syscall(number,name,flags);
    }
    va_end(args);assert(0);errno=ENOSYS;return -1;
}
int gkd_app_fps_gate(int executable,int directory,const char *environment,
                     const struct gkd_app_fps_gate_paths *paths)
{
    (void)executable;(void)directory;(void)environment;(void)paths;
    gate_calls++;return 0;
}
int gkd_app_fps_test_random_session(unsigned char value[16]);
int gkd_app_fps_test_exchange(const char packet[47],int reader,int lifetime);
static unsigned long long milliseconds(void)
{
    struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));
    return (unsigned long long)t.tv_sec*1000U+(unsigned)t.tv_nsec/1000000U;
}
static int listening(void)
{
    const char *path="/tmp/gkd-fps-nonblock.sock";unlink(path);
    int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);assert(fd>=0);
    struct sockaddr_un address={.sun_family=AF_UNIX};strcpy(address.sun_path,path);
    assert(!bind(fd,(struct sockaddr *)&address,sizeof(address))&&!listen(fd,1));
    return fd;
}
static void disabled_server(int listener)
{
    int client=accept4(listener,NULL,NULL,SOCK_CLOEXEC);assert(client>=0);
    char data[64],control[CMSG_SPACE(2*sizeof(int))];struct iovec iov={data,sizeof(data)};
    struct msghdr message={0};message.msg_iov=&iov;message.msg_iovlen=1;
    message.msg_control=control;message.msg_controllen=sizeof(control);
    ssize_t n=recvmsg(client,&message,0);assert(n==47&&data[45]=='1');
    for(struct cmsghdr *cm=CMSG_FIRSTHDR(&message);cm;cm=CMSG_NXTHDR(&message,cm)){
        if(cm->cmsg_level==SOL_SOCKET&&cm->cmsg_type==SCM_RIGHTS){
            unsigned count=(unsigned)((cm->cmsg_len-CMSG_LEN(0))/sizeof(int));
            int *fds=(int *)CMSG_DATA(cm);for(unsigned i=0;i<count;i++)close(fds[i]);
        }
    }
    const char reply[]="GKD_APP_FPS=ACCEPTED inject=0\n";
    assert(send(client,reply,sizeof(reply)-1,MSG_NOSIGNAL)==(ssize_t)sizeof(reply)-1);
    close(client);
}
int main(void)
{
    unsigned char value[16]={0};force_eagain=1;
    assert(gkd_app_fps_test_random_session(value)<0&&errno==EAGAIN);
    force_eagain=0;
    int server=listening(),pair[2];assert(!pipe2(pair,O_CLOEXEC|O_NONBLOCK));
    const char packet[48]="fps-begin 1 0123456789abcdeffedcba9876543210 1\n";
    unsigned long long began=milliseconds();
    assert(!gkd_app_fps_test_exchange(packet,pair[0],pair[1]));
    unsigned long long elapsed=milliseconds()-began;
    assert(elapsed<250U);
    close(pair[0]);close(pair[1]);close(server);unlink("/tmp/gkd-fps-nonblock.sock");

    server=listening();pid_t child=fork();assert(child>=0);
    if(!child){disabled_server(server);_exit(0);}
    struct gkd_app_fps_launch launch=GKD_APP_FPS_LAUNCH_INIT;
    assert(!gkd_app_fps_launch_prepare(&launch,"/bin/true","/","/compat.so"));
    assert(launch.executable_fd<0&&launch.counter_fd<0&&launch.lifetime_fd<0&&
           !launch.preload&&!gate_calls);
    int status;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status));
    close(server);unlink("/tmp/gkd-fps-nonblock.sock");
    printf("GKD_FPS_LAUNCH_NONBLOCK=PASS getrandom-eagain exchange_ms=%llu disabled-gate-skipped=1\n",elapsed);
    return 0;
}
