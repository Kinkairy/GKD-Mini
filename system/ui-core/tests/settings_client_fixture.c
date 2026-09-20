/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-settings-client.h"
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
static void one(unsigned scenario)
{
 struct sockaddr_un address;char path[100],reply[160],request[192];int status;
 snprintf(path,sizeof(path),"/tmp/gkd-settings-client-%ld-%u",(long)getpid(),scenario);
 int listener=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);assert(listener>=0);
 memset(&address,0,sizeof(address));address.sun_family=AF_UNIX;strcpy(address.sun_path,path);
 assert(!bind(listener,(struct sockaddr *)&address,sizeof(address))&&!listen(listener,1));
 pid_t child=fork();assert(child>=0);
 if(!child){
  int fd=accept4(listener,NULL,NULL,SOCK_CLOEXEC);assert(fd>=0);
  ssize_t n=recv(fd,request,sizeof(request),0);assert(n==15&&!memcmp(request,"settings-status",15U));
  if(scenario==0)assert(send(fd,"GKD_APP_SETTINGS=SAVING\n",strlen("GKD_APP_SETTINGS=SAVING\n"),MSG_NOSIGNAL)==(ssize_t)strlen("GKD_APP_SETTINGS=SAVING\n"));
  if(scenario==1){char bad[200];memset(bad,'a',sizeof(bad));assert(send(fd,bad,sizeof(bad),MSG_NOSIGNAL)==(ssize_t)sizeof(bad));}
  if(scenario==2){char bad[]={'a',0,'b'};assert(send(fd,bad,sizeof(bad),MSG_NOSIGNAL)==(ssize_t)sizeof(bad));}
  if(scenario==3)usleep(100000);
  close(fd);close(listener);_exit(0);
 }
 errno=0;int rc=gkd_settings_exchange(path,"settings-status",reply,sizeof(reply));
 if(scenario==0)assert(rc==0&&!strcmp(reply,"GKD_APP_SETTINGS=SAVING\n"));
 else if(scenario==3)assert(rc<0&&errno==ETIMEDOUT);
 else assert(rc<0&&errno==EPROTO);
 assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status));
 close(listener);assert(!unlink(path));
}
int main(void)
{
 for(unsigned i=0;i<4U;++i)one(i);
 char reply[160];assert(gkd_settings_exchange("/tmp/gkd-settings-missing","settings-status",reply,sizeof(reply))<0);
 puts("SETTINGS_CLIENT_PASS seqpacket/oversize/nul/deadline/missing-socket");return 0;
}
