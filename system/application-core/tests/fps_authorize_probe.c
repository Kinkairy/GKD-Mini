/* SPDX-License-Identifier: GPL-2.0 */
/* Read-only probe of actual production authorization and current proc identities. */
#include "../source/gkd-app-fps.c"
#include <assert.h>
int main(int argc,char **argv)
{
 if(argc!=4)return 64;
 struct gkd_app_fps_context x={(pid_t)atoi(argv[1]),(pid_t)atoi(argv[2]),1,1};
 struct ucred peer={(pid_t)atoi(argv[3]),0,0};
 int pin=-1;unsigned long long start=0;errno=0;
 int result=authorize(&peer,&x,&pin,&start),error=errno;
 printf("FPS_AUTH_PROBE result=%d errno=%d peer=%ld start=%llu\n",result,error,(long)peer.pid,start);
 if(pin>=0)close(pin);
 if(result)return 1;
 peer.uid=1;pin=-1;assert(authorize(&peer,&x,&pin,&start)<0&&pin<0);
 peer.uid=0;x.lifecycle_allows_game=0;assert(authorize(&peer,&x,&pin,&start)<0&&pin<0);
 x.lifecycle_allows_game=1;peer.pid=x.init;
 assert(authorize(&peer,&x,&pin,&start)<0&&pin<0);
 puts("FPS_AUTH_NEGATIVE_PASS nonroot/lifecycle/unrelated-process");
 return 0;
}
