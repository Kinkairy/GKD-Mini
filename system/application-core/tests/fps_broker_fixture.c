/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-fps.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/memfd.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static int auth_ok=1;
int gkd_app_fps_test_authorize(const struct ucred *peer,
                               const struct gkd_app_fps_context *context,
                               int *pidfd,unsigned long long *start)
{
    if(!auth_ok||!peer||!context||!context->lifecycle_allows_game){
        errno=EPERM;return -1;
    }
    *pidfd=(int)syscall(SYS_pidfd_open,peer->pid,0U);
    if(*pidfd<0)return -1;
    *start=1234;return 0;
}
struct test_session {
    int producer,reader,lifetime_reader,lifetime_writer;
    struct gkd_fps_counter_page *page;
};
static void session_open(struct test_session *s,uint64_t hi,uint64_t lo)
{
    *s=(struct test_session){-1,-1,-1,-1,NULL};
    s->producer=(int)syscall(SYS_memfd_create,"fps-broker-test",MFD_CLOEXEC|MFD_ALLOW_SEALING);
    assert(s->producer>=0&&!ftruncate(s->producer,GKD_FPS_COUNTER_BYTES));
    s->page=mmap(NULL,GKD_FPS_COUNTER_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,s->producer,0);
    assert(s->page!=MAP_FAILED);
    *s->page=(struct gkd_fps_counter_page){GKD_FPS_COUNTER_MAGIC,GKD_FPS_COUNTER_VERSION,
        GKD_FPS_COUNTER_BYTES,0,hi,lo,0,GKD_FPS_PRODUCER_UNAVAILABLE,{0}};
    assert(!fcntl(s->producer,F_ADD_SEALS,F_SEAL_SEAL|F_SEAL_SHRINK|F_SEAL_GROW));
    char path[64];snprintf(path,sizeof(path),"/proc/self/fd/%d",s->producer);
    s->reader=open(path,O_RDONLY|O_CLOEXEC);
    int pipefd[2];assert(s->reader>=0&&!pipe2(pipefd,O_CLOEXEC|O_NONBLOCK));
    s->lifetime_reader=pipefd[0];s->lifetime_writer=pipefd[1];
}
static void session_close(struct test_session *s)
{
    if(s->page&&s->page!=MAP_FAILED)munmap(s->page,GKD_FPS_COUNTER_BYTES);
    if(s->producer>=0)close(s->producer);
    if(s->reader>=0)close(s->reader);
    if(s->lifetime_reader>=0)close(s->lifetime_reader);
    if(s->lifetime_writer>=0)close(s->lifetime_writer);
    *s=(struct test_session){-1,-1,-1,-1,NULL};
}
static void packet(char out[48],const char *token,int eligible)
{
    assert(strlen(token)==32);
    assert(snprintf(out,48,"fps-begin 1 %s %d\n",token,eligible)==47);
}
static void send_rights(int socket,const char *text,const int *fds,unsigned count)
{
    char control[CMSG_SPACE(3*sizeof(int))];struct iovec iov={(void *)text,strlen(text)};
    struct msghdr message={0};message.msg_iov=&iov;message.msg_iovlen=1;
    if(count){
        assert(count<=3);message.msg_control=control;message.msg_controllen=CMSG_SPACE(count*sizeof(int));
        struct cmsghdr *cm=CMSG_FIRSTHDR(&message);cm->cmsg_level=SOL_SOCKET;
        cm->cmsg_type=SCM_RIGHTS;cm->cmsg_len=CMSG_LEN(count*sizeof(int));
        memcpy(CMSG_DATA(cm),fds,count*sizeof(int));
    }
    assert(sendmsg(socket,&message,MSG_NOSIGNAL)==(ssize_t)strlen(text));
}
static void begin(struct gkd_app_fps *fps,const struct ucred *peer,
                  const struct gkd_app_fps_context *context,uint64_t now,
                  struct test_session *s,const char *token,int eligible,unsigned rights,
                  const char *expected)
{
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair));
    char text[48];packet(text,token,eligible);
    int fds[3]={s->reader,s->lifetime_reader,s->producer};
    send_rights(pair[0],text,fds,rights);
    assert(gkd_app_fps_control(fps,pair[1],peer,context,now)==1);
    char reply[64];ssize_t n=recv(pair[0],reply,sizeof(reply)-1,0);assert(n>0);
    reply[n]=0;assert(!strcmp(reply,expected));
    close(pair[0]);close(pair[1]);
}
static void ordinary_untouched(struct gkd_app_fps *fps,const struct ucred *peer,
                               const struct gkd_app_fps_context *context)
{
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair));
    assert(send(pair[0],"status\n",7,0)==7);
    assert(!gkd_app_fps_control(fps,pair[1],peer,context,0));
    char data[8];assert(recv(pair[1],data,sizeof(data),0)==7&&!memcmp(data,"status\n",7));
    close(pair[0]);close(pair[1]);
}
int main(void)
{
    int guard[2];assert(!pipe2(guard,O_CLOEXEC));
    pid_t child=fork();assert(child>=0);
    if(!child){close(guard[1]);char byte;if(read(guard[0],&byte,1)<0)_exit(2);_exit(0);}
    close(guard[0]);
    struct ucred peer={child,0,0};
    struct gkd_app_fps_context active={101,102,1,1};
    const char *token="0123456789abcdeffedcba9876543210";
    const char *token2="1123456789abcdeffedcba9876543210";
    struct gkd_app_fps fps=GKD_APP_FPS_INIT;
    ordinary_untouched(&fps,&peer,&active);

    struct test_session a,b;session_open(&a,UINT64_C(0x0123456789abcdef),UINT64_C(0xfedcba9876543210));
    begin(&fps,&peer,&active,100,&a,token,1,2,"GKD_APP_FPS=ACCEPTED inject=1\n");
    struct gkd_app_fps_view view=gkd_app_fps_read(&fps,&active);
    assert(view.state==GKD_APP_FPS_UNAVAILABLE&&view.visible);
    __atomic_store_n(&a.page->producer_state,GKD_FPS_PRODUCER_ATTACHED,__ATOMIC_RELEASE);
    gkd_app_fps_update(&fps,&active,110);
    view=gkd_app_fps_read(&fps,&active);
    assert(view.state==GKD_APP_FPS_AVAILABLE&&view.fps==0&&view.visible);
    __atomic_fetch_add(&a.page->successful_flips,60U,__ATOMIC_RELAXED);
    gkd_app_fps_update(&fps,&active,1110);
    view=gkd_app_fps_read(&fps,&active);assert(view.fps==60);
    gkd_app_fps_update(&fps,&active,0);
    view=gkd_app_fps_read(&fps,&active);
    assert(view.state==GKD_APP_FPS_UNAVAILABLE&&view.fps==0);
    gkd_app_fps_update(&fps,&active,1110);
    view=gkd_app_fps_read(&fps,&active);
    assert(view.state==GKD_APP_FPS_AVAILABLE&&view.fps==0);
    fps.previous=UINT32_MAX-10U;fps.sample_ms=1110;
    __atomic_store_n(&a.page->successful_flips,49U,__ATOMIC_RELAXED);
    gkd_app_fps_update(&fps,&active,2110);
    view=gkd_app_fps_read(&fps,&active);assert(view.fps==60);

    session_open(&b,UINT64_C(0x1123456789abcdef),UINT64_C(0xfedcba9876543210));
    begin(&fps,&peer,&active,2200,&b,token2,1,2,"GKD_APP_FPS=REJECTED inject=0\n");
    assert(fps.session_hi==UINT64_C(0x0123456789abcdef));
    close(a.lifetime_writer);a.lifetime_writer=-1;
    gkd_app_fps_update(&fps,&active,2300);
    view=gkd_app_fps_read(&fps,&active);
    assert(view.state==GKD_APP_FPS_UNAVAILABLE&&view.visible);
    struct gkd_app_fps_context hidden=active;hidden.lifecycle_allows_game=0;
    gkd_app_fps_update(&fps,&hidden,2400);
    view=gkd_app_fps_read(&fps,&hidden);
    assert(view.state==GKD_APP_FPS_UNAVAILABLE&&!view.visible);
    gkd_app_fps_close(&fps);session_close(&a);session_close(&b);

    struct test_session bad;session_open(&bad,UINT64_C(0x0123456789abcdef),UINT64_C(0xfedcba9876543210));
    begin(&fps,&peer,&active,1,&bad,token,1,1,"GKD_APP_FPS=ACCEPTED inject=0\n");
    view=gkd_app_fps_read(&fps,&active);
    assert(view.state==GKD_APP_FPS_UNAVAILABLE&&view.visible);
    gkd_app_fps_close(&fps);
    begin(&fps,&peer,&active,1,&bad,token,1,3,"GKD_APP_FPS=ACCEPTED inject=0\n");
    assert(gkd_app_fps_read(&fps,&active).state==GKD_APP_FPS_UNAVAILABLE);
    gkd_app_fps_close(&fps);
    bad.page->bytes=63;
    begin(&fps,&peer,&active,1,&bad,token,1,2,"GKD_APP_FPS=ACCEPTED inject=0\n");
    assert(gkd_app_fps_read(&fps,&active).state==GKD_APP_FPS_UNAVAILABLE);
    gkd_app_fps_close(&fps);
    bad.page->bytes=GKD_FPS_COUNTER_BYTES;
    begin(&fps,&peer,&active,1,&bad,token2,1,2,"GKD_APP_FPS=ACCEPTED inject=0\n");
    assert(gkd_app_fps_read(&fps,&active).state==GKD_APP_FPS_UNAVAILABLE);
    gkd_app_fps_close(&fps);

    struct gkd_app_fps_context disabled=active;disabled.enabled=0;
    begin(&fps,&peer,&disabled,1,&bad,token,1,2,"GKD_APP_FPS=ACCEPTED inject=0\n");
    assert(gkd_app_fps_read(&fps,&disabled).state==GKD_APP_FPS_UNAVAILABLE);
    gkd_app_fps_close(&fps);
    begin(&fps,&peer,&active,1,&bad,token,0,2,"GKD_APP_FPS=ACCEPTED inject=0\n");
    gkd_app_fps_close(&fps);
    auth_ok=0;
    begin(&fps,&peer,&active,1,&bad,token,1,2,"GKD_APP_FPS=REJECTED inject=0\n");
    assert(gkd_app_fps_read(&fps,&active).state==GKD_APP_FPS_NO_GAME);
    auth_ok=1;

    begin(&fps,&peer,&active,1,&bad,token,1,2,"GKD_APP_FPS=ACCEPTED inject=1\n");
    close(guard[1]);assert(waitpid(child,NULL,0)==child);
    gkd_app_fps_update(&fps,&active,2);
    assert(gkd_app_fps_read(&fps,&active).state==GKD_APP_FPS_NO_GAME);
    gkd_app_fps_close(&fps);session_close(&bad);
    puts("GKD_FPS_BROKER=PASS packet-peek/fd-session-validation/live-replacement/count-wrap/lifetime/game-end");
    return 0;
}
