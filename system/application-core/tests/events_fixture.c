/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-events.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
static int inputs[3][2];
int __wrap_ioctl(int fd,unsigned long request,...)
{
 (void)fd;
 va_list ap;va_start(ap,request);void *p=va_arg(ap,void *);va_end(ap);
 assert(_IOC_NR(request)==0x18U);memset(p,0,_IOC_SIZE(request));return (int)_IOC_SIZE(request);
}
static void key(unsigned source,unsigned type,unsigned code,int value)
{
 static unsigned stamp;
 struct input_event e;memset(&e,0,sizeof(e));e.time.tv_usec=++stamp;e.type=(unsigned short)type;e.code=(unsigned short)code;e.value=value;
 assert(write(inputs[source][1],&e,sizeof(e))==(ssize_t)sizeof(e));
}
static unsigned cycle(struct gkd_app_events *e,int blocked)
{
 unsigned out=999;assert(!gkd_app_events_poll(e,blocked,&out));return out&~GKD_APP_EVENT_ACTIVITY;
}
int main(void)
{
 struct gkd_app_events e;gkd_app_events_init(&e);
 for(unsigned i=0;i<3;i++)assert(!pipe2(inputs[i],O_NONBLOCK|O_CLOEXEC));
 e.observer.physical_fd=inputs[0][0];e.observer.virtual_fd=inputs[1][0];e.power_fd=inputs[2][0];
 e.menu_key=KEY_HOME;e.keys[3]=KEY_LEFTALT;e.keys[4]=KEY_TAB;e.keys[6]=KEY_PAGEUP;e.shot[0]=4;e.shot[1]=6;
 assert(!cycle(&e,0));
 key(2,EV_KEY,KEY_POWER,1);key(2,EV_KEY,KEY_POWER,0);assert(cycle(&e,0)==GKD_APP_EVENT_POWER);
 key(0,EV_KEY,KEY_LEFTALT,1);assert(!cycle(&e,0));
 key(1,EV_KEY,KEY_LEFTALT,1);key(0,EV_KEY,KEY_LEFTALT,0);assert(!cycle(&e,0));
 key(1,EV_KEY,KEY_LEFTALT,0);assert(cycle(&e,0)==GKD_APP_EVENT_RETURN);
 key(0,EV_KEY,KEY_TAB,1);key(0,EV_KEY,KEY_PAGEUP,1);assert(cycle(&e,0)==GKD_APP_EVENT_SCREENSHOT);
 key(0,EV_KEY,KEY_TAB,2);assert(!cycle(&e,0));
 key(0,EV_KEY,KEY_TAB,0);key(0,EV_KEY,KEY_PAGEUP,0);assert(!cycle(&e,0));
 key(2,EV_KEY,KEY_POWER,1);assert(!cycle(&e,1));
 assert(!cycle(&e,0));key(2,EV_KEY,KEY_POWER,0);assert(!cycle(&e,0));
 key(2,EV_KEY,KEY_POWER,1);key(2,EV_KEY,KEY_POWER,0);assert(cycle(&e,0)==GKD_APP_EVENT_POWER);
 key(2,EV_KEY,KEY_POWER,1);key(2,EV_SYN,SYN_DROPPED,0);key(2,EV_SYN,SYN_REPORT,0);
 key(2,EV_KEY,KEY_POWER,0);key(2,EV_KEY,KEY_POWER,1);key(2,EV_KEY,KEY_POWER,0);
 assert(!cycle(&e,0));assert(!cycle(&e,0));
 key(2,EV_KEY,KEY_POWER,1);key(2,EV_KEY,KEY_POWER,0);assert(cycle(&e,0)==GKD_APP_EVENT_POWER);
 /* Menu release is its own intent, never POWER; duplicate sources and
  * blocked/drop recovery still require aggregate release. */
 key(0,EV_KEY,KEY_HOME,1);key(1,EV_KEY,KEY_HOME,1);
 key(0,EV_KEY,KEY_HOME,2);assert(!cycle(&e,0));
 key(0,EV_KEY,KEY_HOME,0);assert(!cycle(&e,0));
 key(1,EV_KEY,KEY_HOME,0);assert(cycle(&e,0)==GKD_APP_EVENT_SETTINGS);
 key(0,EV_KEY,KEY_HOME,1);assert(!cycle(&e,1));
 key(0,EV_KEY,KEY_HOME,0);assert(!cycle(&e,0));assert(!cycle(&e,0));
 key(0,EV_KEY,KEY_HOME,1);key(0,EV_SYN,SYN_DROPPED,0);
 key(0,EV_SYN,SYN_REPORT,0);key(0,EV_KEY,KEY_HOME,0);assert(!cycle(&e,0));
 key(0,EV_KEY,KEY_HOME,1);key(0,EV_KEY,KEY_HOME,0);
 assert(cycle(&e,0)==GKD_APP_EVENT_SETTINGS);
 e.shot[0]=8U;e.shot[1]=4U;
 key(0,EV_KEY,KEY_TAB,1);assert(!cycle(&e,0));
 key(0,EV_KEY,KEY_HOME,1);assert(!cycle(&e,0));
 key(0,EV_KEY,KEY_HOME,0);assert(cycle(&e,0)==GKD_APP_EVENT_SETTINGS);
 key(0,EV_KEY,KEY_TAB,0);assert(!cycle(&e,0));
 key(0,EV_KEY,KEY_HOME,1);key(0,EV_KEY,KEY_TAB,1);
 assert(cycle(&e,0)==GKD_APP_EVENT_SCREENSHOT);
 key(0,EV_KEY,KEY_TAB,2);assert(!cycle(&e,0));
 key(0,EV_KEY,KEY_TAB,0);key(0,EV_KEY,KEY_TAB,1);assert(!cycle(&e,0));
 key(0,EV_KEY,KEY_HOME,0);key(0,EV_KEY,KEY_TAB,0);assert(!cycle(&e,0));
 key(0,EV_KEY,KEY_HOME,1);key(1,EV_KEY,KEY_HOME,1);key(0,EV_KEY,KEY_TAB,1);
 assert(cycle(&e,0)==GKD_APP_EVENT_SCREENSHOT);
 key(0,EV_KEY,KEY_HOME,0);assert(!cycle(&e,0));
 key(1,EV_KEY,KEY_HOME,0);key(0,EV_KEY,KEY_TAB,0);assert(!cycle(&e,0));
 key(0,EV_KEY,KEY_HOME,1);key(0,EV_KEY,KEY_HOME,0);
 assert(cycle(&e,0)==GKD_APP_EVENT_SETTINGS);
 key(2,EV_KEY,KEY_POWER,7);unsigned out=123;
 assert(gkd_app_events_poll(&e,0,&out)<0&&errno==EPROTO&&out==123);
 for(unsigned i=0;i<3;i++){close(inputs[i][0]);close(inputs[i][1]);}
 puts("GKD_APP_EVENTS_FIXTURE=PASS batch/duplicates/repeat/held/block/drop/rearm/invalid");
 return 0;
}
