/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-fps-launch.h"
#include "gkd-app-fps-gate.h"
#include "gkd-fps-counter.h"
#include "gkd-app-fps-build.generated.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/memfd.h>
#include <linux/random.h>
#ifndef GRND_NONBLOCK
#define GRND_NONBLOCK 0x0001
#endif
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>
#ifndef GKD_APP_FPS_SOCKET
#define GKD_APP_FPS_SOCKET "/var/run/gkd-application/control.sock"
#endif
#ifndef GKD_APP_FPS_INTERPOSER
#define GKD_APP_FPS_INTERPOSER "/var/run/gkd-app/libgkd-fps-present.so"
#endif
#define FPS_PACKET_BYTES 47U
void gkd_app_fps_launch_init(struct gkd_app_fps_launch *x)
{
    if(x)*x=(struct gkd_app_fps_launch)GKD_APP_FPS_LAUNCH_INIT;
}
void gkd_app_fps_launch_close(struct gkd_app_fps_launch *x)
{
    if(!x)return;
    if(x->executable_fd>=0)close(x->executable_fd);
    if(x->counter_fd>=0)close(x->counter_fd);
    if(x->lifetime_fd>=0)close(x->lifetime_fd);
    if(x->orientation_fd>=0)close(x->orientation_fd);
    free(x->preload);gkd_app_fps_launch_init(x);
}
static int random_session(unsigned char value[16])
{
    size_t done=0;unsigned attempts=0;
    while(done<16&&attempts++<32U){
        ssize_t n=syscall(SYS_getrandom,value+done,16-done,GRND_NONBLOCK);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)return -1;
        done+=(size_t)n;
    }
    if(done!=16){errno=EINTR;return -1;}
    unsigned any=0;for(unsigned i=0;i<16;i++)any|=value[i];
    if(!any){errno=EIO;return -1;}return 0;
}
#ifdef GKD_APP_FPS_TESTING
int gkd_app_fps_test_random_session(unsigned char value[16])
{
    return random_session(value);
}
#endif
static int open_exec(const char *name,int directory)
{
    if(name[0]=='/')return open(name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    return openat(directory,name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
}
static int exchange(const char packet[FPS_PACKET_BYTES],int reader,int lifetime)
{
    struct sockaddr_un a={.sun_family=AF_UNIX};strcpy(a.sun_path,GKD_APP_FPS_SOCKET);
    int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);if(fd<0)return 0;
    int flags=fcntl(fd,F_GETFL);
    if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)||
       connect(fd,(struct sockaddr *)&a,sizeof(a))){close(fd);return 0;}
    struct ucred peer;socklen_t peer_bytes=sizeof(peer);
    if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&peer_bytes)||peer_bytes!=sizeof(peer)||peer.uid){
        close(fd);return 0;
    }
    int rights[2]={reader,lifetime};char control[CMSG_SPACE(sizeof(rights))];
    struct iovec iov={(void *)packet,FPS_PACKET_BYTES};struct msghdr m={0};
    m.msg_iov=&iov;m.msg_iovlen=1;m.msg_control=control;m.msg_controllen=sizeof(control);
    struct cmsghdr *cm=CMSG_FIRSTHDR(&m);cm->cmsg_level=SOL_SOCKET;cm->cmsg_type=SCM_RIGHTS;
    cm->cmsg_len=CMSG_LEN(sizeof(rights));memcpy(CMSG_DATA(cm),rights,sizeof(rights));
    if(sendmsg(fd,&m,MSG_NOSIGNAL)!=(ssize_t)FPS_PACKET_BYTES){close(fd);return 0;}
    struct pollfd p={fd,POLLIN,0};int result=0;
    if(poll(&p,1,50)>0&&(p.revents&POLLIN)){
        char reply[64];ssize_t n=recv(fd,reply,sizeof(reply)-1,MSG_TRUNC);
        if(n>0&&n<(ssize_t)sizeof(reply)){reply[n]=0;
            if(!strcmp(reply,"GKD_APP_FPS=ACCEPTED inject=1\n"))result=1;}
    }
    close(fd);return result;
}
#ifdef GKD_APP_FPS_TESTING
int gkd_app_fps_test_exchange(const char packet[FPS_PACKET_BYTES],int reader,int lifetime)
{
    return exchange(packet,reader,lifetime);
}
#endif
static char *preload_value(const char *existing)
{
    size_t a=strlen(GKD_APP_FPS_INTERPOSER),b=existing?strlen(existing):0;
    if(a+b+2U>4096U){errno=E2BIG;return NULL;}
    char *value=malloc(a+(b?b+1U:0U)+1U);if(!value)return NULL;
    memcpy(value,GKD_APP_FPS_INTERPOSER,a);
    if(b){value[a++]=':';memcpy(value+a,existing,b);a+=b;}
    value[a]=0;return value;
}
int gkd_app_fps_launch_prepare_orientation(struct gkd_app_fps_launch *out,const char *name,
                               const char *directory,const char *existing,int orientation,int *lifetime)
{
    if(!out||!name||!*name||!directory||directory[0]!='/'){errno=EINVAL;return -1;}
    gkd_app_fps_launch_init(out);
    if(lifetime)*lifetime=-1;
    if(orientation>=3&&!lifetime){errno=EINVAL;return -1;}
    int producer=-1,reader=-1,pipefd[2]={-1,-1},executable=-1,working=-1;
    void *mapping=MAP_FAILED;
    unsigned char session[16];char packet[FPS_PACKET_BYTES+1],path[64];
    if(random_session(session))goto unavailable;
    producer=(int)syscall(SYS_memfd_create,"gkd-fps",MFD_CLOEXEC|MFD_ALLOW_SEALING);
    if(producer<0||ftruncate(producer,GKD_FPS_COUNTER_BYTES))goto unavailable;
    mapping=mmap(NULL,GKD_FPS_COUNTER_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,producer,0);
    if(mapping==MAP_FAILED)goto unavailable;
    struct gkd_fps_counter_page page={GKD_FPS_COUNTER_MAGIC,GKD_FPS_COUNTER_VERSION,
        GKD_FPS_COUNTER_BYTES,0,0,0,0,GKD_FPS_PRODUCER_UNAVAILABLE,{0}};
    for(unsigned i=0;i<8;i++)page.session_hi=(page.session_hi<<8)|session[i];
    for(unsigned i=8;i<16;i++)page.session_lo=(page.session_lo<<8)|session[i];
    memcpy(mapping,&page,sizeof(page));
    if(fcntl(producer,F_ADD_SEALS,F_SEAL_SEAL|F_SEAL_SHRINK|F_SEAL_GROW))goto unavailable;
    snprintf(path,sizeof(path),"/proc/self/fd/%d",producer);
    reader=open(path,O_RDONLY|O_CLOEXEC);if(reader<0||pipe2(pipefd,O_CLOEXEC|O_NONBLOCK))goto unavailable;
    static const char digits[]="0123456789abcdef";memcpy(packet,"fps-begin 1 ",12);
    for(unsigned i=0;i<16;i++){packet[12+2*i]=digits[session[i]>>4];packet[13+2*i]=digits[session[i]&15];}
    /* Register every game first. The broker replies inject=0 while disabled,
     * avoiding ELF reads and hashing on the highest-priority launch path. */
    packet[44]=' ';packet[45]='1';packet[46]=10;packet[47]=0;
    int inject=exchange(packet,reader,pipefd[0]),eligible=0;
    close(reader);reader=-1;
    if(inject||orientation>=3){
        working=open(directory,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        if(working>=0)executable=open_exec(name,working);
        struct gkd_app_fps_gate_paths paths={"/usr/lib/libSDL-1.2.so.0",
            "/usr/lib/libSDL-1.2.so.0.11.4",GKD_APP_FPS_SDL_SHA256,
            GKD_APP_FPS_INTERPOSER,GKD_APP_FPS_INTERPOSER_SHA256};
        eligible=executable>=0&&working>=0&&
            gkd_app_fps_gate(executable,working,getenv("LD_LIBRARY_PATH"),&paths)==1;
        if(working>=0){close(working);working=-1;}
    }
    munmap(mapping,GKD_FPS_COUNTER_BYTES);mapping=MAP_FAILED;
    if((inject||orientation>=3)&&eligible){
        char *preload=preload_value(existing);if(!preload)goto unavailable;
        if(orientation>=3){out->orientation_fd=fcntl(orientation,F_DUPFD_CLOEXEC,3);
            if(out->orientation_fd<0){free(preload);goto unavailable;}}
        out->executable_fd=executable;executable=-1;out->counter_fd=producer;producer=-1;
        out->lifetime_fd=pipefd[1];pipefd[1]=-1;out->preload=preload;
        if(orientation>=3){*lifetime=pipefd[0];pipefd[0]=-1;}
        memcpy(out->session,packet+12,32);out->session[32]=0;
    }
unavailable:{int saved=errno;
    if(mapping!=MAP_FAILED)munmap(mapping,GKD_FPS_COUNTER_BYTES);
    if(producer>=0)close(producer);
    if(reader>=0)close(reader);
    if(pipefd[0]>=0)close(pipefd[0]);
    if(pipefd[1]>=0)close(pipefd[1]);
    if(executable>=0)close(executable);
    if(working>=0)close(working);
    errno=saved;
    return 0;
    }
}

int gkd_app_fps_launch_prepare(struct gkd_app_fps_launch *out,const char *name,const char *directory,const char *existing)
{return gkd_app_fps_launch_prepare_orientation(out,name,directory,existing,-1,NULL);}
