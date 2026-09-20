/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-service.h"
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
static volatile sig_atomic_t stop;
static void done(int sig){(void)sig;stop=1;}
int main(int argc,char **argv)
{
    const char *mode=getenv("GKD_SESSION_FIXTURE");
    struct ucred peer;socklen_t len=sizeof(peer);
    assert(argc==4&&!strcmp(argv[2],"--service-fd")&&!strcmp(argv[3],"3"));
    assert(!getsockopt(3,SOL_SOCKET,SO_PEERCRED,&peer,&len));
    assert(peer.uid==0&&peer.pid==getppid());
    assert(mode);
    assert(signal(SIGTERM,!strcmp(mode,"hung")?SIG_IGN:done)!=SIG_ERR);
    struct gkd_app_service_ready message={GKD_APP_SERVICE_MAGIC,1U,getpid(),getpid()+1,getpid()+2};
    if(!strcmp(mode,"spoof")){
        pid_t child=fork();assert(child>=0);
        if(!child){assert(send(3,&message,sizeof(message),MSG_NOSIGNAL)==sizeof(message));_exit(0);}
        assert(waitpid(child,NULL,0)==child);
    } else if(strcmp(mode,"timeout")){
        size_t size=!strcmp(mode,"short")?1U:sizeof(message);
        assert(send(3,&message,size,MSG_NOSIGNAL)==(ssize_t)size);
    }
    while(!stop)pause();
    return 0;
}
