/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-fps.h"
#include "gkd-app-namespace.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#define FPS_PREFIX "fps-begin "
#define FPS_PACKET_BYTES 47U
#define FPS_LAUNCHER "/usr/libexec/gkd-app-launcher"
#define FPS_ACTIVE "/var/run/gkd-mini/active-game"
static void descriptors(int values[2])
{
    for(unsigned i=0;i<2;i++)if(values[i]>=0){close(values[i]);values[i]=-1;}
}
void gkd_app_fps_init(struct gkd_app_fps *f)
{
    if(f)*f=(struct gkd_app_fps)GKD_APP_FPS_INIT;
}
void gkd_app_fps_close(struct gkd_app_fps *f)
{
    if(!f)return;
    if(f->page)munmap((void *)f->page,GKD_FPS_COUNTER_BYTES);
    if(f->page_fd>=0)close(f->page_fd);
    if(f->lifetime_fd>=0)close(f->lifetime_fd);
    if(f->peer_pidfd>=0)close(f->peer_pidfd);
    gkd_app_fps_init(f);
}
#ifndef GKD_APP_FPS_TEST_AUTH
static int same_path(const char *a,const char *b)
{
    struct stat x,y;int one=open(a,O_PATH|O_CLOEXEC),two=open(b,O_PATH|O_CLOEXEC),ok=0;
    if(one>=0&&two>=0&&!fstat(one,&x)&&!fstat(two,&y)&&x.st_dev==y.st_dev&&x.st_ino==y.st_ino)ok=1;
    int saved=errno;if(one>=0)close(one);if(two>=0)close(two);errno=saved;return ok;
}
static int last_nspid(pid_t pid,pid_t *value)
{
    char path[64],data[4096],*line,*end;snprintf(path,sizeof(path),"/proc/%ld/status",(long)pid);
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -1;
    ssize_t n=read(fd,data,sizeof(data)-1);int saved=errno;close(fd);
    if(n<=0||n==(ssize_t)sizeof(data)-1){errno=n<0?saved:EPROTO;return -1;}
    data[n]=0;line=!strncmp(data,"NSpid:\t",7)?data:strstr(data,"\nNSpid:\t");
    if(!line){errno=EPROTO;return -1;}if(*line=='\n')line++;line+=7;long last=0;
    while(*line&&*line!='\n'){
        while(*line==' '||*line=='\t')line++;
        if(!*line||*line=='\n')break;
        errno=0;long item=strtol(line,&end,10);
        if(errno||end==line||item<=0||item>INT32_MAX){errno=EPROTO;return -1;}
        last=item;line=end;
    }
    if(last<=1){errno=EPROTO;return -1;}*value=(pid_t)last;return 0;
}
static int marker(pid_t init,pid_t inner,const struct gkd_app_process_identity *identity)
{
    char path[128],data[160],extra;struct stat st;long pid=0,pgid=0;unsigned long long start=0;
    snprintf(path,sizeof(path),"/proc/%ld/root%s",(long)init,FPS_ACTIVE);
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -1;
    ssize_t n=read(fd,data,sizeof(data)-1),tail=n>=0?read(fd,&extra,1):-1;int saved=errno;
    int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&!st.st_uid&&!(st.st_mode&0022);close(fd);
    if(n<=0||tail||!ok){errno=n<0?saved:EPROTO;return -1;}data[n]=0;
    char trailing;
    if(sscanf(data,"pid=%ld\npgid=%ld\nstarttime=%llu\n%c",&pid,&pgid,&start,&trailing)!=3||
       pid!=inner||pgid!=inner||start!=identity->starttime){errno=EPERM;return -1;}
    return 0;
}
static int authorize(const struct ucred *peer,const struct gkd_app_fps_context *x,
                     int *pidfd,unsigned long long *start)
{
    char a[96],b[160];pid_t inner;struct gkd_app_process_identity identity;
    if(!peer||!x||peer->uid||peer->pid<=1||x->host<=1||x->init<=1||
       !x->lifecycle_allows_game){errno=EPERM;return -1;}
    *pidfd=(int)syscall(SYS_pidfd_open,peer->pid,0U);if(*pidfd<0)return -1;
    if(gkd_app_process_identity_read(peer->pid,&identity)||
       identity.pgid!=peer->pid||identity.sid!=peer->pid||last_nspid(peer->pid,&inner))goto bad;
    snprintf(a,sizeof(a),"/proc/%ld/ns/pid",(long)peer->pid);
    snprintf(b,sizeof(b),"/proc/%ld/ns/pid",(long)x->init);
    if(!same_path(a,b))goto bad;
    snprintf(a,sizeof(a),"/proc/%ld/root",(long)peer->pid);
    snprintf(b,sizeof(b),"/proc/%ld/root",(long)x->init);
    if(!same_path(a,b))goto bad;
    snprintf(a,sizeof(a),"/proc/%ld/exe",(long)peer->pid);
    /* The host binds this trusted outer-runtime inode into app /usr/bin/opkrun.
     * FPS_LAUNCHER is not a path inside the application's root. */
    if(!same_path(a,FPS_LAUNCHER)||marker(x->init,inner,&identity))goto bad;
    struct pollfd p={*pidfd,POLLIN|POLLHUP,0};
    if(poll(&p,1,0)||syscall(SYS_pidfd_send_signal,*pidfd,0,NULL,0U))goto bad;
    *start=identity.starttime;return 0;
bad:{int saved=errno?errno:EPERM;close(*pidfd);*pidfd=-1;errno=saved;return -1;}
}
#else
int gkd_app_fps_test_authorize(const struct ucred *,const struct gkd_app_fps_context *,
                               int *,unsigned long long *);
#define authorize gkd_app_fps_test_authorize
#endif
/* Reuse the exact live launcher/namespace/registry proof for system notices. */
int gkd_app_fps_authorize_launcher(const struct ucred *peer,const struct gkd_app_fps_context *x,
                                  int *pin,unsigned long long *start)
{return authorize(peer,x,pin,start);}
static int parse_session(const char *packet,size_t bytes,uint64_t *hi,uint64_t *lo,int *eligible)
{
    static const char hex[]="0123456789abcdef";uint64_t part[2]={0,0};
    if(bytes!=FPS_PACKET_BYTES||memcmp(packet,"fps-begin 1 ",12)||packet[44]!=' '||
       (packet[45]!='0'&&packet[45]!='1')||packet[46]!='\n')return 0;
    for(unsigned i=0;i<32;i++){const char *p=strchr(hex,packet[12+i]);if(!p)return 0;
        part[i/16]=(part[i/16]<<4)|(unsigned)(p-hex);}
    if(!(part[0]|part[1]))return 0;
    *hi=part[0];*lo=part[1];*eligible=packet[45]-'0';return 1;
}
static int receive(int client,char packet[96],int fds[2],size_t *bytes)
{
    char control[CMSG_SPACE(2*sizeof(int))];struct iovec iov={packet,96};
    struct msghdr m={0};m.msg_iov=&iov;m.msg_iovlen=1;m.msg_control=control;m.msg_controllen=sizeof(control);
    ssize_t n=recvmsg(client,&m,MSG_DONTWAIT|MSG_CMSG_CLOEXEC);
    if(n<0)return -1;
    *bytes=(size_t)n;
    unsigned count=0;int bad=(m.msg_flags&(MSG_TRUNC|MSG_CTRUNC))!=0;
    for(struct cmsghdr *cm=CMSG_FIRSTHDR(&m);cm;cm=CMSG_NXTHDR(&m,cm)){
        if(cm->cmsg_level!=SOL_SOCKET||cm->cmsg_type!=SCM_RIGHTS||
           cm->cmsg_len<CMSG_LEN(0)){bad=1;continue;}
        if((cm->cmsg_len-CMSG_LEN(0))%sizeof(int))bad=1;
        unsigned have=(unsigned)((cm->cmsg_len-CMSG_LEN(0))/sizeof(int));
        const int *received=(const int *)CMSG_DATA(cm);
        for(unsigned i=0;i<have;i++){
            if(count<2U)fds[count++]=received[i];
            else{close(received[i]);bad=1;}
        }
    }
    if(bad||count!=2U){errno=EPROTO;return -1;}return 0;
}
static int validate_fds(int fds[2],uint64_t hi,uint64_t lo,
                        const struct gkd_fps_counter_page **page)
{
    struct stat a,b;int af=fcntl(fds[0],F_GETFL),bf=fcntl(fds[1],F_GETFL);
    int seals=fcntl(fds[0],F_GET_SEALS);
    if(af<0||bf<0||seals<0||fstat(fds[0],&a)||fstat(fds[1],&b)||
       !S_ISREG(a.st_mode)||a.st_size!=GKD_FPS_COUNTER_BYTES||
       (af&O_ACCMODE)!=O_RDONLY||
       (seals&(F_SEAL_SEAL|F_SEAL_SHRINK|F_SEAL_GROW))!=(F_SEAL_SEAL|F_SEAL_SHRINK|F_SEAL_GROW)||
       !S_ISFIFO(b.st_mode)||(bf&O_ACCMODE)!=O_RDONLY||!(bf&O_NONBLOCK)){errno=EPROTO;return -1;}
    void *mapped=mmap(NULL,GKD_FPS_COUNTER_BYTES,PROT_READ,MAP_SHARED,fds[0],0);
    if(mapped==MAP_FAILED)return -1;
    const struct gkd_fps_counter_page *p=mapped;unsigned bad=0;
    bad|=p->magic!=GKD_FPS_COUNTER_MAGIC||p->version!=GKD_FPS_COUNTER_VERSION||
         p->bytes!=GKD_FPS_COUNTER_BYTES||p->flags||p->session_hi!=hi||
         p->session_lo!=lo||p->producer_state!=GKD_FPS_PRODUCER_UNAVAILABLE;
    for(unsigned i=0;i<6;i++)bad|=p->reserved[i]!=0;
    if(bad){munmap(mapped,GKD_FPS_COUNTER_BYTES);errno=EPROTO;return -1;}
    *page=p;return 0;
}
static int peer_live(int fd)
{
    struct pollfd p={fd,POLLIN|POLLHUP,0};
    return fd>=0&&poll(&p,1,0)==0&&syscall(SYS_pidfd_send_signal,fd,0,NULL,0U)==0;
}
int gkd_app_fps_control(struct gkd_app_fps *f,int client,const struct ucred *peer,
                        const struct gkd_app_fps_context *x,uint64_t now)
{
    char prefix[sizeof(FPS_PREFIX)-1],packet[96],reply[64];int fds[2]={-1,-1};
    struct pollfd wait={client,POLLIN,0};
    if(!f||client<0||!peer||!x){errno=EINVAL;return -1;}
    if(poll(&wait,1,50)<=0)return 0;
    ssize_t peek=recv(client,prefix,sizeof(prefix),MSG_PEEK|MSG_DONTWAIT);
    if(peek!=(ssize_t)sizeof(prefix)||memcmp(prefix,FPS_PREFIX,sizeof(prefix)))return 0;
    int pin=-1,valid=0,eligible=0;unsigned long long start=0;uint64_t hi=0,lo=0;size_t bytes=0;
    int auth=!authorize(peer,x,&pin,&start);
    if(!auth)fprintf(stderr,"GKD_APP_FPS=REJECTED stage=authorize peer=%ld errno=%d\n",
                     (long)peer->pid,errno);
    int wire=!receive(client,packet,fds,&bytes)&&parse_session(packet,bytes,&hi,&lo,&eligible);
    const struct gkd_fps_counter_page *page=NULL;
    if(auth&&wire&&!validate_fds(fds,hi,lo,&page))valid=1;
    int accepted=auth&&!(f->peer_pidfd>=0&&peer_live(f->peer_pidfd));
    if(accepted){
        gkd_app_fps_close(f);f->peer_pidfd=pin;pin=-1;f->peer=peer->pid;
        f->host=x->host;f->init=x->init;f->state=GKD_APP_FPS_UNAVAILABLE;
        f->sample_ms=now;f->session_hi=hi;f->session_lo=lo;
        if(valid){f->page_fd=fds[0];fds[0]=-1;f->lifetime_fd=fds[1];fds[1]=-1;
            f->page=page;page=NULL;f->previous=gkd_fps_counter_load(f->page);}
    }
    if(page)munmap((void *)page,GKD_FPS_COUNTER_BYTES);
    descriptors(fds);if(pin>=0)close(pin);
    int inject=accepted&&valid&&eligible&&x->enabled;
    int n=snprintf(reply,sizeof(reply),"GKD_APP_FPS=%s inject=%d\n",accepted?"ACCEPTED":"REJECTED",inject);
    (void)send(client,reply,(size_t)n,MSG_NOSIGNAL);
    return 1;
}
void gkd_app_fps_update(struct gkd_app_fps *f,const struct gkd_app_fps_context *x,uint64_t now)
{
    if(!f||!x)return;
    if(f->peer_pidfd<0)return;
    if(f->host!=x->host||f->init!=x->init||!peer_live(f->peer_pidfd)){
        gkd_app_fps_close(f);return;
    }
    if(!f->page||f->lifetime_fd<0){f->state=GKD_APP_FPS_UNAVAILABLE;return;}
    struct pollfd life={f->lifetime_fd,POLLIN|POLLHUP,0};
    if(poll(&life,1,0)!=0){
        close(f->lifetime_fd);f->lifetime_fd=-1;f->state=GKD_APP_FPS_UNAVAILABLE;return;
    }
    const struct gkd_fps_counter_page *p=f->page;unsigned bad=0;
    bad|=p->magic!=GKD_FPS_COUNTER_MAGIC||p->version!=GKD_FPS_COUNTER_VERSION||
         p->bytes!=GKD_FPS_COUNTER_BYTES||p->flags||p->session_hi!=f->session_hi||
         p->session_lo!=f->session_lo;
    for(unsigned i=0;i<6;i++)bad|=p->reserved[i]!=0;
    if(bad||gkd_fps_producer_load(p)!=GKD_FPS_PRODUCER_ATTACHED){
        f->state=GKD_APP_FPS_UNAVAILABLE;return;
    }
    if(!now||now<f->sample_ms){
        f->previous=gkd_fps_counter_load(p);f->sample_ms=now;f->fps=0;
        f->state=GKD_APP_FPS_UNAVAILABLE;return;
    }
    if(f->state!=GKD_APP_FPS_AVAILABLE){
        f->previous=gkd_fps_counter_load(p);f->sample_ms=now;f->fps=0;
        f->state=GKD_APP_FPS_AVAILABLE;return;
    }
    if(now-f->sample_ms>=1000U){
        uint32_t current=gkd_fps_counter_load(p),delta=gkd_fps_counter_delta(current,f->previous);
        uint64_t elapsed=now-f->sample_ms;
        uint64_t scaled=(uint64_t)delta*1000U+elapsed/2U;
        f->fps=scaled/elapsed>999U?999U:(unsigned)(scaled/elapsed);
        f->previous=current;f->sample_ms=now;
    }
}
struct gkd_app_fps_view gkd_app_fps_read(const struct gkd_app_fps *f,
                                         const struct gkd_app_fps_context *x)
{
    struct gkd_app_fps_view v={GKD_APP_FPS_NO_GAME,0,0};
    if(!f||!x||f->peer_pidfd<0)return v;
    v.state=f->state;v.fps=f->fps;
    v.visible=x->lifecycle_allows_game&&x->enabled;
    return v;
}
