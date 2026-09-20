/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-job.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static uint64_t now(void){struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000U+(uint64_t)t.tv_nsec/1000000U;}
static void finish(struct gkd_app_job *j)
{
 uint64_t deadline=now()+4000;
 while(j->pid>0&&now()<deadline){gkd_app_job_poll(j,now());usleep(1000);}
 assert(j->pid<0);
}
int main(void)
{
 struct gkd_app_job j=GKD_APP_JOB_INIT;
 char *good[]={"/bin/sh","-c","printf 'GKD_RESULT=PASS\\n'",NULL};
 assert(!gkd_app_job_start(&j,good,1000,now()));assert(gkd_app_job_close(&j)<0&&errno==EBUSY);
 finish(&j);assert(j.state==GKD_JOB_DONE&&!strcmp(j.output,"GKD_RESULT=PASS\n"));assert(!gkd_app_job_close(&j));
 char *bad[]={"/bin/sh","-c","exit 9",NULL};
 assert(!gkd_app_job_start(&j,bad,1000,now()));finish(&j);
 assert(j.state==GKD_JOB_FAILED&&WEXITSTATUS(j.status)==9);assert(!gkd_app_job_close(&j));
 char *missing[]={"/not/a/program",NULL};
 assert(!gkd_app_job_start(&j,missing,1000,now()));finish(&j);
 assert(j.state==GKD_JOB_FAILED&&WEXITSTATUS(j.status)==127);assert(!gkd_app_job_close(&j));
 char *large[]={"/bin/sh","-c","printf '%0600d' 1",NULL};
 assert(!gkd_app_job_start(&j,large,1000,now()));finish(&j);
 assert(j.state==GKD_JOB_FAILED&&j.error==EOVERFLOW);assert(!gkd_app_job_close(&j));
 char *hang[]={"/bin/sh","-c","trap '' TERM; while :; do :; done",NULL};
 assert(!gkd_app_job_start(&j,hang,100,now()));finish(&j);
 assert(j.state==GKD_JOB_FAILED&&j.error==ETIMEDOUT&&WIFSIGNALED(j.status)&&WTERMSIG(j.status)==SIGKILL);
 assert(!gkd_app_job_close(&j));
 assert(!gkd_app_job_start(&j,hang,1000,now()));assert(!gkd_app_job_cancel(&j,now()));finish(&j);
 assert(j.state==GKD_JOB_FAILED&&j.error==ECANCELED);assert(!gkd_app_job_close(&j));
 int inherited=open("/dev/null",O_RDONLY|O_CLOEXEC);assert(inherited>=0);
 char *fd_job[]={"/bin/sh","-c","test -r /proc/self/fd/3 && test ! -e /proc/self/fd/4 && printf 'FD3=PASS\\n'",NULL};
 assert(!gkd_app_job_start_fd(&j,fd_job,1000,now(),inherited));close(inherited);finish(&j);
 assert(j.state==GKD_JOB_DONE&&!strcmp(j.output,"FD3=PASS\n"));assert(!gkd_app_job_close(&j));
 int barrier[2];assert(!pipe2(barrier,O_CLOEXEC));
 char *transaction[]={"/bin/sh","-c","read token <&3; printf 'TRANSACTION=%s\\n' \"$token\"",NULL};
 uint64_t started=now();
 assert(!gkd_app_job_start_transaction(&j,transaction,started,barrier[0]));close(barrier[0]);
 assert(j.transaction&&!j.deadline);
 assert(gkd_app_job_poll(&j,started+121000U)==GKD_JOB_RUNNING&&j.pid>0&&!j.error);
 assert(gkd_app_job_cancel(&j,started+121001U)<0&&errno==EBUSY);
 assert(write(barrier[1],"PASS\n",5)==5);close(barrier[1]);
 uint64_t limit=now()+4000U;
 while(j.pid>0&&now()<limit){gkd_app_job_poll(&j,now()+122000U);usleep(1000);}
 assert(j.state==GKD_JOB_DONE&&!strcmp(j.output,"TRANSACTION=PASS\n"));
 assert(!gkd_app_job_close(&j));
 assert(gkd_app_job_start(&j,good,0,now())<0);
 assert(gkd_app_job_start(&j,good,600001U,now())<0);
 assert(!gkd_app_job_start(&j,good,600000U,now()));finish(&j);
 assert(j.state==GKD_JOB_DONE);assert(!gkd_app_job_close(&j));
 assert(gkd_app_job_start(&j,good,600000U,UINT64_MAX-599999U)<0);
 puts("GKD_APP_JOB_FIXTURE=PASS exec/output/exit/overflow/timeout/cancel/owned-reap/transaction-no-timeout");
 return 0;
}
