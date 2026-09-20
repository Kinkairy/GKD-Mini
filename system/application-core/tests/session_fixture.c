/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-session.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static uint64_t milliseconds(void)
{
    struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));
    return (uint64_t)t.tv_sec*1000U+(uint64_t)t.tv_nsec/1000000U;
}
static void tick(void){struct timespec t={0,1000000};nanosleep(&t,NULL);}
static void reap(struct gkd_app_session *s,uint64_t now)
{
    for(unsigned i=0;s->pid>0&&i<3000;i++){tick();gkd_app_session_poll(s,now+i);}
    assert(s->pid<0);
}
int main(void)
{
    const char *modes[]={"ready","short","spoof","timeout","hung","clock"};
    for(unsigned k=0;k<sizeof(modes)/sizeof(modes[0]);k++){
        struct gkd_app_session s=GKD_APP_SESSION_INIT;uint64_t now=milliseconds();
        assert(!setenv("GKD_SESSION_FIXTURE",modes[k],1));
        assert(!gkd_app_session_start(&s,5000U,now));
        assert(gkd_app_session_close(&s)<0&&errno==EBUSY);
        if(!strcmp(modes[k],"timeout")){
            gkd_app_session_poll(&s,s.deadline);
            reap(&s,s.last_clock+1U);
            assert(s.state==GKD_SESSION_FAILED&&s.error==ETIMEDOUT);
        } else {
            for(unsigned i=0;s.state==GKD_SESSION_STARTING&&i<3000;i++){tick();gkd_app_session_poll(&s,milliseconds());}
            if(!strcmp(modes[k],"short")||!strcmp(modes[k],"spoof")){
                reap(&s,milliseconds());assert(s.state==GKD_SESSION_FAILED&&s.error==EPROTO);
            } else {
                assert(s.state==GKD_SESSION_READY);
                if(!strcmp(modes[k],"clock")){
                    gkd_app_session_poll(&s,0);reap(&s,milliseconds());
                    assert(s.state==GKD_SESSION_FAILED&&s.forced);
                } else {
                    now=milliseconds();assert(!gkd_app_session_stop(&s,now));
                    uint64_t deadline=s.deadline;
                    assert(!gkd_app_session_stop(&s,now+1)&&s.deadline==deadline);
                    if(!strcmp(modes[k],"hung"))gkd_app_session_poll(&s,deadline);
                    reap(&s,s.last_clock+1);
                    assert(s.state==(!strcmp(modes[k],"hung")?GKD_SESSION_FAILED:GKD_SESSION_STOPPED));
                }
            }
        }
        assert(!gkd_app_session_close(&s)&&s.state==GKD_SESSION_IDLE);
    }
    puts("GKD_APP_SESSION_FIXTURE=PASS credentials/short/spoof/timeout/stop/hung/clock/ownership");
    return 0;
}
