/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-payload.h"
#include "gkd-app-game-control.h"
#include "gkd-app-loop.h"
#include <stdio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static long long now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return t.tv_sec*1000LL+t.tv_nsec/1000000;}
int main(int argc,char **argv)
{
    if(argc!=2||gkd_app_loop_owned_count(argv[1])!=1){perror("LOOP_OWNER");return 1;}
    int listener=gkd_app_game_listen(1);if(listener<0){perror("LISTEN");return 1;}
    char *normal[]={"/bin/busybox","sh","-c","/bin/busybox sleep 1 & (/bin/busybox sleep 1 &); exit 7",NULL};
    char *forced[]={"/bin/busybox","sleep","10",NULL};
    char *missing[]={"/no-gkd-probe-executable",NULL};
    char **commands[]={normal,forced,missing};
    for(unsigned i=0;i<3;i++){
        struct gkd_app_payload_result r;
        volatile sig_atomic_t stop=i==1;
        long long began=now();
        if(gkd_app_payload_run(commands[i],"/",listener,1,&stop,NULL,&r)){perror("PAYLOAD");return 1;}
        int code=WIFEXITED(r.wait_status)?WEXITSTATUS(r.wait_status):128+WTERMSIG(r.wait_status);
        if(!r.reaped||code!=(i==0?7:i==1?137:127)||(i==0&&now()-began<900))return 1;
        printf("GKD_PAYLOAD_PROBE=PASS case=%u exit=%d reaped=%d duration_ms=%lld\n",i,code,r.reaped,now()-began);
    }
    close(listener);
    if(gkd_app_loop_owned_count(argv[1])!=1)return 1;
    puts("GKD_PAYLOAD_PROBE=PASS loop-owner-unchanged");return 0;
}
