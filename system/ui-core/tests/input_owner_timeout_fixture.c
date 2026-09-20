// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include "gkd-input-owner.h"
#include <assert.h>
#include <errno.h>
#include <linux/input.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static long long ms(void){struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static void tick(int s){(void)s;}
static void event(int fd,int value){struct input_event e;memset(&e,0,sizeof(e));e.type=EV_KEY;e.code=KEY_LEFTCTRL;e.value=value;assert(write(fd,&e,sizeof(e))==sizeof(e));}
int main(void){
 int pipefd[2];assert(!pipe(pipefd));struct gkd_input_owner o;memset(&o,0,sizeof(o));o.physical_fd=pipefd[0];o.virtual_fd=-1;
 assert(gkd_input_owner_next_key_timeout(&o,-2)<0&&errno==EINVAL);
 assert(gkd_input_owner_next_key_timeout(&o,0)==0);
 event(pipefd[1],1);assert(gkd_input_owner_next_key_timeout(&o,0)==KEY_LEFTCTRL);
 event(pipefd[1],0);event(pipefd[1],2);event(pipefd[1],1);assert(gkd_input_owner_next_key(&o)==KEY_LEFTCTRL);
 struct sigaction sa;memset(&sa,0,sizeof(sa));sa.sa_handler=tick;sigemptyset(&sa.sa_mask);assert(!sigaction(SIGALRM,&sa,0));
 struct itimerval timer={{0,5000},{0,5000}};assert(!setitimer(ITIMER_REAL,&timer,0));
 pid_t child=fork();assert(child>=0);if(!child){for(int i=0;i<100;i++){event(pipefd[1],0);usleep(2000);}_exit(0);}
 long long start=ms();assert(gkd_input_owner_next_key_timeout(&o,40)==0);long long elapsed=ms()-start;assert(elapsed>=35&&elapsed<150);
 memset(&timer,0,sizeof(timer));assert(!setitimer(ITIMER_REAL,&timer,0));int status;while(waitpid(child,&status,0)<0)assert(errno==EINTR);assert(WIFEXITED(status)&&!WEXITSTATUS(status));
 close(pipefd[1]);assert(gkd_input_owner_next_key_timeout(&o,40)<0);close(pipefd[0]);
 puts("GKD_INPUT_TIMEOUT=PASS deadline/EINTR/release/held-repeat/immediate-key/legacy-key/hangup");return 0;
}
