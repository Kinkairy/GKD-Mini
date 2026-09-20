/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-profile.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
static void make(const char *path,const char *text)
{int fd=open(path,O_CREAT|O_EXCL|O_WRONLY,0555);assert(fd>=0);assert(write(fd,text,strlen(text))==(ssize_t)strlen(text));assert(!close(fd));}
int main(int argc,char **argv)
{
 assert(argc==2&&geteuid()==0);assert(!chdir(argv[1]));assert(!mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL));
 assert(!mkdir("root",0700));assert(!mkdir("root/var",0700));assert(!mkdir("root/var/run",0700));
 assert(!mkdir("root/usr",0700));assert(!mkdir("root/usr/sbin",0700));
 make("root/usr/sbin/gkd-system-update","OLD EXECUTOR\n");make("service","NEW SERVICE\n");
 int listener=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);assert(listener>=0);
 struct sockaddr_un address={.sun_family=AF_UNIX};strcpy(address.sun_path,"control.sock");
 assert(!bind(listener,(struct sockaddr *)&address,sizeof(address)));assert(!chmod("control.sock",0600));assert(!listen(listener,1));
 char cwd[4096],socket_path[4200],service[4200],root[4200];
 assert(getcwd(cwd,sizeof(cwd)));snprintf(socket_path,sizeof(socket_path),"%s/control.sock",cwd);
 snprintf(service,sizeof(service),"%s/service",cwd);snprintf(root,sizeof(root),"%s/root",cwd);
 assert(!gkd_app_update_entry_mount(socket_path,service,root));
 struct stat expected;assert(!stat("service",&expected));pid_t child=fork();assert(child>=0);
 if(!child){
  close(listener);assert(!chroot(root));assert(!chdir("/"));
  struct stat actual;assert(!stat("/usr/sbin/gkd-system-update",&actual));
  assert(actual.st_dev==expected.st_dev&&actual.st_ino==expected.st_ino);
  errno=0;assert(open("/usr/sbin/gkd-system-update",O_WRONLY)<0&&errno==EROFS);
  int client=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);assert(client>=0);
  strcpy(address.sun_path,"/var/run/gkd-application/control.sock");
  assert(!connect(client,(struct sockaddr *)&address,sizeof(address)));
  assert(send(client,"update-entry",12,MSG_NOSIGNAL)==12);char reply[64];
  assert(recv(client,reply,sizeof(reply),0)==41&&!memcmp(reply,"GKD_APPLICATION_COMMAND=ACCEPTED errno=0\n",41));close(client);_exit(0);
 }
 int client=accept4(listener,NULL,NULL,SOCK_CLOEXEC);assert(client>=0);
 struct ucred peer;socklen_t size=sizeof(peer);assert(!getsockopt(client,SOL_SOCKET,SO_PEERCRED,&peer,&size));
 assert(peer.pid==child&&peer.uid==0);char message[32];assert(recv(client,message,sizeof(message),0)==12&&!memcmp(message,"update-entry",12));
 assert(send(client,"GKD_APPLICATION_COMMAND=ACCEPTED errno=0\n",41,MSG_NOSIGNAL)==41);close(client);close(listener);
 int status;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status));
 puts("GKD_APP_UPDATE_ENTRY=PASS native-mount/chroot/socket-credentials/sole-new-executor/readonly");
 return 0;
}
