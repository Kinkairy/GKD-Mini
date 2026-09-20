/* SPDX-License-Identifier: GPL-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "gkd-ui.h"
#include "gkd-ui-plane.h"
#include "gkd-input-owner.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
int gkd_application_menu_main(int,char **);
struct scripted {unsigned time,source;unsigned short type,code;int value;};
static struct scripted script[32];static unsigned count,cursor,clock_ms,held_initial,submits,hides,clears,owners,closed,hide_time,last_selected,fail_submit,fail_hide,fail_poll,signal_time,backward;
static unsigned loading_draws,loading_frame;
static unsigned require_choice,update_menu;
static const char *expected_from="3.5",*expected_to="3.6";
static unsigned setting_saves,setting_polls,setting_failure,setting_last_sleep,setting_last_fps,setting_style,drawn_style;
static void reset(void) {loading_draws=loading_frame=0;update_menu=0;require_choice=0;setting_style=drawn_style=0;setting_saves=setting_polls=setting_failure=setting_last_sleep=setting_last_fps=0;count=cursor=clock_ms=held_initial=submits=hides=clears=owners=closed=hide_time=last_selected=fail_submit=fail_hide=fail_poll=signal_time=backward=0;}
static void add(unsigned t,unsigned source,unsigned short type,unsigned short code,int value) {script[count++]=(struct scripted){t,source,type,code,value};}
int __wrap_clock_gettime(clockid_t id,struct timespec *t) {(void)id;t->tv_sec=0;t->tv_nsec=(long)(backward&&clock_ms>=100?1:clock_ms)*1000000L;return 0;}
int __wrap_open(const char *p,int flags,...) {(void)flags;if(!strcmp(p,"/run/gkd-config/current/effective.conf")){errno=ENOENT;return -1;}assert(!strcmp(p,"/dev/fb0"));return 99;}
int __wrap_fstat(int fd,struct stat *s) {assert(fd==99);memset(s,0,sizeof(*s));s->st_mode=S_IFCHR;return 0;}
int __wrap_close(int fd) {assert(fd==99);return 0;}
int __wrap_poll(struct pollfd *p,nfds_t n,int delay)
{
 assert(n==2 && delay>=0 && delay<=20);clock_ms+=(unsigned)delay;
 if(signal_time && clock_ms>=signal_time){signal_time=0;raise(SIGTERM);errno=EINTR;return -1;}
 if(fail_poll && clock_ms>=100){errno=EIO;return -1;}
 if(cursor<count && script[cursor].time<=clock_ms){p[script[cursor].source].revents=POLLIN;return 1;}return 0;
}
ssize_t __wrap_read(int fd,void *buffer,size_t bytes)
{
 struct input_event *out=buffer;unsigned n=0,source=(unsigned)(fd-10),time;
 assert(source<2);
 if(cursor==count || script[cursor].time>clock_ms || script[cursor].source!=source){errno=EAGAIN;return -1;}
 time=script[cursor].time;
 while(cursor<count && script[cursor].time==time && script[cursor].source==source && (n+1U)*sizeof(*out)<=bytes){
  memset(&out[n],0,sizeof(out[n]));out[n].type=script[cursor].type;out[n].code=script[cursor].code;out[n].value=script[cursor].value;++n;++cursor;
 }
 return (ssize_t)(n*sizeof(*out));
}
ssize_t __wrap___read_chk(int fd,void *buffer,size_t bytes,size_t capacity) {assert(bytes<=capacity);return __wrap_read(fd,buffer,bytes);}
int __wrap___poll_chk(struct pollfd *p,nfds_t n,int delay,size_t capacity) {assert(n*sizeof(*p)<=capacity);return __wrap_poll(p,n,delay);}
int __wrap_ioctl(int fd,unsigned long request,...)
{
 va_list ap;void *data;va_start(ap,request);data=va_arg(ap,void *);va_end(ap);
 if(fd==10 || fd==11) {
  unsigned long *bits=data;memset(bits,0,_IOC_SIZE(request));
  if(held_initial && !clock_ms && fd==10) bits[KEY_LEFTCTRL/(sizeof(long)*8U)]|=1UL<<(KEY_LEFTCTRL%(sizeof(long)*8U));
  return 0;
 }
 assert(fd==99);
 if(request==GKD_UI_MENU_GET_CAPS) {struct gkd_ui_menu_caps *c=data;*c=(struct gkd_ui_menu_caps){GKD_UI_MENU_ABI,320,240,GKD_UI_MENU_RGB565,GKD_UI_MENU_BYTES,20,10000,1000};return 0;}
 if(request==GKD_UI_MENU_SUBMIT) {struct gkd_ui_menu_submit *s=data;assert(s->ttl_ms==2000 && s->sequence==submits+1U);++submits;if(fail_submit){errno=EIO;return -1;}return 0;}
 if(request==GKD_UI_MENU_HIDE) {++hides;hide_time=clock_ms;if(fail_hide){errno=EIO;return -1;}return 0;}
 if(request==GKD_UI_MENU_CLEAR) {++clears;return 0;}
 assert(0);return -1;
}
void gkd_input_owner_init(struct gkd_input_owner *o) {memset(o,0,sizeof(*o));o->physical_fd=o->virtual_fd=-1;}
int gkd_input_owner_open_menu(struct gkd_input_owner *o) {o->physical_fd=10;o->virtual_fd=11;++owners;return 0;}
int gkd_input_owner_close(struct gkd_input_owner *o) {(void)o;++closed;assert(!owners || clears || !submits || fail_submit);return 0;}
void gkd_ui_config_defaults(struct gkd_ui_config *c) {memset(c,0,sizeof(*c));c->loading_interval_ms=80U;}
void gkd_ui_render_loading(struct gkd_ui_surface *s,const struct gkd_ui_config *c,const struct gkd_ui_font *f,unsigned frame) {(void)c;(void)f;assert(frame==loading_draws++);loading_frame=frame;memset(s->pixels,(int)frame,GKD_UI_MENU_BYTES);}
int gkd_ui_font_load(struct gkd_ui_font *f,const char *p) {(void)p;memset(f,0,sizeof(*f));return 0;}
void gkd_ui_font_release(struct gkd_ui_font *f) {(void)f;}
int gkd_ui_render_menu(struct gkd_ui_surface *s,const struct gkd_ui_config *c,const struct gkd_ui_font *f,const struct gkd_ui_menu *m) {
 (void)c;(void)f;assert(!update_menu&&m->action_a!=GKD_UI_ACTION_HIDDEN);
 assert(m->action_b==(require_choice?GKD_UI_ACTION_DISABLED:m->action_a));last_selected=m->selected;memset(s->pixels,0,GKD_UI_MENU_BYTES);return 0;
}
unsigned gkd_ui_confirmation_visible(const struct gkd_ui_config *c) {(void)c;return 7U;}
int gkd_ui_layout_text(const struct gkd_ui_config *c,const struct gkd_ui_font *f,const char *text,struct gkd_ui_text_layout *out)
{
 (void)c;(void)f;memset(out,0,sizeof(*out));out->count=1U;
 for(const char *p=text;*p;p++) {
  if(*p=='\n'){assert(++out->count<=32U);continue;}
  char *line=out->storage[out->count-1U];size_t n=strlen(line);assert(n<63U);line[n]=*p;
 }
 for(unsigned i=0;i<out->count;i++)out->lines[i]=out->storage[i];
 return 0;
}
int gkd_ui_render_confirmation_info(struct gkd_ui_surface *s,const struct gkd_ui_config *c,const struct gkd_ui_font *f,const struct gkd_ui_confirmation *info)
{
 (void)c;(void)f;assert(info->action_a==GKD_UI_ACTION_ENABLED&&info->action_b==GKD_UI_ACTION_ENABLED);
 if(update_menu==1U){
  char from[64],to[64];snprintf(from,sizeof(from),"CURRENT  %s",expected_from);snprintf(to,sizeof(to),"TARGET  %s",expected_to);
  assert(info->count==4U&&!strcmp(info->lines[0],from)&&!strcmp(info->lines[1],to)&&!info->lines[2][0]);
  assert(!strcmp(info->lines[3],"INSTALL THIS UPDATE?")&&info->first==0U);
 }else assert(update_menu==2U&&info->count==10U&&!strcmp(info->title,"DETAILS")&&!strcmp(info->lines[0],"CALLER TEXT"));
 last_selected=info->first;memset(s->pixels,0,GKD_UI_MENU_BYTES);return 0;
}
int gkd_ui_render_settings(struct gkd_ui_surface *s,const struct gkd_ui_config *c,const struct gkd_ui_font *f,const struct gkd_ui_menu *m,const char *const values[],const char *hint)
{
 drawn_style=c->input_style;
 assert(m->count==6U&&!values[5]&&values&&(!strcmp(values[2],"OFF")||!strcmp(values[2],"ON")));
 assert(!strcmp(values[3],"EN")||!strcmp(values[3],"CN"));
 assert(!strcmp(m->action_a_label,c->action_yes)&&!strcmp(m->action_b_label,c->action_no));
 assert(!hint);
 setting_last_sleep=(unsigned)atoi(values[1]);setting_last_fps=!strcmp(values[2],"ON");
 return gkd_ui_render_menu(s,c,f,m);
}
int gkd_settings_exchange(const char *path,const char *request,char *reply,size_t capacity)
{
 assert(path&&!strcmp(path,"socket")&&!hides&&!closed);
 if(!strncmp(request,"settings-save ",14U)){
  assert(loading_draws&&submits);++setting_saves;assert(strstr(request," 1 5 1 0"));snprintf(reply,capacity,"GKD_APP_SETTINGS=SAVING\n");return 0;
 }
 assert(!strcmp(request,"settings-status"));++setting_polls;
 if(setting_failure==1)snprintf(reply,capacity,"GKD_APP_SETTINGS=FAILED state=recoverable errno=5\n");
 else if(setting_failure==2)snprintf(reply,capacity,"GKD_APP_SETTINGS=SAVED generation=bad\n");
 else if(setting_polls==1||(setting_failure==3&&clock_ms<62000U))snprintf(reply,capacity,"GKD_APP_SETTINGS=SAVING\n");
 else snprintf(reply,capacity,"GKD_APP_SETTINGS=SAVED generation=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n");
 return 0;
}
static int run(const char *effects) {
 char *argv[]={"gkd-application-menu","font.psf","USB",(char *)effects,"1000","KEY_UP","KEY_DOWN","KEY_LEFTCTRL","KEY_LEFTALT",NULL};
 return gkd_application_menu_main(9,argv);
}
static int run_settings(void)
{
 char style[4];snprintf(style,sizeof(style),"%u",setting_style);
 char *args[]={"menu","font","SETTINGS","disabled","1000","KEY_UP","KEY_DOWN","KEY_LEFTCTRL","KEY_LEFTALT",
  "KEY_LEFT","KEY_RIGHT","1","0","0","0","cnfont","cnfont12","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","socket",style,NULL};
 return gkd_application_menu_main(20,args);
}
static void setting_edits(void)
{
 add(20,0,EV_KEY,KEY_DOWN,1);add(20,0,EV_KEY,KEY_DOWN,0);
 add(40,0,EV_KEY,KEY_RIGHT,1);add(40,0,EV_KEY,KEY_RIGHT,0);
 add(60,0,EV_KEY,KEY_DOWN,1);add(60,0,EV_KEY,KEY_DOWN,0);
 add(80,0,EV_KEY,KEY_RIGHT,1);add(80,0,EV_KEY,KEY_RIGHT,0);
}
int main(void)
{
 reset();add(20,0,EV_KEY,KEY_DOWN,1);add(20,0,EV_KEY,KEY_DOWN,0);add(80,0,EV_KEY,KEY_LEFTCTRL,1);add(100,0,EV_KEY,KEY_LEFTCTRL,0);
 assert(!run("enabled"));assert(last_selected==1 && hides==1 && clears==1 && closed==1 && clock_ms>=hide_time+240);
 reset();held_initial=1;add(20,0,EV_KEY,KEY_LEFTCTRL,2);add(40,0,EV_KEY,KEY_LEFTCTRL,0);add(80,0,EV_KEY,KEY_LEFTALT,1);add(100,0,EV_KEY,KEY_LEFTALT,0);
 assert(!run("disabled"));assert(hide_time>=80 && hides==1 && closed==1);
 reset();signal_time=70000U;assert(!run("enabled"));assert(submits>60U && hide_time>=70000U && closed==1);
 reset();signal_time=60;assert(!run("enabled"));assert(hides==1 && closed==1);
 reset();fail_submit=1;assert(run("enabled")==1 && closed==1);
 reset();fail_hide=1;assert(run("enabled")==1 && clears==1 && closed==1);
 reset();fail_poll=1;assert(run("enabled")==1 && clears==1 && closed==1);
 reset();backward=1;assert(run("enabled")==1 && clears==1 && closed==1);
 reset();add(20,0,EV_SYN,SYN_DROPPED,0);add(20,0,EV_KEY,KEY_LEFTCTRL,1);add(40,0,EV_SYN,SYN_REPORT,0);add(40,0,EV_KEY,KEY_LEFTCTRL,1);
 add(100,0,EV_KEY,KEY_LEFTALT,1);add(120,0,EV_KEY,KEY_LEFTALT,0);assert(!run("enabled"));assert(hide_time>=100 && closed==1);
 reset();
 { char *args[]={"menu","font","USB","disabled","200","KEY_UP","KEY_DOWN","KEY_LEFTCTRL","KEY_LEFTALT","2",NULL};
   signal_time=70000U;assert(!gkd_application_menu_main(10,args));assert(last_selected==2&&hide_time>=70000U&&closed==1);
   reset();args[9]="3";assert(gkd_application_menu_main(10,args)==2&&owners==0);
 }
 reset();setting_edits();add(100,0,EV_KEY,KEY_LEFTALT,1);add(120,0,EV_KEY,KEY_LEFTALT,0);
 assert(!run_settings()&&!setting_saves&&setting_last_sleep==5&&setting_last_fps==1&&hides==1&&closed==1);
 reset();setting_edits();add(100,0,EV_KEY,KEY_LEFTCTRL,1);add(120,0,EV_KEY,KEY_LEFTCTRL,0);
 add(140,0,EV_KEY,KEY_RIGHT,1);add(140,0,EV_KEY,KEY_RIGHT,0);
 assert(!run_settings()&&setting_saves==1&&setting_polls==2&&hide_time>=200&&setting_last_fps==1);
 reset();setting_failure=1;setting_edits();add(100,0,EV_KEY,KEY_LEFTCTRL,1);add(120,0,EV_KEY,KEY_LEFTCTRL,0);
 add(200,0,EV_KEY,KEY_LEFTALT,1);add(220,0,EV_KEY,KEY_LEFTALT,0);
 assert(!run_settings()&&setting_saves==1&&setting_polls==1&&hide_time>=200);
 reset();setting_failure=2;setting_edits();add(100,0,EV_KEY,KEY_LEFTCTRL,1);add(120,0,EV_KEY,KEY_LEFTCTRL,0);
 assert(run_settings()==1&&setting_saves==1&&!hides&&clears==1&&closed==1);
 reset();held_initial=1;add(20,0,EV_KEY,KEY_LEFTCTRL,2);add(40,0,EV_KEY,KEY_LEFTCTRL,0);
 add(80,0,EV_KEY,KEY_LEFTALT,1);add(100,0,EV_KEY,KEY_LEFTALT,0);
 assert(!run_settings()&&!setting_saves);
 reset();setting_edits();add(100,0,EV_KEY,KEY_LEFTCTRL,1);add(2000,0,EV_KEY,KEY_LEFTCTRL,0);
 assert(!run_settings()&&setting_saves==1&&clock_ms>=2000U&&closed==1);
 reset();setting_failure=3;setting_edits();add(100,0,EV_KEY,KEY_LEFTCTRL,1);add(120,0,EV_KEY,KEY_LEFTCTRL,0);
 assert(!run_settings()&&setting_saves==1&&clock_ms>=62000U&&setting_polls>600U&&closed==1&&loading_frame>700U);
 for(unsigned style=1;style<=2;style++) {
  reset();setting_style=style;setting_edits();
  add(120,0,EV_KEY,KEY_LEFTALT,1);add(140,0,EV_KEY,KEY_LEFTALT,0);
  assert(!run_settings()&&setting_saves==1&&drawn_style==style);
  reset();setting_style=style;setting_edits();
  add(120,0,EV_KEY,KEY_LEFTCTRL,1);add(140,0,EV_KEY,KEY_LEFTCTRL,0);
  assert(!run_settings()&&!setting_saves&&drawn_style==style);
 }
 for(unsigned kind=0;kind<3U;kind++) {
  reset();signal_time=70000U;
  if(kind==2U){setting_edits();assert(!run_settings());assert(!setting_saves&&setting_last_sleep==5&&setting_last_fps==1);}
  else {
   char *args[]={"menu","font",kind?"POWER":"USB","disabled","0","KEY_UP","KEY_DOWN","KEY_LEFTCTRL","KEY_LEFTALT",NULL};
   assert(!gkd_application_menu_main(9,args));
  }
  assert(hide_time>=70000U&&closed==1&&submits>60U);
 }
 for(unsigned style=0;style<3U;style++) {
  reset();require_choice=1;char style_arg[4];snprintf(style_arg,sizeof(style_arg),"%u",style);
  unsigned short yes=style?KEY_LEFTALT:KEY_LEFTCTRL,no=style?KEY_LEFTCTRL:KEY_LEFTALT;
  /* Both sources, repeat and held cancel are consumed without closing. */
  add(20,0,EV_KEY,no,1);add(40,0,EV_KEY,no,2);add(60,0,EV_KEY,no,0);
  add(80,1,EV_KEY,no,1);add(100,1,EV_KEY,no,0);
  add(120,0,EV_KEY,KEY_DOWN,1);add(140,0,EV_KEY,KEY_DOWN,0);
  add(200,0,EV_KEY,yes,1);add(240,0,EV_KEY,yes,0);
  char *args[]={"menu","font","USB","disabled","0","KEY_UP","KEY_DOWN","KEY_LEFTCTRL","KEY_LEFTALT",
   "0","0","cnfont","cnfont12",style_arg,"1",NULL};
  assert(!gkd_application_menu_main(15,args));
  assert(hide_time==200U&&last_selected==1U&&cursor==count&&closed==1U);
  reset();require_choice=1;signal_time=80U;assert(!gkd_application_menu_main(15,args)&&closed==1U);
  reset();args[2]="POWER";assert(gkd_application_menu_main(15,args)==2&&owners==0);
 }
 for(unsigned style=0;style<3U;style++){
  char style_arg[4];snprintf(style_arg,sizeof(style_arg),"%u",style);
  char *args[]={"menu","font","UPDATE","disabled","0","KEY_UP","KEY_DOWN","KEY_LEFTCTRL","KEY_LEFTALT",
   "2","0","cnfont","cnfont12",style_arg,"0","3.5","3.6",NULL};
  unsigned short yes=style?KEY_LEFTALT:KEY_LEFTCTRL,no=style?KEY_LEFTCTRL:KEY_LEFTALT;
  reset();update_menu=1;add(40,0,EV_KEY,no,1);add(60,0,EV_KEY,no,0);
  assert(!gkd_application_menu_main(17,args)&&closed==1&&hide_time==40);
  reset();update_menu=1;
  add(20,0,EV_KEY,KEY_UP,1);add(40,0,EV_KEY,KEY_UP,0);
  add(60,0,EV_KEY,yes,1);add(80,0,EV_KEY,yes,0);
  assert(!gkd_application_menu_main(17,args)&&closed==1&&hide_time==60&&last_selected==0);
  reset();update_menu=1;args[10]="1";expected_from=args[15]="8.2.13";expected_to=args[16]="10.20.30";
  add(40,0,EV_KEY,yes,1);add(60,0,EV_KEY,yes,0);
  assert(!gkd_application_menu_main(17,args)&&closed==1&&hide_time==40);
  expected_from="3.5";expected_to="3.6";

  reset();args[16]="invalid version";assert(gkd_application_menu_main(17,args)==2&&!owners);
 }
 for(unsigned style=0;style<3U;style++){
  reset();setting_style=style;setting_edits();
  for(unsigned row=0;row<3U;row++){
   add(100U+20U*row,0,EV_KEY,KEY_DOWN,1);add(110U+20U*row,0,EV_KEY,KEY_DOWN,0);
  }
  add(160,0,EV_KEY,KEY_RIGHT,1);add(180,0,EV_KEY,KEY_RIGHT,0);
  add(200,0,EV_KEY,style?KEY_LEFTALT:KEY_LEFTCTRL,1);
  add(220,0,EV_KEY,style?KEY_LEFTALT:KEY_LEFTCTRL,0);
  assert(!run_settings()&&setting_saves==1&&last_selected==5U&&closed==1U);
 }
 {
  char *args[]={"menu","font","TEXT","disabled","0","KEY_UP","KEY_DOWN","KEY_LEFTCTRL","KEY_LEFTALT",
   "0","0","cnfont","cnfont12","0","0","DETAILS","CALLER TEXT\n2\n3\n4\n5\n6\n7\n8\n9\n10",NULL};
  reset();update_menu=2;
  add(20,0,EV_KEY,KEY_UP,1);add(20,0,EV_KEY,KEY_UP,0);
  for(unsigned j=0;j<6U;j++){add(40U+20U*j,0,EV_KEY,KEY_DOWN,1);add(40U+20U*j,0,EV_KEY,KEY_DOWN,0);}
  add(180,0,EV_KEY,KEY_LEFTCTRL,1);add(200,0,EV_KEY,KEY_LEFTCTRL,0);
  assert(!gkd_application_menu_main(17,args)&&last_selected==3U&&hide_time==180U&&closed==1U);
 }
 puts("GENERIC_TEXT_RUNNER_PASS caller-title/body/clamped-scroll/explicit-confirm");
 puts("SETTINGS_UPDATE_ENTRY_PASS raw/xbox/ps/action-row/save-before-release");
 puts("UPDATE_CONFIRM_PASS dynamic-package-versions/english-body-in-both-locales/plain-text/raw/xbox/ps/cancel/explicit-install/version-validation");
 puts("USB_REQUIRED_CHOICE_PASS raw/xbox/ps/cancel-consumed/nav-confirm/disabled-back/signal-cleanup");
 puts("MENU_NO_TIMEOUT_PASS USB/POWER/SETTINGS past70s/legacy-duration-ignored/draft-preserved/explicit-cancel");
 puts("SETTINGS_STYLE_INPUT_PASS xbox/ps actual-confirm/actual-cancel/prompt-style");
 puts("SETTINGS_SAVE_BARRIERS_PASS slow-transaction/held-after-save");
 puts("SETTINGS_RUNNER_PASS draft/cancel-no-save/save-holds-input/recoverable-failure/bad-receipt/held-A");
 puts("APPLICATION_MENU_RUNNER_PASS batch/held/drop/renew/hide/signal/errors/clock/cleanup");return 0;
}
