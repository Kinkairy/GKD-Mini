/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-settings-client.h"
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
int gkd_settings_exchange(const char *path,const char *request,char *reply,size_t capacity)
{
    struct sockaddr_un address;struct ucred peer;socklen_t size=sizeof(peer);
    int fd=-1,rc=-1,saved;ssize_t n;size_t bytes;
    if(!path||!request||!reply||capacity<2U||strlen(path)>=sizeof(address.sun_path)||
       !(bytes=strlen(request))||bytes>=192U){errno=EINVAL;return -1;}
    memset(&address,0,sizeof(address));address.sun_family=AF_UNIX;
    memcpy(address.sun_path,path,strlen(path)+1U);
    fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)return -1;
    if(connect(fd,(struct sockaddr *)&address,sizeof(address))||
       getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&size))goto done;
    if(size!=sizeof(peer)||peer.uid){errno=EPERM;goto done;}
    n=send(fd,request,bytes,MSG_NOSIGNAL);if(n!=(ssize_t)bytes){if(n>=0)errno=EIO;goto done;}
    struct pollfd item={fd,POLLIN,0};
    int ready=poll(&item,1,50);
    if(ready<=0){if(!ready)errno=ETIMEDOUT;goto done;}
    n=recv(fd,reply,capacity,MSG_TRUNC);
    if(n<=0||n>=(ssize_t)capacity||memchr(reply,0,(size_t)n)){errno=EPROTO;goto done;}
    reply[n]=0;rc=0;
done:
    saved=errno;close(fd);errno=saved;return rc;
}
