/* SPDX-License-Identifier: GPL-2.0 */
/* Actual event-open/close code with owned real FDs and mocked device discovery. */
#define _GNU_SOURCE
#include "gkd-app-events.h"
#include "gkd-menu-vt.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
static int live, power_fd=-1, system_fd=-1, fail_open, fail_config, configs;
static int allocate(void) {int fd=open("/dev/null",O_RDONLY|O_CLOEXEC);assert(fd>=0);live++;return fd;}
static void dispose(int fd) {if(fd>=0){assert(!close(fd));live--;}}
static int device_open(const char *path,int flags,...)
{
 assert(flags&O_CLOEXEC);
 if(!strcmp(path,"/dev/gkd-system-hotkey")){
  if(fail_open){errno=EBUSY;return -1;}
  system_fd=allocate();return system_fd;
 }
 if(!strcmp(path,"/dev/input/event0")){power_fd=allocate();return power_fd;}
 errno=ENOENT;return -1;
}
static int device_close(int fd)
{
 if(fd==system_fd)system_fd=-1;
 if(fd==power_fd)power_fd=-1;
 dispose(fd);return 0;
}
static int device_ioctl(int fd,unsigned long request,...)
{
 va_list ap;va_start(ap,request);void *arg=va_arg(ap,void *);va_end(ap);
 if(request==GKD_SYSTEM_HOTKEY_CONFIG){
  assert(fd==system_fd);struct gkd_system_hotkey_config *c=arg;
  assert(c->version==GKD_SYSTEM_HOTKEY_VERSION&&!c->reserved&&c->prefix==KEY_HOME&&c->button==KEY_TAB);
  configs++;if(fail_config){errno=EINVAL;return -1;}return 0;
 }
 if(_IOC_NR(request)==0x06U){assert(fd==power_fd);snprintf(arg,_IOC_SIZE(request),"GKD Mini AXP173 power key");return 0;}
 assert(_IOC_NR(request)==0x18U);memset(arg,0,_IOC_SIZE(request));return 0;
}
void gkd_input_owner_init(struct gkd_input_owner *o)
{memset(o,0,sizeof(*o));o->physical_fd=o->virtual_fd=o->menu_guard.lease_fd=-1;}
int gkd_input_observer_open(struct gkd_input_owner *o)
{o->physical_fd=allocate();o->virtual_fd=allocate();return 0;}
void gkd_input_observer_close(struct gkd_input_owner *o)
{dispose(o->physical_fd);dispose(o->virtual_fd);gkd_input_owner_init(o);}
int gkd_input_owner_open_menu(struct gkd_input_owner *o){return gkd_input_observer_open(o);}
int gkd_input_owner_open_menu_reuse(struct gkd_input_owner *o,struct gkd_menu_guard_owner *g)
{(void)g;return gkd_input_observer_open(o);}
int gkd_input_owner_transfer_menu_guard(struct gkd_input_owner *o,struct gkd_menu_guard_owner *g)
{(void)o;(void)g;return 0;}
int gkd_input_owner_close(struct gkd_input_owner *o){gkd_input_observer_close(o);return 0;}
#define open device_open
#define close device_close
#define ioctl device_ioctl
#include "../source/gkd-app-events.c"
#undef open
#undef close
#undef ioctl
int main(void)
{
 struct gkd_app_events e;struct gkd_app_settings s={0};
 const char *names[]={"KEY_UP","KEY_DOWN","KEY_LEFTCTRL","KEY_LEFTALT","KEY_TAB","KEY_BACKSPACE","KEY_PAGEUP","KEY_PAGEDOWN"};
 for(unsigned i=0;i<8;i++)snprintf(s.keys[i],sizeof(s.keys[i]),"%s",names[i]);
 snprintf(s.menu_key,sizeof(s.menu_key),"KEY_HOME");s.screenshot_pair[0]=8;s.screenshot_pair[1]=4;
 gkd_app_events_init(&e);assert(!gkd_app_events_open(&e,&s));
 assert(live==4&&e.system_fd>=0&&configs==1);
 gkd_app_events_close(&e);assert(!live&&e.system_fd==-1&&e.power_fd==-1);
 gkd_app_events_close(&e);assert(!live); /* idempotent */
 fail_open=1;assert(gkd_app_events_open(&e,&s)==-1&&errno==EBUSY);assert(!live&&e.system_fd==-1);
 fail_open=0;fail_config=1;assert(gkd_app_events_open(&e,&s)==-1&&errno==EINVAL);
 assert(!live&&e.system_fd==-1&&system_fd==-1);fail_config=0;
 int before=configs;
 assert(!gkd_app_events_open_menu(&e,&s));assert(live==3&&e.system_fd==-1&&configs==before);
 gkd_app_events_close(&e);assert(!live);
 s.screenshot_pair[0]=s.screenshot_pair[1]=0;
 assert(!gkd_app_events_open(&e,&s));assert(live==3&&e.system_fd==-1&&configs==before);
 gkd_app_events_close(&e);assert(!live);
 puts("GKD_EVENTS_OWNER=PASS actual-open-close/exclusive/NONE/busy/config-failure/no-FD-leak");
 return 0;
}
