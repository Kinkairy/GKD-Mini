/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-session.h"
#include "gkd-app-media.h"
#include "gkd-app-lifecycle.h"
#include "gkd-app-settings.h"
#include "gkd-settings-values.h"
#include "gkd-app-idle.h"
#include "gkd-app-fps.h"
#include "gkd-app-job.h"
#include "gkd-app-identity.h"
#include "gkd-app-battery.h"
#include "gkd-app-events.h"
#include "gkd-app-profile.h"
#include "gkd-ui.h"
#include "gkd-ui-language.h"
#include "gkd-ui-plane-client.h"
#include "gkd-menu-guard.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define SETTINGS_OSD_MS 2000U
#ifndef APP_RUN
#define APP_RUN "/run/gkd-application"
#endif
#ifndef APP_SOCKET
#define APP_SOCKET APP_RUN "/control.sock"
#endif
#ifndef APP_CONFIG
#define APP_CONFIG "/run/gkd-config/current/effective.conf"
#endif
#ifndef APP_MENU
#define APP_MENU "/usr/sbin/gkd-application-menu"
#endif
#ifndef APP_USB
#define APP_USB "/usr/sbin/gkd-application-usb"
#endif
#ifndef APP_UPDATE
#define APP_UPDATE "/usr/sbin/gkd-application-update"
#endif
#ifndef APP_MANAGEMENT
#define APP_MANAGEMENT "/usr/sbin/gkd-app-management"
#endif
#ifndef APP_TRIAL_MARKER
#define APP_TRIAL_MARKER APP_RUN "/update-trial"
#endif
#ifndef APP_PREPARE
#define APP_PREPARE "/usr/sbin/gkd-update-prepare"
#endif
#ifndef APP_RUNTIME_ID
#define APP_RUNTIME_ID "/etc/gkd-mini/runtime-id"
#endif
#ifndef APP_USB_ONLINE
#define APP_USB_ONLINE "/sys/class/power_supply/usb/online"
#endif
#ifndef APP_UPDATE_LOCK
#define APP_UPDATE_LOCK "/run/gkd-recovery/card-operation.lock"
#endif
#ifndef APP_INPUT_INIT
#define APP_INPUT_INIT "/etc/init.d/S95gkd-input"
#endif
#ifndef APP_NETWORK
#define APP_NETWORK "/usr/sbin/gkd-application-network"
#endif
#ifndef APP_GAME
#define APP_GAME "/usr/sbin/gkd-app-game"
#endif
#ifndef APP_GUARD
#define APP_GUARD "/usr/sbin/gkd-app-card-guard"
#endif
#ifndef APP_CONFIG_STORE
#define APP_CONFIG_STORE "/usr/sbin/gkd-app-config-store"
#endif
#ifndef APP_FB
#define APP_FB "/dev/fb0"
#endif
#ifndef APP_FONT
#define APP_FONT "/etc/gkd-mini/fonts/fallback.psf"
#endif
#ifndef APP_CN_FONT
#define APP_CN_FONT "/etc/gkd-mini/fonts/native-cn.psf"
#endif
#ifndef APP_CN_COMPACT_FONT
#define APP_CN_COMPACT_FONT "/etc/gkd-mini/fonts/native-cn-12.psf"
#endif
#ifndef APP_GENERATION
#define APP_GENERATION "/run/gkd-config/current/generation"
#endif
#ifndef APP_GAME_DISKSEQ
#define APP_GAME_DISKSEQ "/sys/class/block/mmcblk1/diskseq"
#endif
#ifndef APP_LUN
#define APP_LUN "/sys/kernel/config/usb_gadget/gkd_recovery/functions/mass_storage.0/lun.0/file"
#endif
enum menu_kind { MENU_USB, MENU_POWER, MENU_SETTINGS, MENU_UPDATE };
static const char *menu_name(int kind){return kind==MENU_UPDATE?"UPDATE":kind==MENU_SETTINGS?"SETTINGS":kind==MENU_POWER?"POWER":"USB";}
enum job_purpose { JOB_NONE, JOB_MENU, JOB_OPERATION, JOB_SCREENSHOT, JOB_CONFIG_LOAD, JOB_GAME_CHECK, JOB_GAME_EXIT, JOB_GAME_MENU_CHECK, JOB_GAME_MENU, JOB_INPUT_START, JOB_CONFIG_SAVE, JOB_UPDATE_CHECK, JOB_UPDATE_INSPECT, JOB_UPDATE_PREPARE, JOB_UPDATE_GOOD, JOB_MANAGEMENT };
struct service {
 struct gkd_app_session session;
 struct gkd_app_media media;
 struct gkd_app_lifecycle lifecycle;
 struct gkd_app_settings settings;
 struct gkd_ui_catalog texts;
 struct gkd_app_job job,network_job,settings_job;
 struct gkd_app_idle_lease settings_lease,update_lease;
 struct gkd_app_fps fps;
 int fps_visible;
 unsigned fps_drawn_value;enum gkd_app_fps_state fps_drawn_state;
 uint64_t fps_renew,fps_expires,fps_retry,transient_until;
 char settings_generation[65],settings_receipt[160];
 int settings_save_status;
 uint64_t settings_osd_renew;
 char network_config[PATH_MAX];
 int network_etc;
 int network_desired,network_dirty,network_running,network_stopping,network_error;
 uint64_t network_next;
 struct gkd_app_identity identity;
 struct gkd_app_events events;
 struct gkd_ui_font font,cn_font,cn_compact;
 struct gkd_ui_config ui;
 uint32_t osd_pixels[GKD_UI_OSD_WIDTH*GKD_UI_OSD_HEIGHT];
 unsigned osd_sequence,status_sequence,status_kind;
 uint16_t status_pixels[GKD_UI_WIDTH*GKD_UI_HEIGHT];
 uint64_t status_renew,status_hiding,status_expires,charge_until,status_retry,button_osd_until;
 uint64_t job_osd_renew;
 pid_t game_wait_peer;
 int game_wait_pin;
 uint64_t last_activity,battery_next,usb_next,usb_changed,battery_suspend_since;
 int usb_observed,usb_stable,usb_pending,usb_notice_pending,menu_usb_required,low_notified,critical_notified,stopping;
 struct gkd_menu_guard_owner power_guard;
 struct gkd_app_lifecycle_request request;
 int fb,listener,lock,started,menu_kind,menu_released,menu_ready,leave_menu;
 uint64_t update_entry_deadline,update_check_after;
 int update_requested,update_status,update_error,update_lock,trial_checked,trial_pending;
 int update_lease_owned;
 char update_package[65],update_from[32],update_to[32];
 int update_from_settings;
 uint64_t card_generation,card_observed,card_scan_after,card_changed;
 int card_initialized,card_refreshing,notice_controls_pending;
 int config_save_status,config_save_error,stop_host_requested;
 pid_t stopped_host;
 int menu_power_pending;
 int events_live,operation_started,power_event,boot_ready,reported_exit,terminal_error;
 enum job_purpose purpose;
 uint32_t freeze_sequence;
 uint64_t freeze_renew,session_generation;
};
static const struct gkd_ui_font *service_font(const struct service *s)
{return s->settings.chinese?&s->cn_font:&s->font;}
static const char *service_text(const struct service *s,enum gkd_ui_text text)
{return gkd_ui_catalog_text(&s->texts,s->settings.chinese?GKD_UI_CN:GKD_UI_EN,text);}
static volatile sig_atomic_t interrupted;
static void on_signal(int signal_number){(void)signal_number;interrupted=1;}
static uint64_t now_ms(void)
{
 struct timespec t;
 if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;
 return (uint64_t)t.tv_sec*1000U+(uint64_t)t.tv_nsec/1000000U;
}
static struct gkd_app_fps_context fps_context(const struct service *s)
{
 return (struct gkd_app_fps_context){s->session.ready.host,s->session.ready.init,
  s->boot_ready&&s->trial_checked==2&&!s->stopping&&!s->terminal_error&&s->lifecycle.state==GKD_LIFECYCLE_ACTIVE,
  (int)s->settings.show_fps};
}
static void fps_preempt(struct service *s)
{
 if(!s->fps_visible)return;
 if(now_ms()<s->fps_expires&&gkd_ui_plane_clear(s->fb)&&errno!=EBUSY)
  fprintf(stderr,"GKD_APP_FPS=clear-failed errno=%d\n",errno);
 s->fps_visible=0;s->fps_renew=s->fps_expires=0;
}
static void fps_tick(struct service *s,uint64_t now)
{
 struct gkd_app_fps_context context=fps_context(s);
 gkd_app_fps_update(&s->fps,&context,now);
 struct gkd_app_fps_view view=gkd_app_fps_read(&s->fps,&context);
 if(!view.visible||s->purpose!=JOB_NONE||s->request.action!=GKD_LIFECYCLE_ACTION_NONE||
    s->status_kind||now<s->charge_until||now<s->transient_until||now<s->button_osd_until){
  fps_preempt(s);return;
 }
 if(now<s->fps_retry)return;
 if(s->fps_visible&&now<s->fps_renew&&view.state==s->fps_drawn_state&&view.fps==s->fps_drawn_value)return;
 char text[16];
 if(view.state==GKD_APP_FPS_AVAILABLE)snprintf(text,sizeof(text),"FPS %u",view.fps);
 else strcpy(text,"FPS --");
 struct gkd_ui_osd message={text,7U,-1,0};
 if(s->osd_sequence==UINT32_MAX||
    gkd_ui_export_osd_argb(s->osd_pixels,GKD_UI_OSD_WIDTH*GKD_UI_OSD_HEIGHT,&s->ui,&s->font,&message)){
  fps_preempt(s);s->fps_retry=now+1000U;return;
 }
 if(gkd_ui_plane_send(s->fb,s->osd_pixels,1500U,s->settings.effects?160U:0U,++s->osd_sequence)){
  /* Lower priority never asks an existing publisher to yield. */
  if(errno!=EBUSY)fprintf(stderr,"GKD_APP_FPS=publish-failed errno=%d\n",errno);
  s->fps_visible=0;s->fps_retry=now+100U;return;
 }
 s->fps_visible=1;s->fps_renew=now+500U;s->fps_expires=now+1500U;s->fps_retry=0;
 s->fps_drawn_state=view.state;s->fps_drawn_value=view.fps;
}
static int application_directory(struct service *s,const char *relative,const char *device)
{
 char path[192];int pidfd=-1,fd=-1,error;struct stat st,block;
 pidfd=(int)syscall(SYS_pidfd_open,s->session.ready.init,0U);if(pidfd<0)return -1;
 if(snprintf(path,sizeof(path),"/proc/%d/root%s",s->session.ready.init,relative)>=(int)sizeof(path)){
  errno=ENAMETOOLONG;goto done;
 }
 fd=open(path,O_RDONLY|O_CLOEXEC|O_DIRECTORY|O_NOFOLLOW);if(fd<0)goto done;
 struct pollfd pinned={pidfd,POLLIN|POLLHUP|POLLERR,0};
 if(poll(&pinned,1,0)!=0){errno=ESRCH;goto error;}
 if(fstat(fd,&st)||stat(device,&block)||!S_ISBLK(block.st_mode)||st.st_dev!=block.st_rdev){
  errno=EXDEV;goto error;
 }
 goto done;
error:
 error=errno;close(fd);fd=-1;errno=error;
done:
 error=errno;close(pidfd);errno=error;return fd;
}
static int network_start_job(struct service *s,int stop)
{
 int acquired=0;
 if(s->network_etc<0){
  s->network_etc=application_directory(s,"/media/data/local/etc","/dev/mmcblk0p2");
  if(s->network_etc<0)return -1;
  acquired=1;
 }
 if(!s->network_dirty&&!realpath(APP_CONFIG,s->network_config)){
  int error=errno;
  if(acquired){close(s->network_etc);s->network_etc=-1;}
  errno=error;return -1;
 }
 char *argv[]={APP_NETWORK,stop?"stop":"poll","3",s->network_config,NULL};
 int result=gkd_app_job_start_fd(&s->network_job,argv,30000U,now_ms(),s->network_etc),error=errno;
 if(!result){s->network_running=1;s->network_stopping=stop;s->network_dirty=1;}
 else if(acquired&&!s->network_dirty){close(s->network_etc);s->network_etc=-1;}
 errno=error;return result;
}
static void network_poll(struct service *s,uint64_t now)
{
 if(s->network_running){
  enum gkd_app_job_state state=gkd_app_job_poll(&s->network_job,now);
  if(s->network_job.pid>0)return;
  int clean=state==GKD_JOB_DONE;
  if(s->network_stopping){
   if(clean&&!strcmp(s->network_job.output,"GKD_APPLICATION_NETWORK=stopped\n")){
    s->network_dirty=0;s->network_error=0;s->network_config[0]=0;
    /* Keep this capability until cleanup succeeds, even if the app namespace
     * dies. Lifecycle media release is blocked until this close has happened. */
    if(s->network_etc>=0){close(s->network_etc);s->network_etc=-1;}
   }else s->network_error=s->network_job.error?s->network_job.error:EPROTO;
  }
  /* Optional ICS absence is diagnosed without blocking controls or management USB. */
  if(s->network_job.output[0])fprintf(stderr,"%s",s->network_job.output);
  else if(!clean)fprintf(stderr,"GKD_APP_NETWORK=FAILED errno=%d\n",s->network_job.error);
  gkd_app_job_close(&s->network_job);s->network_running=0;s->network_next=now+2000U;
 }
 if(s->network_error||s->network_running)return;
 if(!s->network_desired){
  if(s->network_dirty&&network_start_job(s,1))s->network_error=errno;
 }else if(s->lifecycle.state==GKD_LIFECYCLE_ACTIVE&&now>=s->network_next){
  if(network_start_job(s,0)){
   fprintf(stderr,"GKD_APP_NETWORK=FAILED errno=%d\n",errno);s->network_next=now+2000U;
  }
 }
}
static int events_mode(struct service *s,int exclusive)
{
 gkd_app_events_close(&s->events);s->events_live=0;
 if(exclusive?(s->power_guard.lease_fd>=0?
    gkd_app_events_open_menu_reuse(&s->events,&s->settings,&s->power_guard):
    gkd_app_events_open_menu(&s->events,&s->settings)):
    gkd_app_events_open(&s->events,&s->settings))return -1;
 s->events_live=1;return 0;
}
/* USB modes share the lower-left OSD. System-owned card states use the
 * existing full-screen text page with disabled actions, never a frontend alert. */
static int status_is_card(unsigned kind)
{return kind==6U||kind==7U;}
/* Keep evdev grabs while allowing the newly spawned controls process to
 * restore hardware under its shared lease. No frontend input is released. */
static int notice_start_prepare(struct service *s)
{
 if(!s->card_refreshing&&!(s->card_initialized&&!s->card_generation)&&
    !gkd_app_lifecycle_wait_kind(&s->lifecycle))return 0;
 if(!s->events.exclusive&&events_mode(s,1))return -1;
 gkd_menu_guard_owner_exit(&s->events.observer.menu_guard);
 s->notice_controls_pending=1;return 0;
}
static int notice_start_complete(struct service *s)
{
 if(!s->notice_controls_pending)return 0;
 if(!s->events.exclusive){errno=EPROTO;return -1;}
 if(gkd_menu_guard_owner_enter(&s->events.observer.menu_guard))return -1;
 s->notice_controls_pending=0;return 0;
}
static int status_is_notice(unsigned kind)
{return status_is_card(kind)||(kind>=8U&&kind<=12U);}
static int status_is_loading(unsigned kind)
{return kind==4U||kind==7U||(kind>=8U&&kind<=12U);}
static int status_is_osd(unsigned kind)
{return kind==1U||kind==2U||kind==3U||kind==5U;}
static void reset_status(struct service *s)
{
 s->status_kind=0;s->status_hiding=0;s->status_renew=0;
 s->status_sequence=0;s->status_expires=0;s->status_retry=0;
}
static int hide_status(struct service *s,uint64_t now)
{
 if(!s->status_kind)return 0;
 /* Publish the existing spinner immediately, without menu slide transitions. */
 if(status_is_loading(s->status_kind)){
  if(gkd_ui_menu_clear(s->fb))return -1;
  reset_status(s);return 0;
 }
 if(status_is_osd(s->status_kind)){
  /* An expired lease may already belong to controls: never clear that owner. */
  if(now>=s->status_expires){reset_status(s);return 0;}
  if(!s->status_hiding){
   unsigned fade=s->settings.effects?160U:0U;
   if(!fade){
    if(gkd_ui_plane_clear(s->fb)&&errno!=EBUSY)return -1;
    reset_status(s);return 0;
   }
   if(s->osd_sequence==UINT32_MAX){errno=EOVERFLOW;return -1;}
   if(gkd_ui_plane_send(s->fb,s->osd_pixels,fade,fade,++s->osd_sequence)){
    if(errno!=EBUSY)return -1;
    reset_status(s);return 0;
   }
   s->status_expires=now+fade;s->status_hiding=s->status_expires;
  }
  return 1;
 }
 if(!s->status_hiding){
  if(gkd_ui_menu_hide(s->fb))return -1;
  s->status_hiding=now+(s->settings.effects?240U:40U);return 1;
 }
 if(now<s->status_hiding)return 1;
 if(gkd_ui_menu_clear(s->fb))return -1;
 reset_status(s);return 0;
}
/* One power OSD for insertion, removal and explicit CHARGE acknowledgement. */
static void power_osd(struct service *s,const unsigned *curve,struct gkd_ui_osd *message,
 char *text,size_t capacity)
{
 struct gkd_app_battery reading;
 message->icon=2U;message->level=-1;message->critical=0;
 if(!gkd_app_battery_read(&reading,curve)){
  if(reading.percent<=(int)s->settings.battery_low)
   snprintf(text,capacity,"%s",service_text(s,GKD_UI_TEXT_BAT_LOW));
  else snprintf(text,capacity,"%s %d%%",service_text(s,GKD_UI_TEXT_POWER),reading.percent);
  message->level=reading.percent;
 }else{
  snprintf(text,capacity,"%s --%%",service_text(s,GKD_UI_TEXT_POWER));
  fprintf(stderr,"GKD_APP_POWER_BATTERY=UNAVAILABLE errno=%d\n",errno);
 }
 message->text=text;
}
static int status_screen(struct service *s,uint64_t now)
{
 if(s->game_wait_peer){
  struct pollfd p={s->game_wait_pin,POLLIN|POLLHUP,0};
  if(s->stopping||s->lifecycle.state!=GKD_LIFECYCLE_ACTIVE||poll(&p,1,0)!=0){
   close(s->game_wait_pin);s->game_wait_peer=0;s->game_wait_pin=-1;
  }
 }
 unsigned waiting=gkd_app_lifecycle_wait_kind(&s->lifecycle);
 unsigned kind=waiting?7U+waiting:
  s->lifecycle.state==GKD_LIFECYCLE_STORAGE?1U:
  s->lifecycle.state==GKD_LIFECYCLE_DEBUG?2U:s->lifecycle.state==GKD_LIFECYCLE_RECOVERY?3U:(s->update_requested||s->update_entry_deadline||s->purpose==JOB_UPDATE_PREPARE||s->purpose==JOB_UPDATE_INSPECT||s->purpose==JOB_UPDATE_GOOD)?4U:
  s->card_refreshing?7U:
  s->card_initialized&&!s->card_generation&&s->lifecycle.state==GKD_LIFECYCLE_ACTIVE?6U:
  s->game_wait_peer?11U:
  s->job_osd_renew?12U:
  s->lifecycle.state==GKD_LIFECYCLE_ACTIVE&&s->usb_stable==1&&now<s->charge_until?5U:0U;
 if(status_is_loading(kind)&&status_is_osd(s->status_kind)){
  /* A launcher acknowledgement must not precede the actual waiting page. */
  if(now<s->status_expires&&gkd_ui_plane_clear(s->fb)&&errno!=EBUSY)return -1;
  reset_status(s);
 }
 if(status_is_notice(kind)&&kind==s->status_kind&&s->status_hiding){s->status_hiding=0;s->status_renew=0;}
 if(s->status_kind&&kind!=s->status_kind&&!(status_is_card(kind)&&status_is_card(s->status_kind))){
  int hidden=hide_status(s,now);if(hidden)return hidden<0?-1:0;
  if(s->lifecycle.state==GKD_LIFECYCLE_ACTIVE&&s->events.exclusive&&events_mode(s,0))return -1;
 }
 if(!kind||s->status_hiding)return 0;
 fps_preempt(s);
 /* Input devices are replaced by the restart job while no frontend exists.
  * Reacquire their new descriptors immediately before starting the host. */
 if(kind!=5U&&kind!=11U&&kind!=12U&&!(status_is_notice(kind)&&s->purpose==JOB_INPUT_START)&&
    !s->events.exclusive&&events_mode(s,1))return -1;
 if(status_is_osd(kind)){
  unsigned ttl=kind==5U?(unsigned)(s->charge_until-now):2000U;
  if(kind==s->status_kind&&(kind==5U||now<s->status_renew))return 0;
  if(ttl<GKD_UI_PLANE_MIN_TTL_MS||now<s->status_retry)return 0;
  if(kind!=s->status_kind){
   struct gkd_app_settings current;
   char text[24];
   struct gkd_ui_osd message={NULL,kind==3U?GKD_UI_OSD_ICON_FAILURE:kind==1U?6U:7U,-1,kind==3U};
   if(gkd_app_settings_load(APP_CONFIG,&current))return -1;
   s->settings.effects=current.effects;s->settings.chinese=current.chinese;
   message.text=service_text(s,kind==3U?GKD_UI_TEXT_ACTION_FAILED:kind==1U?GKD_UI_TEXT_STORAGE:GKD_UI_TEXT_DEBUG);
   if(kind==5U)power_osd(s,current.battery_curve,&message,text,sizeof(text));
   if(gkd_ui_export_osd_argb(s->osd_pixels,GKD_UI_OSD_WIDTH*GKD_UI_OSD_HEIGHT,&s->ui,service_font(s),&message))return -1;
  }
  if(s->osd_sequence==UINT32_MAX){errno=EOVERFLOW;return -1;}
  if(gkd_ui_plane_send(s->fb,s->osd_pixels,ttl,s->settings.effects?160U:0U,++s->osd_sequence)){
   /* Let an already-visible volume/screenshot lease finish naturally. */
   if(errno==EBUSY){s->status_retry=now+100U;return 0;}
   return -1;
  }
  if(kind!=3U&&kind!=s->status_kind)fprintf(stderr,"GKD_APP_USB_OSD=%s ttl_ms=%u\n",kind==1U?"STORAGE":kind==2U?"DEBUG":"CHARGE",ttl);
  s->status_kind=kind;s->status_renew=now+500U;s->status_expires=now+ttl;s->status_retry=0;
  return 0;
 }
 if(kind!=s->status_kind)s->status_renew=0;
 if(!s->status_renew||now>=s->status_renew){
  struct gkd_ui_config config=s->ui;
  struct gkd_ui_surface surface={s->status_pixels,GKD_UI_WIDTH,GKD_UI_HEIGHT,GKD_UI_WIDTH};
  if(status_is_loading(kind)){
   gkd_ui_render_loading(&surface,&config,service_font(s),s->status_sequence);
  }else if(kind==6U){
   struct gkd_ui_text_layout text;
   config.input_style=s->settings.input_style;
   snprintf(config.action_yes,sizeof(config.action_yes),"%s",service_text(s,GKD_UI_TEXT_YES));
   snprintf(config.action_no,sizeof(config.action_no),"%s",service_text(s,GKD_UI_TEXT_NO));
   if(gkd_ui_layout_text(&config,service_font(s),service_text(s,GKD_UI_TEXT_INSERT_CARD),&text))return -1;
   const struct gkd_ui_confirmation info={service_text(s,GKD_UI_TEXT_GAME_CARD),text.lines,text.count,0U,
    GKD_UI_ACTION_DISABLED,GKD_UI_ACTION_DISABLED};
   if(gkd_ui_render_confirmation_info(&surface,&config,service_font(s),&info))return -1;
  }else {errno=EPROTO;return -1;}
  if(s->status_sequence==UINT32_MAX){errno=EOVERFLOW;return -1;}
  if(gkd_ui_menu_send(s->fb,s->status_pixels,2000U,status_is_loading(kind)?0U:s->settings.effects?200U:0U,++s->status_sequence))return -1;
  s->status_renew=now+(status_is_loading(kind)?s->ui.loading_interval_ms:500U);s->status_kind=kind;
 }
 return 0;
}
static int begin(void *opaque,const struct gkd_app_lifecycle_request *request)
{
 struct service *s=opaque;
 if(s->request.action!=GKD_LIFECYCLE_ACTION_NONE){errno=EBUSY;return -1;}
 s->request=*request;s->operation_started=0;s->charge_until=0;
 if(request->action!=GKD_LIFECYCLE_ACTION_MENU_ACQUIRE&&request->action!=GKD_LIFECYCLE_ACTION_MENU_RELEASE)
  s->network_desired=0;
 s->network_error=0;return 0;
}
static void finish(struct service *s,int error)
{
 uint64_t token=s->request.token;
 enum gkd_app_lifecycle_action action=s->request.action;
 s->request.action=GKD_LIFECYCLE_ACTION_NONE;s->operation_started=0;
 fprintf(stderr,"GKD_APP_ACTION=%u token=%llu result=%s errno=%d\n",
  (unsigned)action,(unsigned long long)token,error?"FAILED":"PASS",error);
 if(error){
  if(gkd_app_lifecycle_fail(&s->lifecycle,token,error))s->terminal_error=errno;
  if(s->update_status==3){
   s->update_status=-1;s->update_error=error;
   fprintf(stderr,"GKD_APP_UPDATE=FAILED errno=%d\n",error);
  }
 }
 else if(gkd_app_lifecycle_complete(&s->lifecycle,token))s->terminal_error=errno;
 /* Menu owners grab input away from the observer. Returning to the active
  * frontend starts a new idle interval; time in menus/USB/suspend is not idle. */
 if(!error&&!s->terminal_error&&s->lifecycle.state==GKD_LIFECYCLE_ACTIVE)
  s->last_activity=now_ms();
 if(!error&&action==GKD_LIFECYCLE_ACTION_POWER_RESUME)
  s->battery_suspend_since=0; /* A deliberate wake always gets a fresh grace period. */
 if(!error&&action==GKD_LIFECYCLE_ACTION_USB_CHARGE&&s->usb_stable==1&&!s->menu_usb_required){
  struct gkd_app_settings current;
  if(gkd_app_settings_load(APP_CONFIG,&current))s->terminal_error=errno;
  else s->charge_until=now_ms()+current.usb_notify_ms;
 }
 if(!error&&!s->stopping&&(action==GKD_LIFECYCLE_ACTION_USB_CHARGE||
    action==GKD_LIFECYCLE_ACTION_APP_RESUME||action==GKD_LIFECYCLE_ACTION_DISPLAY_THAW||action==GKD_LIFECYCLE_ACTION_POWER_RESUME))
  s->network_desired=s->usb_stable==1;
}
static int job_start(struct service *s,char *const argv[],unsigned timeout,enum job_purpose purpose)
{
 if(s->purpose!=JOB_NONE){errno=EBUSY;return -1;}
 if(gkd_app_job_start(&s->job,argv,timeout,now_ms()))return -1;
 s->purpose=purpose;return 0;
}
static int command(struct service *s,const char *path,const char *argument)
{
 char *argv[]={(char *)path,(char *)argument,NULL};
 return job_start(s,argv,30000U,JOB_OPERATION);
}
static int freeze(struct service *s)
{
 struct gkd_ui_freeze_submit request={10000U,0U,{0U,0U}};
 if(s->freeze_sequence==UINT32_MAX){errno=EOVERFLOW;return -1;}
 request.sequence=++s->freeze_sequence;
 if(ioctl(s->fb,GKD_UI_FREEZE_SUBMIT,&request))return -1;
 s->freeze_renew=now_ms()+500U;return 0;
}

static int settings_hash(const char *hash)
{
 if(strlen(hash)!=64U)return 0;
 for(unsigned i=0;i<64U;i++)if(!((hash[i]>='0'&&hash[i]<='9')||(hash[i]>='a'&&hash[i]<='f')))return 0;
 return 1;
}
static int read_generation(char value[65])
{
 char bytes[66];struct stat st;int fd=open(APP_GENERATION,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0)return -1;
 ssize_t n=read(fd,bytes,sizeof(bytes));int error=errno;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid||(st.st_mode&0022)){close(fd);errno=EPERM;return -1;}
 close(fd);
 if(n!=65||bytes[64]!='\n'){errno=n<0?error:EPROTO;return -1;}
 memcpy(value,bytes,64U);value[64]=0;
 if(!settings_hash(value)){errno=EPROTO;return -1;}return 0;
}
static int settings_snapshot(struct service *s)
{
 struct gkd_app_settings settings;struct gkd_ui_catalog texts;char before[65],after[65];
 if(read_generation(before)||gkd_app_settings_load(APP_CONFIG,&settings)||gkd_ui_catalog_load(&texts,APP_CONFIG)||read_generation(after))return -1;
 if(strcmp(before,after)){errno=ESTALE;return -1;}
 if(settings.auto_suspend_seconds%60U||!gkd_settings_sleep_valid(settings.auto_suspend_seconds/60U)){errno=ERANGE;return -1;}
 s->settings=settings;s->texts=texts;memcpy(s->settings_generation,after,sizeof(after));return 0;
}
static int settings_failure(const char *text,int *recoverable,int *error)
{
 const char *prefix="GKD_APP_SETTINGS=FAILED state=recoverable errno=";
 *recoverable=1;
 if(strncmp(text,prefix,strlen(prefix))){
  prefix="GKD_APP_SETTINGS=FAILED state=unknown errno=";*recoverable=0;
  if(strncmp(text,prefix,strlen(prefix)))return -1;
 }
 const char *digits=text+strlen(prefix);size_t length=strlen(digits);unsigned value=0;
 if(length<2U||length>5U||digits[0]=='0'||digits[length-1U]!='\n')return -1;
 for(size_t i=0;i+1U<length;i++){
  if(digits[i]<'0'||digits[i]>'9')return -1;
  value=value*10U+(unsigned)(digits[i]-'0');
 }
 if(!value||value>4095U)return -1;
 *error=(int)value;return 0;
}
static int osd(struct service *s,const char *text,unsigned icon,int level,int critical,unsigned ttl);
static int result_osd(struct service *s,int success,unsigned ttl)
{
 return osd(s,service_text(s,success?GKD_UI_TEXT_ACTION_SUCCESS:GKD_UI_TEXT_ACTION_FAILED),
  success?GKD_UI_OSD_ICON_SUCCESS:GKD_UI_OSD_ICON_FAILURE,-1,!success,ttl);
}
static void settings_poll(struct service *s)
{
 if(s->settings_save_status!=1)return;
 enum gkd_app_job_state state=gkd_app_job_poll(&s->settings_job,now_ms());
 /* The menu owns the fullscreen plane and renders the shared spinner while
  * saving. The service reports completion/failure, without a competing OSD. */
 if(s->settings_job.pid>0)return;
 static const char success[]="GKD_APP_SETTINGS=SAVED generation=";
 int saved=0;char expected[160],hash[65];int error=0;
 if(state==GKD_JOB_DONE&&strlen(s->settings_job.output)==sizeof(success)-1U+65U&&
    !strncmp(s->settings_job.output,success,sizeof(success)-1U)){
  memcpy(hash,s->settings_job.output+sizeof(success)-1U,64U);hash[64]=0;
  snprintf(expected,sizeof(expected),"%s%s\n",success,hash);
  if(settings_hash(hash)&&!strcmp(s->settings_job.output,expected)&&!settings_snapshot(s)&&
     !strcmp(s->settings_generation,hash))saved=1;
 }
 if(saved){
  s->settings_save_status=2;memcpy(s->settings_receipt,expected,strlen(expected)+1U);
  /* End progress before the menu starts closing. A success toast is emitted
   * only after the menu confirms its animation and held-key release ended. */
  if(!gkd_ui_plane_clear(s->fb))s->transient_until=0;
 }
 else{
  int recoverable=0;
  if(state!=GKD_JOB_FAILED||settings_failure(s->settings_job.output,&recoverable,&error)){
   recoverable=0;error=EPROTO;
  }
  s->settings_save_status=recoverable?-1:-2;
  snprintf(s->settings_receipt,sizeof(s->settings_receipt),"GKD_APP_SETTINGS=FAILED state=%s errno=%d\n",recoverable?"recoverable":"unknown",error);
  if(!recoverable)s->terminal_error=error;
  (void)result_osd(s,0,SETTINGS_OSD_MS);
 }
 s->settings_osd_renew=0;
 gkd_app_job_close(&s->settings_job);
 /* No owner may resume SM while persistence is still changing. */
 if(s->purpose!=JOB_MENU)gkd_app_idle_lease_release(&s->settings_lease);
}
static int settings_save(struct service *s,const char *request,pid_t peer)
{
 char hash[65],canonical[160],a[4],sleep[4],fps[4],language[4],style[4];
 unsigned animation,minutes,show_fps,chinese,input_style;int used=0,fd,result,error;
 if(s->purpose!=JOB_MENU||s->menu_kind!=MENU_SETTINGS||!s->menu_ready||peer!=s->job.pid||
    s->settings_save_status==1||s->settings_save_status==2||s->stopping){errno=EBUSY;return -1;}
 if(sscanf(request,"settings-save %64s %u %u %u %u %u%n",hash,&animation,&minutes,&show_fps,&chinese,&input_style,&used)!=6||
    !used||request[used]||!settings_hash(hash)||animation>1U||!gkd_settings_sleep_valid(minutes)||show_fps>1U||chinese>1U||input_style>2U){errno=EINVAL;return -1;}
 snprintf(canonical,sizeof(canonical),"settings-save %s %u %u %u %u %u",hash,animation,minutes,show_fps,chinese,input_style);
 if(strcmp(canonical,request)||strcmp(hash,s->settings_generation)){errno=ESTALE;return -1;}
 if(gkd_app_idle_lease_live(&s->settings_lease))return -1;
 fd=application_directory(s,"/media/data/local/etc","/dev/mmcblk0p2");if(fd<0)return -1;
 snprintf(a,sizeof(a),"%u",animation);snprintf(sleep,sizeof(sleep),"%u",minutes);
 snprintf(fps,sizeof(fps),"%u",show_fps);snprintf(language,sizeof(language),"%u",chinese);
 snprintf(style,sizeof(style),"%u",input_style);
 char *argv[]={APP_CONFIG_STORE,"settings-save","3",hash,a,sleep,fps,language,style,NULL};
 result=gkd_app_job_start_transaction(&s->settings_job,argv,now_ms(),fd);error=errno;close(fd);errno=error;
 if(!result){
  s->settings_osd_renew=0;
  s->settings_save_status=1;strcpy(s->settings_receipt,"GKD_APP_SETTINGS=SAVING\n");
  /* Keep the menu/input owner alive through the non-cancellable commit. */
  s->job.deadline=0;
 }
 return result;
}
static int menu_start(struct service *s)
{
 char initial[16],language[16],animation[16],sleep[16],fps[16],style[16];
 int settings=s->menu_kind==MENU_SETTINGS;
 snprintf(initial,sizeof(initial),"%u",s->menu_kind==MENU_UPDATE?2U:s->menu_kind==MENU_POWER?s->settings.power_default:s->settings.usb_default);
 snprintf(language,sizeof(language),"%u",s->settings.chinese);
 snprintf(animation,sizeof(animation),"%u",s->settings.effects);
 snprintf(sleep,sizeof(sleep),"%u",s->settings.auto_suspend_seconds/60U);
 snprintf(fps,sizeof(fps),"%u",s->settings.show_fps);
 snprintf(style,sizeof(style),"%u",s->settings.input_style);
 char *normal[]={APP_MENU,APP_FONT,(char *)menu_name(s->menu_kind),s->settings.effects?"enabled":"disabled",
  "0",s->settings.keys[0],s->settings.keys[1],s->settings.keys[2],s->settings.keys[3],initial,language,APP_CN_FONT,APP_CN_COMPACT_FONT,style,s->menu_usb_required?"1":"0",s->menu_kind==MENU_UPDATE?s->update_from:NULL,s->menu_kind==MENU_UPDATE?s->update_to:NULL,NULL};
 char *editor[]={APP_MENU,APP_FONT,"SETTINGS",s->settings.effects?"enabled":"disabled","0",
  s->settings.keys[0],s->settings.keys[1],s->settings.keys[2],s->settings.keys[3],s->settings.left_key,s->settings.right_key,
  animation,sleep,fps,language,APP_CN_FONT,APP_CN_COMPACT_FONT,s->settings_generation,APP_SOCKET,style,NULL};
 s->menu_released=0;s->menu_ready=0;s->leave_menu=0;
 if(settings&&gkd_app_idle_lease_live(&s->settings_lease))return -1;
 int result=job_start(s,settings?editor:normal,5000U,JOB_MENU);
 if(result&&settings)gkd_app_idle_lease_release(&s->settings_lease);
 return result;
}
static int sync_application(struct service *s)
{
 static const char *const mounts[]={"data","sdcard"};
 for(unsigned i=0;i<2U;i++){
  char path[128];int fd,result,saved;
  snprintf(path,sizeof(path),"/proc/%d/root/media/%s",s->session.ready.init,mounts[i]);
  fd=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
  if(fd<0)return -1;
  result=syncfs(fd);saved=errno;close(fd);errno=saved;if(result)return -1;
 }
 return 0;
}
static int no_lun(void)
{
 char value[2];int fd=open(APP_LUN,O_RDONLY|O_CLOEXEC);
 if(fd<0){
  if(errno==ENOENT){
   struct stat st;
   if(lstat("/sys/kernel/config/usb_gadget/gkd_recovery/functions/mass_storage.0",&st)<0&&errno==ENOENT)return 0;
  }
  return -1;
 }
 ssize_t n=read(fd,value,sizeof(value));int saved=errno;close(fd);
 if(n<0){errno=saved;return -1;}
 if(n&&!(n==1&&value[0]=='\n')){errno=EBUSY;return -1;}
 return 0;
}
static int suspend_system(void)
{
 int fd=open("/sys/power/state",O_WRONLY|O_CLOEXEC),rc,saved;
 if(fd<0)return -1;
 rc=write(fd,"mem\n",4U)==4?0:-1;saved=errno;close(fd);errno=saved;return rc;
}
/* Shared accepted R/A PMIC cutoff leaf, linked from gkd-r-poweroff.c. */
int gkd_r_poweroff(void);
static void start_application(struct service *s)
{
 if(gkd_app_settings_load(APP_CONFIG,&s->settings)||gkd_ui_catalog_load(&s->texts,APP_CONFIG)){finish(s,errno);return;}
 s->reported_exit=0;s->session_generation=s->request.generation;
 if(notice_start_prepare(s)||gkd_app_session_start(&s->session,s->settings.frontend_timeout,now_ms()))finish(s,errno);
 else finish(s,0);
}
static int game_card_generation(uint64_t *generation)
{
 char bytes[32],*end;int fd=open(APP_GAME_DISKSEQ,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0){if(errno==ENOENT){*generation=0;return 0;}return -1;}
 ssize_t count=read(fd,bytes,sizeof(bytes)-1U);int saved=errno;close(fd);
 if(count<2||count>21){errno=count<0?saved:EPROTO;return -1;}
 bytes[count]=0;errno=0;unsigned long long value=strtoull(bytes,&end,10);
 if(errno||bytes[0]<'1'||bytes[0]>'9'||strcmp(end,"\n")){errno=EPROTO;return -1;}
 *generation=(uint64_t)value;return 0;
}
static void game_card_poll(struct service *s,uint64_t now)
{
 if(now<s->card_scan_after)return;
 s->card_scan_after=now+200U;
 uint64_t generation;if(game_card_generation(&generation))return;
 if(!s->card_initialized){s->card_initialized=1;s->card_generation=s->card_observed=generation;return;}
 if(generation!=s->card_observed){s->card_observed=generation;s->card_changed=now;return;}
 if(generation==s->card_generation||now-s->card_changed<200U)return;
 if(s->stopping||s->terminal_error||s->trial_checked!=2||s->purpose!=JOB_NONE||s->update_status>0||s->update_lease_owned||
    s->settings_save_status==1||s->request.action!=GKD_LIFECYCLE_ACTION_NONE||no_lun())return;
 if(!gkd_app_lifecycle_event(&s->lifecycle,GKD_LIFECYCLE_EVENT_CARD_REFRESH,0)){
  s->card_generation=generation;s->card_refreshing=1;
  fprintf(stderr,"GKD_GAME_CARD=REFRESH generation=%llu\n",(unsigned long long)generation);
 }
}
static void update_release(struct service *s);
static void operation(struct service *s)
{
 int result=0;enum gkd_app_lifecycle_action action=s->request.action;
 if(action==GKD_LIFECYCLE_ACTION_NONE||s->operation_started)return;
 if(s->settings_save_status==1)return;
 fps_preempt(s);
 unsigned waiting=gkd_app_lifecycle_wait_kind(&s->lifecycle);
 /* Publish before unmount/export work and keep this same page through READY. */
 if(waiting){
  if(status_screen(s,now_ms())){finish(s,errno);return;}
  if(s->status_kind!=7U+waiting||s->status_hiding)return;
 }
 if(s->status_kind&&!(s->card_refreshing&&status_is_card(s->status_kind))&&!waiting){
  int hidden=hide_status(s,now_ms());if(hidden<0){finish(s,errno);return;}if(hidden)return;
 }
 if(action!=GKD_LIFECYCLE_ACTION_MENU_ACQUIRE&&action!=GKD_LIFECYCLE_ACTION_MENU_RELEASE){
  s->network_desired=0;network_poll(s,now_ms());
  if(s->network_error){finish(s,s->network_error);return;}
  if(s->network_running||s->network_dirty)return;
 }
 s->operation_started=1;
 switch(action){
 case GKD_LIFECYCLE_ACTION_MENU_ACQUIRE:
  if(s->events.exclusive&&events_mode(s,0)){finish(s,errno);return;}
  if(menu_start(s))finish(s,errno);
  return;
 case GKD_LIFECYCLE_ACTION_MENU_RELEASE:
  if(!s->menu_released){s->leave_menu=1;if(s->job.pid>0&&(gkd_app_job_cancel(&s->job,now_ms())))finish(s,errno);return;}
  break;
 case GKD_LIFECYCLE_ACTION_USB_CHARGE:result=command(s,APP_USB,"network");if(!result)return;break;
 case GKD_LIFECYCLE_ACTION_DISPLAY_FREEZE:
  result=events_mode(s,1);if(!result)result=freeze(s);break;
 case GKD_LIFECYCLE_ACTION_DISPLAY_THAW:
  result=ioctl(s->fb,GKD_UI_FREEZE_CLEAR);if(!result){s->freeze_renew=0;s->freeze_sequence=0;s->card_refreshing=0;}break;
 case GKD_LIFECYCLE_ACTION_APP_PAUSE:
  result=events_mode(s,1);
  if(!result)result=gkd_app_media_pause(&s->media,s->session.ready.init,s->session.ready.host,1000U);
  break;
 case GKD_LIFECYCLE_ACTION_GAME_UNMOUNT:result=gkd_app_media_game_unmount(&s->media);break;
 case GKD_LIFECYCLE_ACTION_GAME_PROBE:{
  uint64_t token=s->request.token;
  result=gkd_app_media_probe(&s->media);
  if(result){finish(s,errno);return;}
  s->request.action=GKD_LIFECYCLE_ACTION_NONE;s->operation_started=0;
  if(gkd_app_lifecycle_probe_complete(&s->lifecycle,token,s->media.mounted?
     GKD_LIFECYCLE_MEDIA_MOUNTED:GKD_LIFECYCLE_MEDIA_UNMOUNTED))s->terminal_error=errno;
  return;
 }
 case GKD_LIFECYCLE_ACTION_GAME_MOUNT:{
  uint64_t generation;
  result=game_card_generation(&generation);
  if(!result&&s->card_initialized&&generation!=s->card_generation){errno=ESTALE;result=-1;}
  if(!result)result=gkd_app_media_game_mount(&s->media);
  break;
 }
 case GKD_LIFECYCLE_ACTION_APP_RESUME:
  result=gkd_app_media_resume(&s->media);
  if(!result){gkd_app_media_close(&s->media);result=events_mode(s,0);}break;
 case GKD_LIFECYCLE_ACTION_APP_STOP:
   if(s->session.pid<=0&&(s->session.state==GKD_SESSION_STOPPED||s->session.state==GKD_SESSION_FAILED)){
    result=gkd_app_profile_cleanup(s->session.ready.host);break;
   }
  result=gkd_app_session_stop(&s->session,now_ms());if(!result)return;break;
 case GKD_LIFECYCLE_ACTION_P2_RELEASE:
  if(s->card_refreshing&&gkd_app_media_release_dead(&s->media)){finish(s,errno);return;}
  /* fall through */
 case GKD_LIFECYCLE_ACTION_LOOPS_RELEASE:
  result=command(s,APP_GUARD,"mmcblk0");if(!result)return;break;
 case GKD_LIFECYCLE_ACTION_USB_EXPORT_GAME:result=command(s,APP_USB,"storage");if(!result)return;break;
 case GKD_LIFECYCLE_ACTION_USB_EXPORT_SYSTEM:result=command(s,APP_USB,"debug");if(!result)return;break;
 case GKD_LIFECYCLE_ACTION_USB_CLEAR_LUN:result=command(s,APP_USB,"eject");if(!result)return;break;
 case GKD_LIFECYCLE_ACTION_USB_FLUSH:
  /* The successful eject job proves forced-eject, empty LUN and block flush;
   * the next step independently verifies there is still no exported LUN. */
  result=no_lun();break;
 case GKD_LIFECYCLE_ACTION_SYSTEM_MEDIA_VALIDATE:result=gkd_app_identity_verify(&s->identity);break;
 case GKD_LIFECYCLE_ACTION_APP_START:
  if(gkd_app_session_close(&s->session)){result=-1;break;}
  result=command(s,APP_CONFIG_STORE,"offline-load");
  if(!result){s->purpose=JOB_CONFIG_LOAD;return;}break;
 case GKD_LIFECYCLE_ACTION_POWER_QUIESCE:
  result=s->events.exclusive||s->power_guard.lease_fd>=0?0:gkd_menu_guard_owner_enter(&s->power_guard);
  if(result)break;
  /* The update lease pins the application mount namespace. Drop it once
   * input is owned, before worker teardown tries to detach its loops. */
  if(s->power_event!=GKD_LIFECYCLE_EVENT_SUSPEND)update_release(s);
  if(s->power_event==GKD_LIFECYCLE_EVENT_SUSPEND)
   result=gkd_app_media_pause(&s->media,s->session.ready.init,s->session.ready.host,1000U);
  else if(s->session.pid<=0&&(s->session.state==GKD_SESSION_STOPPED||s->session.state==GKD_SESSION_FAILED))
    result=gkd_app_profile_cleanup(s->session.ready.host);
   else {result=gkd_app_session_stop(&s->session,now_ms());if(!result)return;}
  break;
 case GKD_LIFECYCLE_ACTION_POWER_CARD_RELEASE:
  if(s->power_event==GKD_LIFECYCLE_EVENT_SUSPEND)result=sync_application(s);
  else {result=command(s,APP_GUARD,"mmcblk0");if(!result)return;}
  break;
 case GKD_LIFECYCLE_ACTION_POWER_SUSPEND:result=suspend_system();break;
 case GKD_LIFECYCLE_ACTION_POWER_RESUME:
  result=gkd_app_media_resume(&s->media);
  if(!result){gkd_app_media_close(&s->media);result=events_mode(s,0);
   if(!result)gkd_menu_guard_owner_exit(&s->power_guard);}break;
 case GKD_LIFECYCLE_ACTION_POWER_REBOOT:
  sync();result=reboot(RB_AUTOBOOT);if(!result){errno=EIO;result=-1;}break;
 case GKD_LIFECYCLE_ACTION_POWER_SHUTDOWN:result=gkd_r_poweroff();break;
 default:errno=EPROTO;result=-1;break;
 }
 finish(s,result?(errno?errno:EIO):0);
}
static int menu_result(struct service *s,const char *output)
{
 char ready[64],selected[96],cancelled[96],timed[96];
 const char *kind=menu_name(s->menu_kind);unsigned choice;
 snprintf(ready,sizeof(ready),"GKD_MENU_READY kind=%s\n",kind);
 if(strncmp(output,ready,strlen(ready))){errno=EPROTO;return -1;}
 output+=strlen(ready);
 if(s->menu_kind==MENU_SETTINGS){
  if(!strcmp(output,"GKD_MENU_RESULT=CANCELLED kind=SETTINGS\n")||
     !strcmp(output,"GKD_MENU_RESULT=TIMED_OUT kind=SETTINGS\n"))return GKD_LIFECYCLE_EVENT_CANCEL;
  if(s->settings_save_status==2&&!strcmp(output,"GKD_MENU_RESULT=SAVED kind=SETTINGS\n"))return GKD_LIFECYCLE_EVENT_CANCEL;
  if(s->settings_save_status==2&&!strcmp(output,"GKD_MENU_RESULT=UPDATE kind=SETTINGS\n")){
   s->update_from_settings=1;s->update_entry_deadline=now_ms()+5000U;
   s->update_check_after=now_ms();s->update_status=1;s->update_error=0;
   return GKD_LIFECYCLE_EVENT_CANCEL;
  }
  errno=EPROTO;return -1;
 }
 for(choice=0;choice<3U;choice++){
  snprintf(selected,sizeof(selected),"GKD_MENU_RESULT=SELECTED kind=%s index=%u\n",kind,choice);
  if(!strcmp(output,selected))break;
 }
 if(choice==3U){
  snprintf(cancelled,sizeof(cancelled),"GKD_MENU_RESULT=CANCELLED kind=%s\n",kind);
  snprintf(timed,sizeof(timed),"GKD_MENU_RESULT=TIMED_OUT kind=%s\n",kind);
  /* Legacy timeout receipts are cancellation, never action confirmation. */
  if(!strcmp(output,cancelled)||!strcmp(output,timed))return GKD_LIFECYCLE_EVENT_CANCEL;
  errno=EPROTO;return -1;
 }
 if(s->menu_kind==MENU_UPDATE){
  if(choice!=2U){errno=EPROTO;return -1;}
  s->update_requested=1;s->update_status=1;
  return GKD_LIFECYCLE_EVENT_CANCEL;
 }
 if(s->menu_kind){
  static const enum gkd_app_lifecycle_event events[]={GKD_LIFECYCLE_EVENT_SUSPEND,GKD_LIFECYCLE_EVENT_REBOOT,GKD_LIFECYCLE_EVENT_SHUTDOWN};
  s->power_event=events[choice];return events[choice];
 }
 static const enum gkd_app_lifecycle_event events[]={GKD_LIFECYCLE_EVENT_CHARGE,GKD_LIFECYCLE_EVENT_STORAGE,GKD_LIFECYCLE_EVENT_DEBUG};
 return events[choice];
}
static int dispatch_command(struct service *s,const char *command_text);
static int game_job(struct service *s,int exit_game)
{
 char host[24],init[24];
 snprintf(host,sizeof(host),"%d",s->session.ready.host);
 snprintf(init,sizeof(init),"%d",s->session.ready.init);
 char *argv[]={APP_GAME,exit_game==2?"menu":exit_game?"exit":"check",host,init,NULL};
 return job_start(s,argv,5000U,exit_game==2?JOB_GAME_MENU:exit_game?JOB_GAME_EXIT:JOB_GAME_CHECK);
}
static int usb_online(void);
static int current_runtime(char id[65])
{
 char value[66];struct stat st;int fd=open(APP_RUNTIME_ID,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0)return -1;
 ssize_t n=read(fd,value,sizeof(value));int valid=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&!st.st_uid&&!(st.st_mode&0022);
 int error=errno;close(fd);errno=error;
 if(!valid||n!=65||value[64]!='\n'){errno=EPROTO;return -1;}
 for(unsigned i=0;i<64U;i++)if(!((value[i]>='0'&&value[i]<='9')||(value[i]>='a'&&value[i]<='f'))){errno=EPROTO;return -1;}
 memcpy(id,value,64U);id[64]=0;return 0;
}
static void trial_health(struct service *s)
{
 struct stat st;
 if(s->stopping||s->trial_checked||s->purpose!=JOB_NONE||s->lifecycle.state!=GKD_LIFECYCLE_ACTIVE)return;
 if(lstat(APP_TRIAL_MARKER,&st)==0){
  if(!S_ISREG(st.st_mode)||st.st_uid||st.st_nlink!=1U||(st.st_mode&0777U)!=0600U){
   s->terminal_error=EPERM;return;
  }
  s->trial_pending=1;
 }else if(errno!=ENOENT){s->terminal_error=errno;return;}
 char *argv[]={APP_MANAGEMENT,NULL};
 if(job_start(s,argv,5000U,JOB_MANAGEMENT)){s->terminal_error=errno;return;}
 s->trial_checked=1;
}
static void update_release(struct service *s)
{
 if(s->update_lease_owned)gkd_app_idle_lease_release(&s->update_lease);
 s->update_lease_owned=0;
}
static int update_inspect(struct service *s)
{
 char runtime[65];int fd;
 if(no_lun()||current_runtime(runtime))return -1;
 enum gkd_app_idle_result idle=gkd_app_idle_lease_acquire(&s->update_lease,s->session.ready.host,s->session.ready.init);
 if(idle!=GKD_APP_IDLE_ACQUIRED){if(idle==GKD_APP_IDLE_BUSY)errno=EBUSY;return -1;}
 s->update_lease_owned=1;
 fd=application_directory(s,"/media/sdcard","/dev/mmcblk1p1");
 if(fd<0){update_release(s);return -1;}
 char *argv[]={APP_PREPARE,"/proc/self/fd/3/gkd-update/system.gkdupdate",
  "/etc/gkd-mini/update-public.pem","/dev/mmcblk0",runtime,"/dev/mmcblk0p3","--inspect",NULL};
 int result=gkd_app_job_start_fd(&s->job,argv,600000U,now_ms(),fd),saved=errno;
 close(fd);
 if(result)update_release(s);else s->purpose=JOB_UPDATE_INSPECT;
 errno=saved;return result;
}
static int inspect_receipt(struct service *s,const char *text)
{
 char hash[65],from[32],to[32],extra,exact[192];
 if(sscanf(text,"GKDSU_INSPECT=PASS package=%64[0-9a-f] from=%31[a-zA-Z0-9._-] to=%31[a-zA-Z0-9._-] %c",hash,from,to,&extra)!=3||!settings_hash(hash))return -1;
 snprintf(exact,sizeof(exact),"GKDSU_INSPECT=PASS package=%s from=%s to=%s\n",hash,from,to);
 if(strcmp(text,exact))return -1;
 memcpy(s->update_package,hash,sizeof(hash));snprintf(s->update_from,sizeof(s->update_from),"%s",from);snprintf(s->update_to,sizeof(s->update_to),"%s",to);
 return 0;
}
static int prepare_receipt(const char *output)
{
 unsigned long long bytes,p1,kernel;char exact[192];int used=0;
 if(sscanf(output,"GKDSU_PREPARE=PASS package_bytes=%llu p1_offset=%llu kernel_offset=%llu\n%n",
   &bytes,&p1,&kernel,&used)!=3||!used||output[used]||!p1||p1>=kernel||kernel>=bytes)return 0;
 snprintf(exact,sizeof(exact),"GKDSU_PREPARE=PASS package_bytes=%llu p1_offset=%llu kernel_offset=%llu\n",bytes,p1,kernel);
 return !strcmp(exact,output);
}
static int good_receipt(const char *output)
{
 char runtime[65],exact[160];
 if(current_runtime(runtime))return 0;
 snprintf(exact,sizeof(exact),"GKD_APPLICATION_UPDATE=MARK_GOOD outcome=complete target=%s\n",runtime);
 return !strcmp(exact,output);
}
static void update_reboot(struct service *s)
{
 if(s->update_status!=4||s->stopping||s->purpose!=JOB_NONE||
    s->request.action!=GKD_LIFECYCLE_ACTION_NONE||s->lifecycle.state!=GKD_LIFECYCLE_ACTIVE)return;
 s->power_event=GKD_LIFECYCLE_EVENT_REBOOT;
 if(gkd_app_lifecycle_event(&s->lifecycle,GKD_LIFECYCLE_EVENT_REBOOT,0)){
  s->update_error=errno;fprintf(stderr,"GKD_APP_UPDATE=NEEDS_REBOOT errno=%d\n",s->update_error);
 }else{s->update_status=3;s->update_error=0;}
}
static void update_prepare(struct service *s)
{
 int fd=-1,error=0;char runtime[65];struct gkd_app_battery reading;
 if(s->stopping)return;
 update_reboot(s);
 if(s->update_from_settings&&!s->update_entry_deadline&&!s->update_lease_owned&&s->update_status<=0&&
    s->purpose==JOB_NONE&&s->request.action==GKD_LIFECYCLE_ACTION_NONE&&s->lifecycle.state==GKD_LIFECYCLE_ACTIVE){
  s->update_from_settings=0;
  if(dispatch_command(s,"settings-menu"))fprintf(stderr,"GKD_APP_SETTINGS=RETURN_FAILED errno=%d\n",errno);
  return;
 }
 if(s->update_entry_deadline&&s->purpose==JOB_NONE&&s->lifecycle.state==GKD_LIFECYCLE_ACTIVE&&s->request.action==GKD_LIFECYCLE_ACTION_NONE&&now_ms()>=s->update_check_after){
  if(game_job(s,0)){s->update_entry_deadline=0;s->update_status=-1;s->update_error=errno;}
  else s->purpose=JOB_UPDATE_CHECK;
  return;
 }
 if(!s->update_requested||s->purpose!=JOB_NONE||s->lifecycle.state!=GKD_LIFECYCLE_ACTIVE||s->request.action!=GKD_LIFECYCLE_ACTION_NONE)return;
 if(!settings_hash(s->update_package)||!s->update_lease_owned||gkd_app_idle_lease_live(&s->update_lease)){error=ESTALE;goto fail;}
 s->network_desired=0;network_poll(s,now_ms());
 if(s->network_error){error=s->network_error;goto fail;}
 if(s->network_running||s->network_dirty)return;
 if(no_lun()||current_runtime(runtime)){error=errno;goto fail;}
 if(usb_online()!=1&&(gkd_app_battery_read(&reading,s->settings.battery_curve)||reading.percent<30)){
  error=EAGAIN;goto fail;
 }
 if(mkdir(APP_UPDATE_LOCK,0700)){error=errno;goto fail;}
 s->update_lock=1;
 fd=application_directory(s,"/media/sdcard","/dev/mmcblk1p1");if(fd<0){error=errno;goto fail;}
 char *argv[]={APP_PREPARE,"/proc/self/fd/3/gkd-update/system.gkdupdate",
  "/etc/gkd-mini/update-public.pem","/dev/mmcblk0",runtime,"/dev/mmcblk0p3","--confirmed",s->update_package,NULL};
 if(gkd_app_job_start_transaction(&s->job,argv,now_ms(),fd)){error=errno;goto fail;}
 close(fd);s->purpose=JOB_UPDATE_PREPARE;s->update_requested=0;s->update_status=2;return;
fail:
 if(fd>=0)close(fd);
 if(s->update_lock){(void)rmdir(APP_UPDATE_LOCK);s->update_lock=0;}
 update_release(s);s->update_requested=0;s->update_status=-1;s->update_error=error?error:EIO;
 fprintf(stderr,"GKD_APP_UPDATE=FAILED errno=%d\n",s->update_error);
}
static void jobs(struct service *s)
{
 settings_poll(s);
 enum gkd_app_job_state state;
 if(s->purpose==JOB_NONE)return;
 state=gkd_app_job_poll(&s->job,now_ms());
 if(s->purpose==JOB_MENU&&!s->menu_ready){
  char ready[64];snprintf(ready,sizeof(ready),"GKD_MENU_READY kind=%s\n",menu_name(s->menu_kind));
  if(state==GKD_JOB_RUNNING&&!s->job.error&&!strncmp(s->job.output,ready,strlen(ready))){
   /* Startup is bounded; a ready interactive menu has no lifetime deadline.
    * Cancellation and child-failure cleanup retain their own bounded paths. */
   s->job.deadline=0;
   s->menu_ready=1;if(s->request.action==GKD_LIFECYCLE_ACTION_MENU_ACQUIRE)finish(s,0);
  }
 }
 if(s->job.pid>0){
  static const char captured[]="GKD_SCREENSHOT_CAPTURED\n";
  int shot=s->purpose==JOB_SCREENSHOT&&!strncmp(s->job.output,captured,sizeof(captured)-1U);
  int waiting=s->purpose==JOB_GAME_CHECK||s->purpose==JOB_GAME_EXIT||s->purpose==JOB_GAME_MENU_CHECK||
   s->purpose==JOB_GAME_MENU||s->purpose==JOB_CONFIG_SAVE||s->purpose==JOB_MANAGEMENT||s->purpose==JOB_UPDATE_GOOD;
  if(shot||waiting)s->job_osd_renew=1;
  return;
 }
 if(s->job_osd_renew){
  s->job_osd_renew=0;
  if(s->status_kind==12U&&hide_status(s,now_ms()))s->terminal_error=errno;
 }
 if(s->purpose==JOB_MENU){
  int notify_saved=0;
  int event=-1,error=s->job.error,clean=WIFEXITED(s->job.status)&&WEXITSTATUS(s->job.status)==0;
  if(clean&&(state==GKD_JOB_DONE||(error==ECANCELED&&s->leave_menu))){
   event=menu_result(s,s->job.output);
   if(event>=0){
    s->menu_released=1;
    notify_saved=s->menu_kind==MENU_SETTINGS&&s->settings_save_status==2&&
      !strcmp(s->job.output,"GKD_MENU_READY kind=SETTINGS\nGKD_MENU_RESULT=SAVED kind=SETTINGS\n");
   }
   else error=errno;
  }
  if(!s->menu_released){
   /* Kernel/menu leases expire on owner death; that is not clean action approval. */
   if(s->request.action!=GKD_LIFECYCLE_ACTION_NONE)finish(s,error?error:EPROTO);
   else if(!s->terminal_error)s->terminal_error=error?error:EPROTO;
  }
  gkd_app_job_close(&s->job);s->purpose=JOB_NONE;
  if(s->menu_kind==MENU_SETTINGS&&s->settings_save_status!=1)gkd_app_idle_lease_release(&s->settings_lease);
  if(s->menu_released){
   if(s->request.action==GKD_LIFECYCLE_ACTION_MENU_RELEASE)finish(s,0);
   else if(s->lifecycle.state==GKD_LIFECYCLE_MENU&&
      gkd_app_lifecycle_event(&s->lifecycle,s->leave_menu?GKD_LIFECYCLE_EVENT_CANCEL:(enum gkd_app_lifecycle_event)event,0))
    s->terminal_error=errno;
  }
  if(s->menu_kind==MENU_UPDATE&&(!s->update_requested||s->leave_menu||!s->menu_released)){
   s->update_requested=0;s->update_status=0;update_release(s);
  }
  if(notify_saved&&!s->terminal_error&&!s->stopping)
   (void)result_osd(s,1,SETTINGS_OSD_MS);
 }else if(s->purpose==JOB_UPDATE_CHECK){
  int active=state==GKD_JOB_DONE&&!strcmp(s->job.output,"ACTIVE\n");
  int idle=state==GKD_JOB_DONE&&!strcmp(s->job.output,"IDLE\n");
  s->update_error=idle?0:s->job.error?s->job.error:EBUSY;
  gkd_app_job_close(&s->job);s->purpose=JOB_NONE;
  if(s->stopping){s->update_entry_deadline=0;s->update_requested=0;s->update_status=0;}
  else if(idle){
   s->update_entry_deadline=0;
   if(update_inspect(s)){s->update_status=-1;s->update_error=errno;}
  }
  else if(s->update_entry_deadline&&now_ms()<s->update_entry_deadline&&active){
   s->update_check_after=now_ms()+100U;s->update_status=1;
  }else{s->update_entry_deadline=0;s->update_status=-1;fprintf(stderr,"GKD_APP_UPDATE=FAILED errno=%d\n",s->update_error);}
 }else if(s->purpose==JOB_UPDATE_INSPECT){
  int error=state==GKD_JOB_DONE&&!inspect_receipt(s,s->job.output)?0:s->job.error?s->job.error:EPROTO;
  gkd_app_job_close(&s->job);s->purpose=JOB_NONE;
  if(!error&&!s->stopping){
   s->menu_kind=MENU_UPDATE;s->menu_usb_required=0;s->update_status=0;
   if(gkd_app_lifecycle_event(&s->lifecycle,GKD_LIFECYCLE_EVENT_MENU,0))error=errno;
  }
  if(error||s->stopping){
   update_release(s);s->update_status=-1;s->update_error=error?error:ECANCELED;
   if(!s->stopping)(void)result_osd(s,0,SETTINGS_OSD_MS);
  }
 }else if(s->purpose==JOB_UPDATE_PREPARE){
  s->update_error=state==GKD_JOB_DONE&&prepare_receipt(s->job.output)?0:s->job.error?s->job.error:EPROTO;
  if(s->job.output[0])fprintf(stderr,"%s",s->job.output);
  gkd_app_job_close(&s->job);s->purpose=JOB_NONE;
  if(s->update_lock){
   if(rmdir(APP_UPDATE_LOCK)&&!s->update_error)s->update_error=errno;
   s->update_lock=0;
  }
  if(!s->update_error){
   s->update_status=4;update_reboot(s);
   if(s->update_status==4)fprintf(stderr,"GKD_APP_UPDATE=NEEDS_REBOOT errno=%d\n",s->lifecycle.last_error);
  }else{update_release(s);s->update_status=-1;fprintf(stderr,"GKD_APP_UPDATE=FAILED errno=%d\n",s->update_error);}
 }else if(s->purpose==JOB_MANAGEMENT){
  int error=state==GKD_JOB_DONE&&!strcmp(s->job.output,"GKD_APP_MANAGEMENT=READY\n")?0:
   s->job.error?s->job.error:EPROTO;
  gkd_app_job_close(&s->job);s->purpose=JOB_NONE;
  if(error)s->terminal_error=error;
  else if(!s->trial_pending)s->trial_checked=2;
  else if(no_lun())s->terminal_error=errno;
  else{
   char *argv[]={APP_UPDATE,"mark-good",NULL};
   if(gkd_app_job_start_transaction(&s->job,argv,now_ms(),-1))s->terminal_error=errno;
   else s->purpose=JOB_UPDATE_GOOD;
  }
 }else if(s->purpose==JOB_UPDATE_GOOD){
  int error=state==GKD_JOB_DONE&&good_receipt(s->job.output)?0:s->job.error?s->job.error:EPROTO;
  if(s->job.output[0])fprintf(stderr,"%s",s->job.output);
  gkd_app_job_close(&s->job);s->purpose=JOB_NONE;
  if(error)s->terminal_error=error;
  else if(gkd_app_identity_read(&s->identity))s->terminal_error=errno;
  else{s->trial_checked=2;s->trial_pending=0;(void)result_osd(s,1,2000U);}
 }else if(s->purpose==JOB_GAME_MENU_CHECK||s->purpose==JOB_GAME_MENU){
  enum job_purpose purpose=s->purpose;
  int active=state==GKD_JOB_DONE&&!strcmp(s->job.output,"ACTIVE\n");
  int idle=state==GKD_JOB_DONE&&!strcmp(s->job.output,"IDLE\n");
  int delivered=state==GKD_JOB_DONE&&!strcmp(s->job.output,"MENU_DELIVERED\n");
  int error=s->job.error?s->job.error:EPROTO;
  gkd_app_job_close(&s->job);s->purpose=JOB_NONE;
  if(s->menu_power_pending){s->menu_power_pending=0;if(game_job(s,0))fprintf(stderr,"GKD_APP_GAME=FAILED errno=%d\n",errno);}
  else if(purpose==JOB_GAME_MENU_CHECK&&(active||idle)){
   if(active?game_job(s,2):dispatch_command(s,"settings-menu"))fprintf(stderr,"GKD_APP_MENU=FAILED errno=%d\n",errno);
  }else if(purpose==JOB_GAME_MENU&&delivered)fprintf(stderr,"GKD_APP_MENU=DELIVERED\n");
  else fprintf(stderr,"GKD_APP_MENU=FAILED errno=%d\n",error);
 }else if(s->purpose==JOB_GAME_CHECK||s->purpose==JOB_GAME_EXIT){
  enum job_purpose purpose=s->purpose;
  int active=state==GKD_JOB_DONE&&!strcmp(s->job.output,"ACTIVE\n");
  int idle=state==GKD_JOB_DONE&&!strcmp(s->job.output,"IDLE\n");
  int exited=state==GKD_JOB_DONE&&!strcmp(s->job.output,"EXITED\n");
  int error=s->job.error?s->job.error:EPROTO;
  gkd_app_job_close(&s->job);s->purpose=JOB_NONE;
  if(purpose==JOB_GAME_CHECK&&(active||idle)){
   if(active?game_job(s,1):dispatch_command(s,"power-menu"))
    fprintf(stderr,"GKD_APP_GAME=FAILED errno=%d\n",errno);
  }else if(purpose==JOB_GAME_EXIT&&exited){
   s->last_activity=now_ms();
   fprintf(stderr,"GKD_APP_GAME=EXITED same_frontend=1\n");
  }else fprintf(stderr,"GKD_APP_GAME=FAILED errno=%d\n",error);
 }else if(s->purpose==JOB_CONFIG_LOAD){
  int error=state==GKD_JOB_DONE?0:s->job.error?s->job.error:EIO;
  gkd_app_job_close(&s->job);s->purpose=JOB_NONE;
  if(error)finish(s,error);
  else {
   gkd_app_events_close(&s->events);s->events_live=0;
   if(command(s,APP_INPUT_INIT,"restart"))finish(s,errno);
   else s->purpose=JOB_INPUT_START;
  }
 }else if(s->purpose==JOB_INPUT_START){
  int error=state==GKD_JOB_DONE?0:s->job.error?s->job.error:EIO;
  gkd_app_job_close(&s->job);s->purpose=JOB_NONE;
  if(error)finish(s,error);else start_application(s);
 }else if(s->purpose==JOB_CONFIG_SAVE){
  s->config_save_error=state==GKD_JOB_DONE?0:s->job.error?s->job.error:EIO;
  s->config_save_status=s->config_save_error?-1:2;
  if(s->job.output[0])fprintf(stderr,"%s",s->job.output);
  gkd_app_job_close(&s->job);s->purpose=JOB_NONE;
 }else if(s->purpose==JOB_SCREENSHOT){
  static const char captured[]="GKD_SCREENSHOT_CAPTURED\n";
  static const char prefix[]="GKD_SCREENSHOT_RESULT=success filename=";
  const char *receipt=s->job.output;
  if(!strncmp(receipt,captured,sizeof(captured)-1U))receipt+=sizeof(captured)-1U;
  size_t n=strlen(receipt);
  int success=state==GKD_JOB_DONE&&n>sizeof(prefix)&&receipt[n-1U]=='\n'&&
   !strncmp(receipt,prefix,sizeof(prefix)-1U)&&
   !strncmp(receipt+sizeof(prefix)-1U,s->settings.screenshot_directory,strlen(s->settings.screenshot_directory))&&
   !strchr(receipt,'\r')&&strchr(receipt,'\n')==receipt+n-1U;
  gkd_app_job_close(&s->job);s->purpose=JOB_NONE;gkd_menu_guard_owner_exit(&s->power_guard);
  if(s->settings.screenshot_notify_ms)
   (void)osd(s,service_text(s,success?GKD_UI_TEXT_SHOT_SAVED:GKD_UI_TEXT_SHOT_FAILED),3U,-1,!success,s->settings.screenshot_notify_ms);
 }else{
  int error=state==GKD_JOB_DONE?0:s->job.error?s->job.error:EIO;
  gkd_app_job_close(&s->job);s->purpose=JOB_NONE;finish(s,error);
 }
}
static void session(struct service *s)
{
 enum gkd_app_session_state state=gkd_app_session_poll(&s->session,now_ms());
 if(s->stop_host_requested)return;
 if((state==GKD_SESSION_READY||state==GKD_SESSION_FAILED)&&notice_start_complete(s)){
  if(errno!=EWOULDBLOCK&&errno!=EAGAIN)s->terminal_error=errno;
  return;
 }
 if(!s->boot_ready){
  if(state==GKD_SESSION_READY){
   if(gkd_app_lifecycle_init(&s->lifecycle,1U,begin,s)){s->terminal_error=errno;return;}
   s->session_generation=1U;s->boot_ready=1;s->last_activity=now_ms();
   if(!s->events_live&&gkd_app_events_open(&s->events,&s->settings)){s->terminal_error=errno;return;}
   s->events_live=1;
   puts("GKD_APPLICATION=READY");fflush(stdout);
  }else if(s->session.pid<=0)s->terminal_error=s->session.error?s->session.error:ECHILD;
  return;
 }
 if(state==GKD_SESSION_STOPPED&&s->operation_started&&
    (s->request.action==GKD_LIFECYCLE_ACTION_APP_STOP||
     (s->request.action==GKD_LIFECYCLE_ACTION_POWER_QUIESCE&&s->power_event!=GKD_LIFECYCLE_EVENT_SUSPEND))){
  finish(s,gkd_app_profile_cleanup(s->session.ready.host)?errno:0);return;
 }
 if(state==GKD_SESSION_READY&&s->lifecycle.waiting_ready){
  if(!s->events_live){
   if(gkd_app_events_open(&s->events,&s->settings)){s->terminal_error=errno;return;}
   s->events_live=1;
  }
  if(gkd_app_lifecycle_event(&s->lifecycle,GKD_LIFECYCLE_EVENT_APP_READY,s->session_generation))s->terminal_error=errno;
 }
 if(state==GKD_SESSION_FAILED&&!s->reported_exit){
  s->reported_exit=1;
  if(s->request.action==GKD_LIFECYCLE_ACTION_APP_STOP||
     s->request.action==GKD_LIFECYCLE_ACTION_POWER_QUIESCE)finish(s,s->session.error?s->session.error:ECHILD);
  else if(gkd_app_lifecycle_event(&s->lifecycle,GKD_LIFECYCLE_EVENT_APP_EXIT,s->session_generation))s->terminal_error=errno;
 }
}
static int listen_control(void)
{
 struct sockaddr_un address={.sun_family=AF_UNIX};struct stat st;int fd;
 if(mkdir(APP_RUN,0700)&&errno!=EEXIST)return -1;
 if(lstat(APP_RUN,&st)||!S_ISDIR(st.st_mode)||st.st_uid||st.st_mode&0077){errno=EPERM;return -1;}
 fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)return -1;
 strcpy(address.sun_path,APP_SOCKET);
 /* The sole service lock must be held before stale socket removal. */
 if(unlink(APP_SOCKET)&&errno!=ENOENT){int e=errno;close(fd);errno=e;return -1;}
 if(bind(fd,(struct sockaddr *)&address,sizeof(address))||chmod(APP_SOCKET,0600)||listen(fd,4)){
  int e=errno;close(fd);errno=e;return -1;
 }
 return fd;
}
static int dispatch_command(struct service *s,const char *command_text)
{
 if(!s->boot_ready||s->terminal_error||s->trial_checked!=2){errno=EBUSY;return -1;}
 if(!strcmp(command_text,"osd-yield")){
  /* Controls never wait for this reply under their shared input guard. Clear
   * our transient immediately; their next existing-loop retry gets the tile. */
  if(s->lifecycle.state!=GKD_LIFECYCLE_ACTIVE){errno=EBUSY;return -1;}
  if(gkd_ui_plane_clear(s->fb)&&errno!=EBUSY)return -1;
  s->charge_until=0;s->button_osd_until=now_ms()+200U;
  s->fps_visible=0;s->fps_renew=s->fps_expires=0;
  if(s->status_kind==5U)reset_status(s);
  return 0;
 }

 if(s->settings_save_status==1||(s->update_lease_owned&&strcmp(command_text,"retry"))){errno=EBUSY;return -1;}
 if((s->stopping||s->update_status>=3)&&strcmp(command_text,"retry")){errno=EBUSY;return -1;}
 if(s->update_requested||s->update_entry_deadline||s->purpose==JOB_UPDATE_PREPARE||s->purpose==JOB_UPDATE_GOOD){errno=EBUSY;return -1;}
 if(!strcmp(command_text,"update-entry")){
  if(s->lifecycle.state!=GKD_LIFECYCLE_ACTIVE||s->purpose!=JOB_NONE){errno=EBUSY;return -1;}
  s->update_entry_deadline=now_ms()+5000U;s->update_check_after=now_ms()+250U;
  s->update_status=1;s->update_error=0;return 0;
 }
 if(!strcmp(command_text,"update")){
  if(s->lifecycle.state!=GKD_LIFECYCLE_ACTIVE||s->purpose!=JOB_NONE){errno=EBUSY;return -1;}
  if(game_job(s,0))return -1;
  s->purpose=JOB_UPDATE_CHECK;s->update_status=1;s->update_error=0;return 0;
 }
 if(!strcmp(command_text,"config-save")){
  if(s->lifecycle.state!=GKD_LIFECYCLE_ACTIVE||s->purpose!=JOB_NONE){errno=EBUSY;return -1;}
  int fd=application_directory(s,"/media/data/local/etc","/dev/mmcblk0p2");
  if(fd<0)return -1;
  char *argv[]={APP_CONFIG_STORE,"save","3",NULL};
  int result=gkd_app_job_start_fd(&s->job,argv,30000U,now_ms(),fd),error=errno;
  close(fd);errno=error;
  if(!result){s->purpose=JOB_CONFIG_SAVE;s->config_save_status=1;s->config_save_error=0;}
  return result;
 }
 if(!strcmp(command_text,"settings-menu")){
  if(s->lifecycle.state!=GKD_LIFECYCLE_ACTIVE||s->purpose!=JOB_NONE){errno=EBUSY;return -1;}
  enum gkd_app_idle_result idle=gkd_app_idle_lease_acquire(&s->settings_lease,s->session.ready.host,s->session.ready.init);
  if(idle!=GKD_APP_IDLE_ACQUIRED){if(idle==GKD_APP_IDLE_BUSY)errno=EBUSY;return -1;}
  if(settings_snapshot(s)){int error=errno;gkd_app_idle_lease_release(&s->settings_lease);errno=error;return -1;}
  s->menu_kind=MENU_SETTINGS;s->menu_usb_required=0;s->settings_save_status=0;s->settings_receipt[0]=0;
  if(gkd_app_lifecycle_event(&s->lifecycle,GKD_LIFECYCLE_EVENT_MENU,0)){
   int error=errno;gkd_app_idle_lease_release(&s->settings_lease);errno=error;return -1;
  }
  return 0;
 }
 if(!strcmp(command_text,"usb-menu")||!strcmp(command_text,"power-menu")){
  if(s->lifecycle.state!=GKD_LIFECYCLE_ACTIVE||s->purpose!=JOB_NONE){errno=EBUSY;return -1;}
  struct gkd_app_settings settings;
  if(gkd_app_settings_load(APP_CONFIG,&settings))return -1;
  s->settings=settings;s->menu_kind=!strcmp(command_text,"power-menu");s->menu_usb_required=0;
  return gkd_app_lifecycle_event(&s->lifecycle,GKD_LIFECYCLE_EVENT_MENU,0);
 }
 if(!strcmp(command_text,"return")||!strcmp(command_text,"detach")){
  if(s->purpose==JOB_MENU)s->leave_menu=1;
  return gkd_app_lifecycle_event(&s->lifecycle,!strcmp(command_text,"return")?
   GKD_LIFECYCLE_EVENT_RETURN:GKD_LIFECYCLE_EVENT_DETACH,0);
 }
 if(!strcmp(command_text,"retry")){
  if(s->stopping&&s->lifecycle.state==GKD_LIFECYCLE_ACTIVE&&s->purpose==JOB_NONE&&
     !s->network_running&&s->network_error){
   s->network_error=0;return 0;
  }
  return gkd_app_lifecycle_event(&s->lifecycle,GKD_LIFECYCLE_EVENT_RETRY,0);
 }
 if(!strcmp(command_text,"suspend")){
   if(s->lifecycle.state!=GKD_LIFECYCLE_ACTIVE||s->purpose!=JOB_NONE){errno=EBUSY;return -1;}
  s->power_event=GKD_LIFECYCLE_EVENT_SUSPEND;
  return gkd_app_lifecycle_event(&s->lifecycle,GKD_LIFECYCLE_EVENT_SUSPEND,0);
 }
 errno=EINVAL;return -1;
}
static int game_wait(struct service *s,const struct ucred *peer,int active)
{
 if(active<=0){
  if(s->game_wait_peer!=peer->pid){errno=EPERM;return -1;}
  close(s->game_wait_pin);s->game_wait_pin=-1;s->game_wait_peer=0;
  if(active<0)(void)result_osd(s,0,SETTINGS_OSD_MS);
  return 0;
 }
 if(s->game_wait_peer||s->lifecycle.state!=GKD_LIFECYCLE_ACTIVE||s->stopping||s->terminal_error||s->settings_save_status==1){errno=EBUSY;return -1;}
 struct gkd_app_fps_context context=fps_context(s);int pin;unsigned long long start;
 if(gkd_app_fps_authorize_launcher(peer,&context,&pin,&start))return -1;
 s->game_wait_peer=peer->pid;s->game_wait_pin=pin;
 if(status_screen(s,now_ms())){int error=errno;close(pin);s->game_wait_peer=0;s->game_wait_pin=-1;errno=error;return -1;}
 return 0;
}
static void control(struct service *s)
{
 int client=accept4(s->listener,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);
 if(client<0)return;
 struct ucred peer;socklen_t size=sizeof(peer);char input[192],reply[320];
 if(getsockopt(client,SOL_SOCKET,SO_PEERCRED,&peer,&size)||size!=sizeof(peer)||peer.uid){close(client);return;}
 /* One local seqpacket, bounded wait: accept may precede sender's first write. */
 struct pollfd p={client,POLLIN,0};
 if(poll(&p,1,50)<=0){close(client);return;}
 struct gkd_app_fps_context fps_state=fps_context(s);
 int fps_handled=gkd_app_fps_control(&s->fps,client,&peer,&fps_state,now_ms());
 if(fps_handled){close(client);return;}
 ssize_t n=recv(client,input,sizeof(input),MSG_TRUNC);
 if(n<=0||n>=(ssize_t)sizeof(input)||memchr(input,0,(size_t)n)){close(client);return;}
 input[n]=0;
 if(!strcmp(input,"game-wait-begin")||!strcmp(input,"game-wait-end")||!strcmp(input,"game-wait-fail")){
  int mode=!strcmp(input,"game-wait-begin")?1:!strcmp(input,"game-wait-fail")?-1:0;
  int rc=game_wait(s,&peer,mode),error=rc?errno:0;
  snprintf(reply,sizeof(reply),"GKD_APPLICATION_COMMAND=%s errno=%d\n",rc?"REJECTED":"ACCEPTED",error);
 }else if(!strncmp(input,"settings-save ",14)){
  if(settings_save(s,input,peer.pid)){
   snprintf(reply,sizeof(reply),"GKD_APP_SETTINGS=FAILED state=recoverable errno=%d\n",errno?errno:EIO);
  }else snprintf(reply,sizeof(reply),"%s",s->settings_receipt);
 }else if(!strcmp(input,"settings-status")){
  if(s->purpose!=JOB_MENU||s->menu_kind!=MENU_SETTINGS||peer.pid!=s->job.pid)
   snprintf(reply,sizeof(reply),"GKD_APP_SETTINGS=FAILED state=unknown errno=%d\n",EPERM);
  else if(!s->settings_receipt[0])
   snprintf(reply,sizeof(reply),"GKD_APP_SETTINGS=FAILED state=recoverable errno=%d\n",EAGAIN);
  else snprintf(reply,sizeof(reply),"%s",s->settings_receipt);
 }else if(!strcmp(input,"fps-status")){
  struct gkd_app_fps_view view=gkd_app_fps_read(&s->fps,&fps_state);
  snprintf(reply,sizeof(reply),"GKD_APPLICATION_FPS=%s visible=%d fps=%u\n",
   view.state==GKD_APP_FPS_NO_GAME?"NO_GAME":view.state==GKD_APP_FPS_AVAILABLE?"AVAILABLE":"UNAVAILABLE",s->fps_visible,view.fps);
 }else if(!strcmp(input,"update-status"))
  snprintf(reply,sizeof(reply),"GKD_APPLICATION_UPDATE=%s errno=%d\n",
   s->update_status==4?"NEEDS_REBOOT":s->update_status==3?"REBOOTING":s->update_status==2?"PREPARING":s->update_status==1?"CHECKING":s->update_status<0?"FAILED":"IDLE",s->update_error);
 else if(!strcmp(input,"config-status"))
  snprintf(reply,sizeof(reply),"GKD_APPLICATION_CONFIG=%s errno=%d\n",
   s->config_save_status==2?"SAVED":s->config_save_status==1?"SAVING":s->config_save_status<0?"FAILED":"IDLE",s->config_save_error);
 else if(!strcmp(input,"status"))
  snprintf(reply,sizeof(reply),"GKD_APPLICATION state=%u action=%u host=%d init=%d app=%d error=%d\n",
   (unsigned)s->lifecycle.state,(unsigned)s->request.action,s->session.ready.host,s->session.ready.init,
   s->session.ready.application,s->terminal_error?s->terminal_error:s->lifecycle.last_error);
 else{
  int rc=dispatch_command(s,input),error=rc?errno:0;
  snprintf(reply,sizeof(reply),"GKD_APPLICATION_COMMAND=%s errno=%d\n",rc?"REJECTED":"ACCEPTED",error);
 }
 (void)send(client,reply,strlen(reply),MSG_NOSIGNAL);close(client);
}
static int osd(struct service *s,const char *text,unsigned icon,int level,int critical,unsigned ttl)
{
 struct gkd_ui_osd message={text,icon,level,critical};
 fps_preempt(s);
 if(now_ms()<s->button_osd_until){errno=EBUSY;return -1;}
 /* A new transient supersedes a charge acknowledgement in the same slot. */
 s->charge_until=0;
 if(s->status_kind==5U)reset_status(s);
 if(!ttl)return 0;
 struct gkd_app_settings current;
 if(gkd_app_settings_load(APP_CONFIG,&current))return -1;
 s->settings.effects=current.effects;s->settings.chinese=current.chinese;
 if(s->osd_sequence==UINT32_MAX){errno=EOVERFLOW;return -1;}
 if(gkd_ui_export_osd_argb(s->osd_pixels,GKD_UI_OSD_WIDTH*GKD_UI_OSD_HEIGHT,&s->ui,service_font(s),&message))return -1;
 int result=gkd_ui_plane_send(s->fb,s->osd_pixels,ttl,s->settings.effects?160U:0U,++s->osd_sequence);
 if(!result)s->transient_until=now_ms()+ttl;
 return result;
}
static void battery(struct service *s,uint64_t now)
{
 struct gkd_app_battery reading;
 if(now<s->battery_next)return;
 s->battery_next=now+s->settings.battery_poll_ms;
 if(gkd_app_battery_read(&reading,s->settings.battery_curve)){
  s->battery_suspend_since=0;
  fprintf(stderr,"GKD_APP_BATTERY=UNAVAILABLE errno=%d\n",errno);return;
 }
 if(gkd_app_battery_led(&reading,s->settings.battery_leds))
  fprintf(stderr,"GKD_APP_BATTERY_LED=FAILED errno=%d\n",errno);
 if(reading.external_power||reading.percent>(int)(s->settings.battery_low+s->settings.battery_hysteresis))s->low_notified=0;
 if(reading.external_power||reading.percent>(int)(s->settings.battery_critical+s->settings.battery_hysteresis))s->critical_notified=0;
 /* Invalid samples, external power and non-active ownership restart the grace.
  * Never interrupt an update, menu, USB export or outstanding config commit. */
 if(!s->settings.battery_suspend_enabled||reading.external_power||
    reading.percent>(int)s->settings.battery_critical||
    s->lifecycle.state!=GKD_LIFECYCLE_ACTIVE||
    s->request.action!=GKD_LIFECYCLE_ACTION_NONE||s->purpose!=JOB_NONE||
    s->settings_save_status==1||s->update_requested||s->update_entry_deadline||
    s->update_status>0||s->game_wait_peer||s->trial_checked!=2||s->stopping||s->terminal_error)
  s->battery_suspend_since=0;
 else if(!s->battery_suspend_since)s->battery_suspend_since=now;
 if(reading.external_power||s->lifecycle.state!=GKD_LIFECYCLE_ACTIVE||
    s->request.action!=GKD_LIFECYCLE_ACTION_NONE||s->purpose!=JOB_NONE||
    (s->status_kind&&!status_is_card(s->status_kind)))return;
 if(reading.percent<=(int)s->settings.battery_critical&&!s->critical_notified){
  if(!osd(s,service_text(s,GKD_UI_TEXT_BAT_LOW),2U,reading.percent,1,s->settings.battery_notify_ms))s->critical_notified=s->low_notified=1;
 }else if(reading.percent<=(int)s->settings.battery_low&&!s->low_notified){
  if(!osd(s,service_text(s,GKD_UI_TEXT_BAT_LOW),2U,reading.percent,1,s->settings.battery_notify_ms))s->low_notified=1;
 }
 if(s->battery_suspend_since&&now>=s->battery_suspend_since&&
    now-s->battery_suspend_since>=s->settings.battery_suspend_delay_ms){
  if(!dispatch_command(s,"suspend")){
   s->battery_suspend_since=0;
   fprintf(stderr,"GKD_APP_BATTERY=SUSPEND percent=%d\n",reading.percent);
  }else fprintf(stderr,"GKD_APP_BATTERY=SUSPEND_DEFERRED errno=%d\n",errno);
 }
}
static int usb_online(void)
{
 char value[4];int fd=open(APP_USB_ONLINE,O_RDONLY|O_CLOEXEC);
 if(fd<0)return -1;
 ssize_t n=read(fd,value,sizeof(value));int saved=errno;close(fd);
 if(n<0){errno=saved;return -1;}
 if(n!=2||value[1]!='\n'||(value[0]!='0'&&value[0]!='1')){errno=EPROTO;return -1;}
 return value[0]-'0';
}
/* Queue debounced power edges separately from mode acknowledgement. A menu or
 * export teardown may own the planes; retain the latest edge until it can show. */
static void usb_power_notice(struct service *s)
{
 if(!s->usb_notice_pending||s->status_kind||now_ms()<s->transient_until||
    (s->lifecycle.state!=GKD_LIFECYCLE_ACTIVE&&s->lifecycle.state!=GKD_LIFECYCLE_MENU&&
     s->lifecycle.state!=GKD_LIFECYCLE_MENU_OPENING))return;
 struct gkd_ui_osd message;char text[24];
 power_osd(s,s->settings.battery_curve,&message,text,sizeof(text));
 if(!osd(s,message.text,message.icon,message.level,message.critical,s->settings.usb_notify_ms)){
  fprintf(stderr,"GKD_APP_USB_POWER_OSD=%s\n",s->usb_notice_pending==2?"CONNECTED":"DISCONNECTED");
  s->usb_notice_pending=0;
 }
}
static void usb_events(struct service *s,uint64_t now)
{
 int online;
 if(now<s->usb_next)return;
 s->usb_next=now+100U;online=usb_online();if(online<0)return;
 if(online!=s->usb_observed){s->usb_observed=online;s->usb_changed=now;return;}
 if(now-s->usb_changed<500U)return;
 if(online!=s->usb_stable){
  s->usb_stable=online;s->usb_notice_pending=online?2:1;s->last_activity=now;
  if(online)s->usb_pending=1;
  else{
   s->usb_pending=0;s->network_desired=0;s->charge_until=0;
   if(s->lifecycle.state!=GKD_LIFECYCLE_ACTIVE&&(dispatch_command(s,"detach"))&&errno!=EALREADY)
    fprintf(stderr,"GKD_APP_DETACH=DEFERRED errno=%d\n",errno);
  }
 }
 if(s->usb_pending&&s->lifecycle.state==GKD_LIFECYCLE_ACTIVE&&s->purpose==JOB_NONE){
  if(!dispatch_command(s,"usb-menu")){s->usb_pending=0;s->menu_usb_required=1;}
 }
 usb_power_notice(s);
}
static int screenshot(struct service *s)
{
 int rootfd=-1,result=-1,saved;
 fps_preempt(s);
 if(gkd_menu_guard_owner_enter(&s->power_guard))return -1;
 rootfd=application_directory(s,"/media/sdcard","/dev/mmcblk1p1");
 if(rootfd<0)goto done;
 char *argv[]={"/usr/sbin/gkd-screenshot","--application-capture","3",s->settings.screenshot_directory,NULL};
 if(gkd_app_job_start_fd(&s->job,argv,30000U,now_ms(),rootfd))goto done;
 s->purpose=JOB_SCREENSHOT;s->job_osd_renew=0;result=0;
done:
 saved=errno;if(rootfd>=0)close(rootfd);
 if(result)gkd_menu_guard_owner_exit(&s->power_guard);
 errno=saved;return result;
}
static void input_events(struct service *s,uint64_t now)
{
 unsigned events=0;
 if(!s->events_live)return;
 int menu_busy=s->purpose==JOB_GAME_MENU_CHECK||s->purpose==JOB_GAME_MENU;
 int blocked=((s->stopping||s->update_status>=3)&&s->lifecycle.state!=GKD_LIFECYCLE_RECOVERY)||s->trial_checked!=2||s->update_requested||s->update_entry_deadline||(s->purpose!=JOB_NONE&&!menu_busy)||s->request.action!=GKD_LIFECYCLE_ACTION_NONE||
  (s->lifecycle.state!=GKD_LIFECYCLE_ACTIVE&&s->lifecycle.state!=GKD_LIFECYCLE_STORAGE&&s->lifecycle.state!=GKD_LIFECYCLE_DEBUG&&s->lifecycle.state!=GKD_LIFECYCLE_RECOVERY);
 if(gkd_app_events_poll(&s->events,blocked,&events)){s->terminal_error=errno;return;}
 if(events&GKD_APP_EVENT_ACTIVITY)s->last_activity=now;
 if(blocked)return;
 if(s->game_wait_peer&&!(events&GKD_APP_EVENT_POWER))return;
 if((events&GKD_APP_EVENT_RETURN)&&s->lifecycle.state==GKD_LIFECYCLE_RECOVERY){
  (void)dispatch_command(s,"retry");return;
 }
 if((events&GKD_APP_EVENT_RETURN)&&
    (s->lifecycle.state==GKD_LIFECYCLE_STORAGE||s->lifecycle.state==GKD_LIFECYCLE_DEBUG)){
  (void)dispatch_command(s,"return");return;
 }
 if(s->lifecycle.state!=GKD_LIFECYCLE_ACTIVE)return;
 if(events&GKD_APP_EVENT_POWER){
  if(menu_busy){s->menu_power_pending=1;if(gkd_app_job_cancel(&s->job,now))s->terminal_error=errno;return;}
  if(game_job(s,0))fprintf(stderr,"GKD_APP_GAME=FAILED errno=%d\n",errno);
  return;
 }
 if(menu_busy)return;
 if(!status_is_card(s->status_kind)&&(events&GKD_APP_EVENT_SETTINGS)){
  if(game_job(s,0))fprintf(stderr,"GKD_APP_MENU=FAILED errno=%d\n",errno);
  else s->purpose=JOB_GAME_MENU_CHECK;
  return;
 }
 if(!status_is_card(s->status_kind)&&(events&GKD_APP_EVENT_SCREENSHOT)){
  if(screenshot(s))fprintf(stderr,"GKD_APP_SCREENSHOT=FAILED errno=%d\n",errno);
  return;
 }
 if(s->settings.auto_suspend_seconds&&now-s->last_activity>=(uint64_t)s->settings.auto_suspend_seconds*1000U){
  s->last_activity=now;(void)dispatch_command(s,"suspend");
 }
}
static int control_client(const char *command_text,const char *socket_path)
{
 struct sockaddr_un address={.sun_family=AF_UNIX};char reply[512];int fd,result=1;
 if(geteuid())return 1;
 fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);if(fd<0)return 1;
 snprintf(address.sun_path,sizeof(address.sun_path),"%s",socket_path);
 if(connect(fd,(struct sockaddr *)&address,sizeof(address))||
    send(fd,command_text,strlen(command_text),MSG_NOSIGNAL)!=(ssize_t)strlen(command_text))goto done;
 struct pollfd p={fd,POLLIN,0};
 if(poll(&p,1,2000)<=0)goto done;
 ssize_t n=recv(fd,reply,sizeof(reply)-1U,MSG_TRUNC);
 if(n<=0||n>=(ssize_t)sizeof(reply))goto done;
 if(memchr(reply,0,(size_t)n))goto done;
 reply[n]=0;fputs(reply,stdout);
 if(!strcmp(command_text,"status"))
  result=strncmp(reply,"GKD_APPLICATION state=",22)||reply[n-1]!='\n'||strchr(reply,'\n')!=reply+n-1;
 else if(!strcmp(command_text,"config-status")||!strcmp(command_text,"update-status")||!strcmp(command_text,"fps-status")){
  const char *prefix=!strcmp(command_text,"config-status")?"GKD_APPLICATION_CONFIG=":!strcmp(command_text,"fps-status")?"GKD_APPLICATION_FPS=":"GKD_APPLICATION_UPDATE=";
  result=strncmp(reply,prefix,strlen(prefix))||reply[n-1]!='\n'||strchr(reply,'\n')!=reply+n-1;
 }else result=strcmp(reply,"GKD_APPLICATION_COMMAND=ACCEPTED errno=0\n")!=0;
done:close(fd);return result;
}
/* Signals unwind through the same lifecycle and media guards. A failed
 * operation remains held for explicit recovery, never an automatic retry. */
static int stop_progress(struct service *s)
{
 if(!s->stopping)return 0;
 if(s->settings_save_status==1)return 0;
 s->update_entry_deadline=0;s->update_requested=0;s->network_desired=0;
 if(s->purpose==JOB_MENU)s->leave_menu=1;
 if(s->boot_ready&&!s->stop_host_requested&&s->lifecycle.state!=GKD_LIFECYCLE_ACTIVE&&
    s->lifecycle.state!=GKD_LIFECYCLE_RECOVERY){
  if(gkd_app_lifecycle_event(&s->lifecycle,GKD_LIFECYCLE_EVENT_RETURN,0)&&errno!=EALREADY&&errno!=EBUSY)
   fprintf(stderr,"GKD_APP_STOP=WAIT state=%u errno=%d\n",s->lifecycle.state,errno);
 }
 network_poll(s,now_ms());
 if(s->purpose!=JOB_NONE||s->network_running||s->network_dirty||s->network_error||
    s->request.action!=GKD_LIFECYCLE_ACTION_NONE)return 0;
 if(s->boot_ready&&s->lifecycle.state!=GKD_LIFECYCLE_ACTIVE)return 0;
 update_release(s);
 if(s->session.pid>0){
  if(!s->stop_host_requested){
   s->stop_host_requested=1;s->stopped_host=s->session.pid;
   if(gkd_app_session_stop(&s->session,now_ms())){s->stop_host_requested=0;s->terminal_error=errno;}
  }
  return 0;
 }
 pid_t cleanup_host=s->stopped_host>1?s->stopped_host:s->session.ready.host;
 if(cleanup_host>1&&gkd_app_profile_cleanup(cleanup_host)){
  s->terminal_error=errno;return 0;
 }
 if(s->session.state==GKD_SESSION_FAILED)s->terminal_error=s->session.error?s->session.error:ECHILD;
 return 1;
}
/* Boot updates precede the service and application mounts. Reuse the same
 * renderer here while leaving transaction exit codes and lifetime intact. */
static int update_boot_loading(struct service *s)
{
 struct sigaction action,old_term,old_int;
 struct gkd_ui_surface surface={s->status_pixels,GKD_UI_WIDTH,GKD_UI_HEIGHT,GKD_UI_WIDTH};
 unsigned frame=0,sequence=0;int status=0,result=1,visible=0;
 pid_t child,waited;
 memset(&action,0,sizeof(action));action.sa_handler=on_signal;sigemptyset(&action.sa_mask);
 if(sigaction(SIGTERM,&action,&old_term))return 1;
 if(sigaction(SIGINT,&action,&old_int)){(void)sigaction(SIGTERM,&old_term,NULL);return 1;}
 gkd_ui_config_defaults(&s->ui);
 s->fb=open(APP_FB,O_RDWR|O_CLOEXEC);
 int drawing=s->fb>=0&&!gkd_ui_font_load(&s->font,APP_FONT);
 if(drawing){
  gkd_ui_render_loading(&surface,&s->ui,&s->font,frame++);
  drawing=!gkd_ui_menu_send(s->fb,s->status_pixels,2000U,0U,++sequence);
  visible=drawing;
 }
 if(!drawing)fprintf(stderr,"GKD_APP_UPDATE_LOADING=UNAVAILABLE errno=%d\n",errno);
 child=fork();
 if(child<0)goto done;
 if(!child){char *argv[]={APP_UPDATE,"boot",NULL};execv(argv[0],argv);_exit(127);}
 for(;;){
  waited=waitpid(child,&status,WNOHANG);
  if(waited==child){result=WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);break;}
  if(waited<0&&errno!=EINTR)break;
  if(drawing){
   gkd_ui_render_loading(&surface,&s->ui,&s->font,frame++);
   if(gkd_ui_menu_send(s->fb,s->status_pixels,2000U,0U,++sequence)){
    fprintf(stderr,"GKD_APP_UPDATE_LOADING=UNAVAILABLE errno=%d\n",errno);drawing=0;
   }
  }
  /* A display error or termination request must not kill an armed update. */
  struct timespec delay={s->ui.loading_interval_ms/1000U,(long)(s->ui.loading_interval_ms%1000U)*1000000L};
  while(nanosleep(&delay,&delay)&&errno==EINTR){}
 }
done:
 if(visible)(void)gkd_ui_menu_clear(s->fb);
 if(result!=0&&result!=10&&result!=20&&s->font.data&&s->fb>=0){
  /* Persistent settings are not mounted during an offline boot transaction. */
  struct gkd_ui_osd message={service_text(s,GKD_UI_TEXT_ACTION_FAILED),GKD_UI_OSD_ICON_FAILURE,-1,1};
  if(!gkd_ui_export_osd_argb(s->osd_pixels,GKD_UI_OSD_WIDTH*GKD_UI_OSD_HEIGHT,&s->ui,&s->font,&message))
   (void)gkd_ui_plane_send(s->fb,s->osd_pixels,SETTINGS_OSD_MS,0U,++s->osd_sequence);
 }
 if(s->fb>=0){close(s->fb);s->fb=-1;}
 gkd_ui_font_release(&s->font);interrupted=0;
 (void)sigaction(SIGTERM,&old_term,NULL);(void)sigaction(SIGINT,&old_int,NULL);
 return result;
}
int main(int argc,char **argv)
{
 struct service s;struct sigaction action;
 memset(&s,0,sizeof(s));s.session=(struct gkd_app_session)GKD_APP_SESSION_INIT;
 s.media=(struct gkd_app_media)GKD_APP_MEDIA_INIT;s.job=(struct gkd_app_job)GKD_APP_JOB_INIT;
 s.network_job=(struct gkd_app_job)GKD_APP_JOB_INIT;
 s.settings_job=(struct gkd_app_job)GKD_APP_JOB_INIT;gkd_app_idle_lease_init(&s.settings_lease);gkd_app_idle_lease_init(&s.update_lease);gkd_app_fps_init(&s.fps);
 s.game_wait_pin=-1;
 s.fb=s.listener=s.lock=s.network_etc=-1;s.usb_observed=s.usb_stable=-1;
 gkd_menu_guard_owner_init(&s.power_guard);gkd_app_events_init(&s.events);gkd_ui_config_defaults(&s.ui);
 const char *entry=strrchr(argv[0],'/');entry=entry?entry+1:argv[0];
 if(argc==1&&!strcmp(entry,"gkd-system-update"))
  return control_client("update-entry","/var/run/gkd-application/control.sock");
 if(argc==3&&!strcmp(argv[1],"--control"))return control_client(argv[2],APP_SOCKET);
 if(argc==2&&!strcmp(argv[1],"--update-boot")&&!geteuid())return update_boot_loading(&s);
 if(argc!=1||geteuid())return 2;
 memset(&action,0,sizeof(action));action.sa_handler=on_signal;sigemptyset(&action.sa_mask);
 if(sigaction(SIGTERM,&action,NULL)||sigaction(SIGINT,&action,NULL))return 1;
 if(mkdir(APP_RUN,0700)&&errno!=EEXIST)return 1;
 s.lock=open(APP_RUN "/owner.lock",O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
 if(s.lock<0||flock(s.lock,LOCK_EX|LOCK_NB))return 1;
 if(command(&s,APP_CONFIG_STORE,"offline-load"))return 1;
 while(s.job.pid>0){gkd_app_job_poll(&s.job,now_ms());usleep(20000);}
 if(s.job.state!=GKD_JOB_DONE)return 1;
 gkd_app_job_close(&s.job);s.purpose=JOB_NONE;
 if(gkd_app_settings_load(APP_CONFIG,&s.settings)||gkd_ui_catalog_load(&s.texts,APP_CONFIG)||gkd_app_identity_read(&s.identity))return 1;
 s.fb=open("/dev/fb0",O_RDWR|O_CLOEXEC);struct gkd_ui_menu_caps caps;struct gkd_ui_plane_caps osd_caps;
 if(s.fb<0||gkd_ui_plane_capabilities(s.fb,&osd_caps)||gkd_ui_menu_capabilities(s.fb,&caps)||gkd_ui_font_load(&s.font,APP_FONT)||gkd_ui_font_load(&s.cn_font,APP_CN_FONT)||gkd_ui_font_load(&s.cn_compact,APP_CN_COMPACT_FONT)||(s.listener=listen_control())<0)return 1;
 s.cn_font.latin=s.cn_compact.latin=&s.font;s.cn_font.compact=&s.cn_compact;
 /* The input producer starts only after the persisted config generation exists. */
 if(command(&s,APP_INPUT_INIT,"start"))return 1;
 while(s.job.pid>0){gkd_app_job_poll(&s.job,now_ms());usleep(20000);}
 if(s.job.state!=GKD_JOB_DONE)return 1;
 gkd_app_job_close(&s.job);s.purpose=JOB_NONE;
 /* Initialize the known ACM/network interfaces before mounting application media. */
 if(command(&s,APP_USB,"network"))return 1;
 while(s.job.pid>0){gkd_app_job_poll(&s.job,now_ms());usleep(20000);}
 if(s.job.state!=GKD_JOB_DONE)return 1;
 gkd_app_job_close(&s.job);s.purpose=JOB_NONE;
 game_card_poll(&s,now_ms());
 /* Own the no-card screen before the frontend can submit its first frame. */
 if(s.card_initialized&&!s.card_generation&&status_screen(&s,now_ms()))s.terminal_error=errno;
 if(!s.terminal_error&&(notice_start_prepare(&s)||gkd_app_session_start(&s.session,s.settings.frontend_timeout,now_ms())))s.terminal_error=errno;
 for(;;){
  if(interrupted){s.stopping=1;interrupted=0;}
  if(stop_progress(&s))break;
  session(&s);jobs(&s);
  if(!s.boot_ready&&status_is_card(s.status_kind)&&status_screen(&s,now_ms()))s.terminal_error=errno;
  if(s.freeze_renew&&now_ms()>=s.freeze_renew&&freeze(&s))s.terminal_error=errno;
  if(!s.terminal_error&&s.boot_ready){
   trial_health(&s);update_prepare(&s);operation(&s);
   uint64_t now=now_ms();
   /* Keep shared text notices alive through asynchronous cleanup/mount/start. */
   if((s.request.action==GKD_LIFECYCLE_ACTION_NONE||s.card_refreshing||gkd_app_lifecycle_wait_kind(&s.lifecycle))&&status_screen(&s,now))s.terminal_error=errno;
   network_poll(&s,now);input_events(&s,now);usb_events(&s,now);battery(&s,now);game_card_poll(&s,now);
  }
  if(s.lifecycle.state==GKD_LIFECYCLE_SUSPENDED)
   if(gkd_app_lifecycle_event(&s.lifecycle,GKD_LIFECYCLE_EVENT_RETURN,0))s.terminal_error=errno;
  fps_tick(&s,now_ms());
  control(&s);
  if(stop_progress(&s))break;
  struct pollfd p={s.listener,POLLIN,0};(void)poll(&p,1,20);
 }
 if(s.freeze_renew)ioctl(s.fb,GKD_UI_FREEZE_CLEAR);
 fps_preempt(&s);gkd_app_fps_close(&s.fps);
 gkd_app_idle_lease_release(&s.settings_lease);
 gkd_menu_guard_owner_exit(&s.power_guard);gkd_app_session_close(&s.session);
 if(s.game_wait_peer)close(s.game_wait_pin);
 if(s.network_etc>=0)close(s.network_etc);
 gkd_app_events_close(&s.events);gkd_ui_font_release(&s.cn_compact);gkd_ui_font_release(&s.cn_font);gkd_ui_font_release(&s.font);
 close(s.listener);unlink(APP_SOCKET);close(s.fb);close(s.lock);
 return s.terminal_error?1:0;
}
