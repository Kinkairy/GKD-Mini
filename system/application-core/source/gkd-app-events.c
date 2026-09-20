/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-events.h"
#include "gkd-input-keys.h"
#include "gkd-menu-vt.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#ifndef GKD_APP_EVENTS_INPUT
#define GKD_APP_EVENTS_INPUT "/dev/input"
#endif
void gkd_app_events_init(struct gkd_app_events *e)
{
 memset(e,0,sizeof(*e));gkd_input_owner_init(&e->observer);e->power_fd=e->system_fd=-1;e->barrier=1;
}
static int snapshot(struct gkd_app_events *e,unsigned source,int fd)
{
 unsigned char bits[(KEY_MAX+8U)/8U];
 memset(bits,0,sizeof(bits));if(ioctl(fd,EVIOCGKEY(sizeof(bits)),bits)<0)return -1;
 for(unsigned key=0;key<=KEY_MAX;key++)e->state[source][key]=(unsigned char)((bits[key/8U]>>(key%8U))&1U);
 return 0;
}
static int open_events(struct gkd_app_events *e,const struct gkd_app_settings *s,int exclusive,
 struct gkd_menu_guard_owner *guard)
{
 if(!e||!s||e->power_fd>=0||(!exclusive&&guard)){errno=EINVAL;return -1;}
 for(unsigned i=0;i<8U;i++)if(gkd_input_key_code(s->keys[i],&e->keys[i])){errno=EINVAL;return -1;}
 if(gkd_input_key_code(s->menu_key,&e->menu_key)){errno=EINVAL;return -1;}
 e->shot[0]=s->screenshot_pair[0];e->shot[1]=s->screenshot_pair[1];
 e->exclusive=exclusive;
 if(exclusive?(guard?gkd_input_owner_open_menu_reuse(&e->observer,guard):gkd_input_owner_open_menu(&e->observer)):gkd_input_observer_open(&e->observer))return -1;
 for(unsigned i=0;i<32U;i++){
  char path[128],name[128]={0};snprintf(path,sizeof(path),GKD_APP_EVENTS_INPUT "/event%u",i);
  int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
  if(fd<0){if(errno==ENOENT)continue;goto fail;}
  if(ioctl(fd,EVIOCGNAME(sizeof(name)-1U),name)<0){close(fd);goto fail;}
  if(strcmp(name,"GKD Mini AXP173 power key")){close(fd);continue;}
  if(e->power_fd>=0){close(fd);errno=EEXIST;goto fail;}
  e->power_fd=fd;
 }
 if(e->power_fd<0){errno=ENODEV;goto fail;}
 if(snapshot(e,0U,e->observer.physical_fd)||snapshot(e,1U,e->observer.virtual_fd)||snapshot(e,2U,e->power_fd))goto fail;
 if(!exclusive&&e->shot[0]==8U){
  struct gkd_system_hotkey_config config={GKD_SYSTEM_HOTKEY_VERSION,0,e->menu_key,e->keys[e->shot[1]]};
  e->system_fd=open("/dev/gkd-system-hotkey",O_RDONLY|O_NONBLOCK|O_CLOEXEC);
  if(e->system_fd<0||ioctl(e->system_fd,GKD_SYSTEM_HOTKEY_CONFIG,&config))goto fail;
 }
 if(guard&&gkd_input_owner_transfer_menu_guard(&e->observer,guard))goto fail;
 e->barrier=1;e->armed=0;return 0;
fail:{int saved=errno;gkd_app_events_close(e);errno=saved;return -1;}
}
int gkd_app_events_open(struct gkd_app_events *e,const struct gkd_app_settings *s)
{return open_events(e,s,0,NULL);}
int gkd_app_events_open_menu(struct gkd_app_events *e,const struct gkd_app_settings *s)
{return open_events(e,s,1,NULL);}
int gkd_app_events_open_menu_reuse(struct gkd_app_events *e,const struct gkd_app_settings *s,
 struct gkd_menu_guard_owner *guard)
{return open_events(e,s,1,guard);}
static unsigned logical_state(const struct gkd_app_events *e)
{
 unsigned logical=0;
 for(unsigned i=0;i<3U;i++){
  if(e->state[i][KEY_POWER])logical|=1U;
  if(e->state[i][e->keys[3]])logical|=2U;
  if(e->state[i][e->menu_key])logical|=8U;
 }
 if(e->shot[0]){
  unsigned first=e->shot[0]==8U?e->menu_key:e->keys[e->shot[0]],second=e->keys[e->shot[1]];
  if(e->state[0][second]||e->state[1][second])logical|=32U;
  if((e->state[0][first]||e->state[1][first])&&(logical&32U))logical|=4U;
 }
 return logical;
}
static void edge(struct gkd_app_events *e,unsigned *events)
{
 unsigned logical=logical_state(e);
 int prefix=e->shot[0]==8U;
 if(prefix){
  if(!(e->previous&8U)&&(logical&8U)){
   e->shot_primed=!(logical&32U);e->shot_latched=0;
  }
  if((logical&8U)&&!(logical&32U)&&!e->shot_latched)e->shot_primed=1;
 }
 if(e->barrier){e->armed=0;e->shot_primed=0;if(logical&8U)e->shot_latched=1;}
 else if(e->armed){
  if((e->previous&1U)&&!(logical&1U))*events|=GKD_APP_EVENT_POWER;
  if((e->previous&2U)&&!(logical&2U))*events|=GKD_APP_EVENT_RETURN;
  if((e->previous&8U)&&!(logical&8U)&&(!prefix||!e->shot_latched))*events|=GKD_APP_EVENT_SETTINGS;
  if(!(e->previous&4U)&&(logical&4U)&&(!prefix||(e->shot_primed&&!e->shot_latched))){
   *events|=GKD_APP_EVENT_SCREENSHOT;if(prefix)e->shot_latched=1;
  }
 }
 if(prefix&&!(logical&8U)){e->shot_primed=0;e->shot_latched=0;}
 if(!e->barrier&&!(logical&15U))e->armed=1;
 e->previous=logical;
}
struct queued_event {struct input_event input;unsigned source,order;};
static int compare_events(const void *left,const void *right)
{
 const struct queued_event *a=left,*b=right;
 if(a->input.time.tv_sec!=b->input.time.tv_sec)return a->input.time.tv_sec<b->input.time.tv_sec?-1:1;
 if(a->input.time.tv_usec!=b->input.time.tv_usec)return a->input.time.tv_usec<b->input.time.tv_usec?-1:1;
 return a->order<b->order?-1:a->order>b->order;
}
static int drain(struct gkd_app_events *e,unsigned source,int fd,
 struct queued_event *queued,unsigned *count,unsigned *discard)
{
 struct input_event input[16];unsigned batches=0;
 for(;;){
  ssize_t n=read(fd,input,sizeof(input));
  if(n<0&&errno==EINTR)continue;
  if(n<0&&errno==EAGAIN){
   if(e->dropped[source]){if(snapshot(e,source,fd))return -1;e->dropped[source]=0;e->barrier=1;*discard|=1U<<source;}
   return 0;
  }
  if(n<=0||n%(ssize_t)sizeof(input[0])){errno=EIO;return -1;}
  for(unsigned i=0;i<(unsigned)n/sizeof(input[0]);i++){
   if(input[i].type==EV_SYN&&input[i].code==SYN_DROPPED){e->dropped[source]=1;e->barrier=1;}
   if(e->dropped[source])continue;
   if(input[i].type==EV_KEY&&input[i].code<=KEY_MAX){
    if(input[i].value<0||input[i].value>2||input[i].time.tv_usec<0||input[i].time.tv_usec>=1000000){errno=EPROTO;return -1;}
    if(*count>=256U){errno=EOVERFLOW;return -1;}
    queued[*count]=(struct queued_event){input[i],source,*count};++*count;
   }
  }
  if(++batches>=64U){errno=EOVERFLOW;return -1;}
 }
}
int gkd_app_events_poll(struct gkd_app_events *e,int blocked,unsigned *out)
{
 unsigned events=0,count=0,discard=0;struct queued_event queued[256];
 int fds[3]={e->observer.physical_fd,e->observer.virtual_fd,e->power_fd};
 if(!out){errno=EINVAL;return -1;}
 if(blocked)e->barrier=1;
 for(unsigned i=0;i<3U;i++)if(drain(e,i,fds[i],queued,&count,&discard))return -1;
 qsort(queued,count,sizeof(queued[0]),compare_events);
 for(unsigned i=0;i<count;i++){
  struct queued_event *q=&queued[i];
  if(discard&(1U<<q->source))continue;
  e->state[q->source][q->input.code]=q->input.value!=0;events|=GKD_APP_EVENT_ACTIVITY;edge(e,&events);
 }
 edge(e,&events);
 for(unsigned source=0;source<3U;source++)for(unsigned key=0;key<=KEY_MAX;key++)
  if(e->state[source][key])events|=GKD_APP_EVENT_ACTIVITY;
 if(e->barrier)events&=GKD_APP_EVENT_ACTIVITY;
 if(!blocked){e->barrier=0;if(!(e->previous&15U))e->armed=1;}
 *out=events;return 0;
}
void gkd_app_events_close(struct gkd_app_events *e)
{
 if(e->system_fd>=0)close(e->system_fd);
 if(e->power_fd>=0)close(e->power_fd);
 if(e->exclusive)(void)gkd_input_owner_close(&e->observer);
 else gkd_input_observer_close(&e->observer);
 gkd_app_events_init(e);
}
