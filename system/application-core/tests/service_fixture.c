/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#define APP_FB "/tmp/gkd-service-boot-fb"
#define APP_FONT "/out/fallback.psf"
#define APP_GAME_DISKSEQ "/tmp/gkd-service-diskseq"
#define APP_MANAGEMENT "/tmp/gkd-service-management"
#define APP_UPDATE "/tmp/gkd-service-update"
#define APP_TRIAL_MARKER "/tmp/gkd-service-trial"
#define APP_PREPARE "/tmp/gkd-service-prepare"
#define APP_RUNTIME_ID "/tmp/gkd-service-runtime"
#define APP_USB_ONLINE "/tmp/gkd-service-online"
#define APP_UPDATE_LOCK "/tmp/gkd-service-update-lock"
#define APP_CONFIG "/tmp/gkd-service-effective.conf"
#define APP_GENERATION "/tmp/gkd-service-generation"
#define APP_INPUT_INIT "/tmp/gkd-service-usb-fixture"
#define APP_NETWORK "/tmp/gkd-service-network-fixture"
#define APP_GAME "/tmp/gkd-service-game-fixture"
#define APP_MENU "/tmp/gkd-service-menu-fixture"
#define APP_USB "/tmp/gkd-service-usb-fixture"
#define APP_GUARD "/tmp/gkd-service-usb-fixture"
#define APP_CONFIG_STORE "/tmp/gkd-service-usb-fixture"
#define APP_LUN "/tmp/gkd-service-lun"
#define main gkd_application_service_main
#include "../source/gkd-application-service.c"
#undef main
#include <assert.h>
#include <stdarg.h>
#include <sys/mman.h>
static pid_t trusted_wait_peer;
int __wrap_gkd_app_fps_authorize_launcher(const struct ucred *peer,const struct gkd_app_fps_context *context,int *pin,unsigned long long *start)
{
 assert(context->host>1&&context->init>1);
 if(!trusted_wait_peer||peer->pid!=trusted_wait_peer){errno=EPERM;return -1;}
 *pin=(int)syscall(SYS_pidfd_open,peer->pid,0);*start=1;return *pin<0?-1:0;
}
static int teardown_pin_fd=-1;
void __real_gkd_app_idle_lease_release(struct gkd_app_idle_lease *);
static unsigned idle_acquires,idle_releases;static int idle_result=GKD_APP_IDLE_ACQUIRED,idle_live_error;
enum gkd_app_idle_result __wrap_gkd_app_idle_lease_acquire(struct gkd_app_idle_lease *lease,pid_t host,pid_t init)
{(void)lease;assert(host>1&&init>1);++idle_acquires;return idle_result;}
int __wrap_gkd_app_idle_lease_live(const struct gkd_app_idle_lease *lease)
{(void)lease;if(idle_live_error){errno=ESRCH;return -1;}return 0;}
void __wrap_gkd_app_idle_lease_release(struct gkd_app_idle_lease *lease)
{++idle_releases;if(teardown_pin_fd>=0)__real_gkd_app_idle_lease_release(lease);else gkd_app_idle_lease_init(lease);}
static unsigned pauses,unmounts,mounts,resumes,stops,starts,freezes,thaws,validated,cleaned;
static unsigned status_draws,status_sends,status_hides,status_clears;
static unsigned osd_draws,osd_sends,osd_clears,osd_ttl,osd_fade,osd_icon,osd_last_sequence;
static int osd_critical;
static int osd_level,osd_busy,osd_error;
static char osd_text[24];
static struct gkd_ui_font test_font;
int __real_gkd_ui_export_osd_argb(uint32_t *,size_t,const struct gkd_ui_config *,const struct gkd_ui_font *,const struct gkd_ui_osd *);
int __wrap_gkd_ui_export_osd_argb(uint32_t *pixels,size_t count,const struct gkd_ui_config *config,const struct gkd_ui_font *font,const struct gkd_ui_osd *message)
{
 osd_draws++;snprintf(osd_text,sizeof(osd_text),"%s",message->text);
 osd_icon=message->icon;osd_level=message->level;osd_critical=message->critical;
 return __real_gkd_ui_export_osd_argb(pixels,count,config,font,message);
}
int __wrap_gkd_ui_plane_send(int fd,const uint32_t *pixels,unsigned ttl,unsigned fade,unsigned sequence)
{
 (void)fd;assert(pixels&&ttl>=20U&&ttl<=10000U&&(fade==0U||fade==160U)&&sequence);
 if(osd_busy||osd_error){errno=osd_busy?EBUSY:EIO;return -1;}
 osd_sends++;osd_ttl=ttl;osd_fade=fade;osd_last_sequence=sequence;return 0;
}
int __wrap_gkd_ui_plane_clear(int fd){(void)fd;osd_clears++;return 0;}

void __wrap_gkd_ui_render_status(struct gkd_ui_surface *surface,const struct gkd_ui_config *config,const struct gkd_ui_font *font,const char *label,int failed)
{(void)surface;(void)config;(void)font;assert(label&&failed>=0);status_draws++;}
static unsigned status_transition;
static int boot_display_error;
int __wrap_gkd_ui_menu_send(int fd,const uint16_t *pixels,unsigned ttl,unsigned transition,unsigned sequence)
{(void)fd;assert(pixels&&ttl==2000&&(transition==0||transition==200)&&sequence);if(boot_display_error){errno=EIO;return -1;}status_sends++;status_transition=transition;return 0;}
int __wrap_gkd_ui_menu_hide(int fd){(void)fd;status_hides++;return 0;}
int __wrap_gkd_ui_menu_clear(int fd){(void)fd;status_clears++;return 0;}
static unsigned loading_draws,loading_frame;
void __real_gkd_ui_render_loading(struct gkd_ui_surface *,const struct gkd_ui_config *,const struct gkd_ui_font *,unsigned);
void __wrap_gkd_ui_render_loading(struct gkd_ui_surface *surface,const struct gkd_ui_config *config,const struct gkd_ui_font *font,unsigned frame)
{
 loading_draws++;loading_frame=frame;
 __real_gkd_ui_render_loading(surface,config,font,frame);
 char path[80];snprintf(path,sizeof(path),"/out/loading-%u.rgb565",frame%8U);
 FILE *f=fopen(path,"wb");assert(f&&fwrite(surface->pixels,2,GKD_UI_WIDTH*GKD_UI_HEIGHT,f)==GKD_UI_WIDTH*GKD_UI_HEIGHT&&!fclose(f));
}
static unsigned card_pages;
static char card_body[64];
int __real_gkd_ui_render_confirmation_info(struct gkd_ui_surface *,const struct gkd_ui_config *,const struct gkd_ui_font *,const struct gkd_ui_confirmation *);
int __wrap_gkd_ui_render_confirmation_info(struct gkd_ui_surface *surface,const struct gkd_ui_config *config,const struct gkd_ui_font *font,const struct gkd_ui_confirmation *info)
{
 assert(info->action_a==GKD_UI_ACTION_DISABLED&&info->action_b==GKD_UI_ACTION_DISABLED);
 assert(info->count&&info->first==0&&(!strcmp(info->title,"GAME CARD")||!strcmp(info->title,"USB MODE")||!strcmp(info->title,"POWER")||!strcmp(info->title,"LOADING")||!strcmp(info->title,"SYSTEM UPDATE")));
 snprintf(card_body,sizeof(card_body),"%s",info->lines[0]);card_pages++;
 int result=__real_gkd_ui_render_confirmation_info(surface,config,font,info);
 assert(!result);
 const char *path=!strcmp(card_body,"LOADING")?"/out/usb-loading.rgb565":!strcmp(card_body,"INSERT GAME CARD")?"/out/card-absent.rgb565":"/out/card-loading.rgb565";
 FILE *f=fopen(path,"wb");assert(f&&fwrite(surface->pixels,2,GKD_UI_WIDTH*GKD_UI_HEIGHT,f)==GKD_UI_WIDTH*GKD_UI_HEIGHT&&!fclose(f));
 return result;
}
static unsigned event_closes;
static int real_card_lease;
static int event_reuse_failure;
int __wrap_gkd_app_events_open_menu(struct gkd_app_events *events,const struct gkd_app_settings *settings)
{(void)settings;
 if(real_card_lease){gkd_menu_guard_owner_init(&events->observer.menu_guard);if(gkd_menu_guard_owner_enter(&events->observer.menu_guard))return -1;}
 events->exclusive=1;return 0;}
int __wrap_gkd_app_events_open_menu_reuse(struct gkd_app_events *events,
 const struct gkd_app_settings *settings,struct gkd_menu_guard_owner *guard)
{
 (void)settings;
 if(event_reuse_failure){errno=EIO;return -1;}
 assert(guard&&guard->lease_fd>=0);events->exclusive=1;
 events->observer.menu_guard.lease_fd=guard->lease_fd;guard->lease_fd=-1;return 0;
}
static unsigned identity_reads;
int __wrap_gkd_app_identity_read(struct gkd_app_identity *identity){(void)identity;identity_reads++;return 0;}
static int stop_failure,cleanup_failures,battery_percent=29,battery_invalid,battery_powered;
int __wrap_gkd_app_battery_read(struct gkd_app_battery *reading,const unsigned *curve)
{(void)curve;if(battery_invalid){errno=EIO;return -1;}memset(reading,0,sizeof(*reading));reading->percent=battery_percent;reading->external_power=battery_powered;return 0;}
int __wrap_gkd_app_battery_led(const struct gkd_app_battery *reading,const unsigned *thresholds)
{(void)reading;(void)thresholds;return 0;}

int __wrap_gkd_app_events_open(struct gkd_app_events *events,const struct gkd_app_settings *settings)
{(void)settings;events->exclusive=0;return 0;}
void __wrap_gkd_app_events_close(struct gkd_app_events *events)
{
 event_closes++;
 if(events->exclusive)gkd_menu_guard_owner_exit(&events->observer.menu_guard);
 events->exclusive=0;
}
int __real_stat(const char *path,struct stat *st);
int __wrap_stat(const char *path,struct stat *st)
{
 if(!strcmp(path,"/dev/mmcblk0p2")||!strcmp(path,"/dev/mmcblk1p1")){
  if(__real_stat("/media/data/local/etc",st))return -1;
  st->st_rdev=st->st_dev;st->st_mode=S_IFBLK|0600;return 0;
 }
 return __real_stat(path,st);
}
int __wrap_gkd_app_media_pause(struct gkd_app_media *m,pid_t init,pid_t host,unsigned timeout)
{assert(init>1&&host>1&&timeout==1000);pauses++;m->paused=1;return 0;}
int __wrap_gkd_app_media_game_unmount(struct gkd_app_media *m)
{assert(m->paused);unmounts++;m->mounted=0;return 0;}
int __wrap_gkd_app_media_game_mount(struct gkd_app_media *m)
{assert(m->paused);mounts++;m->mounted=1;return 0;}
int __wrap_gkd_app_media_resume(struct gkd_app_media *m)
{assert(m->paused&&m->mounted==1);resumes++;m->paused=0;return 0;}
int __wrap_gkd_app_session_stop(struct gkd_app_session *session,uint64_t now)
{
 if(teardown_pin_fd>=0){errno=0;assert(fcntl(teardown_pin_fd,F_GETFD)<0&&errno==EBADF);}
 assert(now);stops++;session->pid=-1;
 session->state=stop_failure?GKD_SESSION_FAILED:GKD_SESSION_STOPPED;
 return 0;
}
int __wrap_gkd_app_session_start(struct gkd_app_session *session,unsigned timeout,uint64_t now)
{
 if(real_card_lease){
  struct gkd_menu_guard_controls controls;uint64_t epoch;
  gkd_menu_guard_controls_init(&controls);
  int acquired=gkd_menu_guard_controls_acquire(&controls,&epoch);
  if(acquired)fputs("CARD_CONTROLS_START blocked by notice lease\n",stderr);
  assert(acquired==0);gkd_menu_guard_controls_release(&controls);
 }
 assert(timeout&&now);starts++;session->state=GKD_SESSION_STARTING;session->pid=1001;
 session->ready=(struct gkd_app_service_ready){GKD_APP_SERVICE_MAGIC,1U,1001,1002,1003};return 0;
}
enum gkd_app_session_state __wrap_gkd_app_session_poll(struct gkd_app_session *session,uint64_t now)
{assert(now);return session->state;}
int __wrap_gkd_app_identity_verify(const struct gkd_app_identity *identity)
{(void)identity;validated++;return 0;}
int __wrap_gkd_app_profile_cleanup(pid_t host)
{
 if(teardown_pin_fd>=0){errno=0;assert(fcntl(teardown_pin_fd,F_GETFD)<0&&errno==EBADF);}
 assert(host>1);cleaned++;
 if(cleanup_failures){cleanup_failures--;errno=EIO;return -1;}
 return 0;
}
int __wrap_ioctl(int fd,unsigned long op,...)
{
 (void)fd;
 if(op==GKD_UI_FREEZE_SUBMIT){
  va_list ap;va_start(ap,op);struct gkd_ui_freeze_submit *v=va_arg(ap,struct gkd_ui_freeze_submit *);va_end(ap);
  assert(v->ttl_ms==10000&&v->sequence&&v->reserved[0]==0&&v->reserved[1]==0);freezes++;return 0;
 }
 if(op==GKD_UI_FREEZE_CLEAR){thaws++;return 0;}
 errno=ENOTTY;return -1;
}
static void write_file(const char *path,const char *body,mode_t mode)
{FILE *f=fopen(path,"w");assert(f);assert(fputs(body,f)>=0);assert(!fclose(f));assert(!chmod(path,mode));}
static void init(struct service *s)
{
 memset(s,0,sizeof(*s));s->session=(struct gkd_app_session)GKD_APP_SESSION_INIT;
 s->media=(struct gkd_app_media)GKD_APP_MEDIA_INIT;s->job=(struct gkd_app_job)GKD_APP_JOB_INIT;
 s->network_job=(struct gkd_app_job)GKD_APP_JOB_INIT;
 s->settings_job=(struct gkd_app_job)GKD_APP_JOB_INIT;gkd_app_idle_lease_init(&s->settings_lease);gkd_app_idle_lease_init(&s->update_lease);gkd_app_fps_init(&s->fps);
 gkd_app_events_init(&s->events);gkd_menu_guard_owner_init(&s->power_guard);s->fb=s->network_etc=-1;s->boot_ready=1;s->trial_checked=2;s->session_generation=1;
 s->session.pid=901;s->session.state=GKD_SESSION_READY;
 s->session.ready=(struct gkd_app_service_ready){GKD_APP_SERVICE_MAGIC,1U,901,902,903};
 gkd_ui_config_defaults(&s->ui);s->font=test_font;
 assert(!gkd_app_settings_load(APP_CONFIG,&s->settings));
 assert(!gkd_app_lifecycle_init(&s->lifecycle,1U,begin,s));
}
static void reach(struct service *s,enum gkd_app_lifecycle_state state)
{
 uint64_t deadline=now_ms()+4000;
 while(now_ms()<deadline){
  session(s);jobs(s);operation(s);
  if(s->request.action==GKD_LIFECYCLE_ACTION_NONE||s->card_refreshing||gkd_app_lifecycle_wait_kind(&s->lifecycle))assert(!status_screen(s,now_ms()));
  assert(!s->terminal_error);
  if(s->lifecycle.state==state&&s->purpose==JOB_NONE&&s->request.action==GKD_LIFECYCLE_ACTION_NONE&&!s->status_hiding)return;
  usleep(1000);
 }
 fprintf(stderr,"wanted=%u state=%u request=%u purpose=%u error=%d\n",
  state,s->lifecycle.state,s->request.action,s->purpose,s->lifecycle.last_error);assert(0);
}
static void drain_jobs(struct service *s)
{
 uint64_t deadline=now_ms()+4000;
 while(s->purpose!=JOB_NONE&&now_ms()<deadline){jobs(s);usleep(1000);}
 assert(s->purpose==JOB_NONE);
}
static int control_reply(const char *command,const char *reply)
{
 static const char path[]="/tmp/gkd-service-client.sock";
 struct sockaddr_un address={.sun_family=AF_UNIX};int listener,client,status;pid_t child;
 assert(strlen(path)<sizeof(address.sun_path));strcpy(address.sun_path,path);(void)unlink(path);
 listener=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);assert(listener>=0);
 assert(!bind(listener,(struct sockaddr *)&address,sizeof(address))&&!listen(listener,1));
 child=fork();assert(child>=0);
 if(!child){
  char input[64];client=accept(listener,NULL,NULL);assert(client>=0);
  ssize_t n=recv(client,input,sizeof(input),0);assert(n==(ssize_t)strlen(command)&&!memcmp(input,command,(size_t)n));
  if(reply)assert(send(client,reply,strlen(reply),MSG_NOSIGNAL)==(ssize_t)strlen(reply));
  close(client);close(listener);_exit(0);
 }
 int result=control_client(command,path);
 assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status));
 close(listener);assert(!unlink(path));return result;
}
static int finish_stopping(struct service *s)
{
 uint64_t deadline=now_ms()+4000;
 while(now_ms()<deadline){
  jobs(s);operation(s);
  if(stop_progress(s))return 0;
  usleep(1000);
 }
 return -1;
}
static void fire_and_forget_control(struct service *s,const char *command)
{
 static const char path[]="/tmp/gkd-service-yield-fixture.sock";
 struct sockaddr_un address={.sun_family=AF_UNIX};
 strcpy(address.sun_path,path);unlink(path);
 int listener=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
 assert(listener>=0&&!bind(listener,(struct sockaddr *)&address,sizeof(address))&&!listen(listener,4));
 int client=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
 assert(client>=0&&!connect(client,(struct sockaddr *)&address,sizeof(address)));
 assert(send(client,command,strlen(command),MSG_NOSIGNAL)==(ssize_t)strlen(command));
 close(client);s->listener=listener;control(s);
 close(listener);s->listener=-1;unlink(path);
}
static void service_control(struct service *s,const char *command,const char *expected)
{
 static const char path[]="/tmp/gkd-service-control-fixture.sock";
 struct sockaddr_un address={.sun_family=AF_UNIX};char reply[320];int listener,client;
 assert(strlen(path)<sizeof(address.sun_path));strcpy(address.sun_path,path);(void)unlink(path);
 listener=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);assert(listener>=0);
 assert(!bind(listener,(struct sockaddr *)&address,sizeof(address))&&!listen(listener,1));
 client=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);assert(client>=0);
 assert(!connect(client,(struct sockaddr *)&address,sizeof(address)));
 assert(send(client,command,strlen(command),MSG_NOSIGNAL)==(ssize_t)strlen(command));
 s->listener=listener;control(s);
 ssize_t n=recv(client,reply,sizeof(reply)-1U,0);assert(n>0);reply[n]=0;assert(!strcmp(reply,expected));
 close(client);close(listener);s->listener=-1;assert(!unlink(path));
}

static void settings_service_tests(void)
{
 struct service editor;char command_text[160];
 static const char hash[]="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
 char line[66];snprintf(line,sizeof(line),"%s\n",hash);write_file(APP_GENERATION,line,0444);
 init(&editor);idle_result=GKD_APP_IDLE_BUSY;unsigned previous=idle_acquires;
 assert(dispatch_command(&editor,"settings-menu")<0&&errno==EBUSY&&idle_acquires==previous+1U&&editor.purpose==JOB_NONE&&editor.lifecycle.state==GKD_LIFECYCLE_ACTIVE);
 idle_result=GKD_APP_IDLE_ERROR;assert(dispatch_command(&editor,"settings-menu")<0&&editor.purpose==JOB_NONE);
 idle_result=GKD_APP_IDLE_ACQUIRED;
 write_file(APP_MENU,"#!/bin/sh\n[ \"$2\" = SETTINGS ] || exit 2\nprintf 'GKD_MENU_READY kind=SETTINGS\\n'\nsleep 0.4\nprintf 'GKD_MENU_RESULT=CANCELLED kind=SETTINGS\\n'\n",0700);
 init(&editor);unsigned releases=idle_releases,notification_before=osd_sends;
 assert(!dispatch_command(&editor,"settings-menu")&&editor.menu_kind==MENU_SETTINGS);
 reach(&editor,GKD_LIFECYCLE_ACTIVE);
 assert(!editor.terminal_error&&editor.settings_save_status==0&&idle_releases==releases+1U&&osd_sends==notification_before);
 write_file(APP_MENU,"#!/bin/sh\nprintf 'GKD_MENU_READY kind=SETTINGS\\n'\nsleep 0.5\nprintf 'GKD_MENU_RESULT=SAVED kind=SETTINGS\\n'\n",0700);
 write_file(APP_CONFIG_STORE,"#!/bin/sh\n[ \"$1\" = settings-save ] && [ \"$2\" = 3 ] && [ \"$4 $5 $6 $7\" = '1 0 1 0' ] || exit 2\nsleep 0.1\nprintf 'GKD_APP_SETTINGS=SAVED generation=%s\\n' \"$3\"\n",0700);
 init(&editor);editor.session.ready.init=getpid();releases=idle_releases;
 assert(!dispatch_command(&editor,"settings-menu"));operation(&editor);
 uint64_t until=now_ms()+2000U;
 while(!editor.menu_ready&&now_ms()<until){jobs(&editor);usleep(1000);}
 assert(editor.menu_ready&&editor.lifecycle.state==GKD_LIFECYCLE_MENU);
 snprintf(command_text,sizeof(command_text),"settings-save %s 1 0 1 0 0",hash);
 assert(settings_save(&editor,command_text,editor.job.pid+1)<0&&errno==EBUSY&&editor.settings_save_status==0);
 idle_live_error=1;assert(settings_save(&editor,command_text,editor.job.pid)<0&&errno==ESRCH);idle_live_error=0;
 assert(settings_save(&editor,"settings-save bad 1 0 1 0",editor.job.pid)<0&&errno==EINVAL);
 assert(!settings_save(&editor,command_text,editor.job.pid));
 assert(editor.settings_save_status==1&&editor.settings_job.transaction&&!editor.settings_job.deadline&&editor.job.pid>0);
 unsigned progress_sends=osd_sends;
 settings_poll(&editor);assert(osd_sends==progress_sends&&editor.settings_save_status==1);
 /* The menu owns animation; the service must not overlay a second progress UI. */
 settings_poll(&editor);assert(osd_sends==progress_sends);

 assert(gkd_app_job_cancel(&editor.settings_job,now_ms())<0&&errno==EBUSY);
 assert(dispatch_command(&editor,"suspend")<0&&errno==EBUSY);
 assert(dispatch_command(&editor,"return")<0&&errno==EBUSY);
 editor.stopping=1;assert(!stop_progress(&editor)&&!editor.stop_host_requested&&idle_releases==releases);editor.stopping=0;
 unsigned clear_before_commit=osd_clears,send_before_commit=osd_sends;
 while(editor.settings_save_status==1&&now_ms()<until){jobs(&editor);usleep(1000);}
 assert(editor.settings_save_status==2&&!editor.terminal_error&&editor.job.pid>0&&idle_releases==releases);
 assert(strcmp(osd_text,"SUCCESS")&&osd_sends==send_before_commit&&osd_clears==clear_before_commit+1U&&!editor.settings_osd_renew);
 uint64_t saved_after=now_ms();editor.last_activity=saved_after-600000U;
 reach(&editor,GKD_LIFECYCLE_ACTIVE);assert(!editor.terminal_error&&idle_releases==releases+1U);
 assert(editor.last_activity>=saved_after);
 assert(!strcmp(osd_text,"SUCCESS")&&osd_icon==GKD_UI_OSD_ICON_SUCCESS&&osd_ttl==2000U&&osd_fade==160U&&osd_sends==send_before_commit+1U);

 /* A committed transaction is still not a successful menu-close acknowledgement. */
 for(unsigned bad=0;bad<3U;++bad){
  const char *menus[]={
   "#!/bin/sh\nprintf 'GKD_MENU_READY kind=SETTINGS\\n'\nsleep 0.4\nprintf 'GKD_MENU_RESULT=CANCELLED kind=SETTINGS\\n'\n",
   "#!/bin/sh\nprintf 'GKD_MENU_READY kind=SETTINGS\\n'\nsleep 0.4\nprintf 'GKD_MENU_RESULT=SAVED kind=SETTINGS\\nEXTRA\\n'\n",
   "#!/bin/sh\nprintf 'GKD_MENU_READY kind=SETTINGS\\n'\nsleep 0.4\nexit 1\n"};
  write_file(APP_MENU,menus[bad],0700);
  init(&editor);editor.session.ready.init=getpid();
  assert(!dispatch_command(&editor,"settings-menu"));operation(&editor);until=now_ms()+2000U;
  while(!editor.menu_ready&&now_ms()<until){jobs(&editor);usleep(1000);}
  assert(editor.menu_ready&&!editor.job.deadline&&!settings_save(&editor,command_text,editor.job.pid));
  while(editor.settings_save_status==1&&now_ms()<until){jobs(&editor);usleep(1000);}
  assert(editor.settings_save_status==2);unsigned before_exit=osd_sends;
  while(editor.purpose==JOB_MENU&&now_ms()<until){jobs(&editor);usleep(1000);}
  assert(editor.purpose==JOB_NONE&&osd_sends==before_exit);
 }
 /* A settings update action waits for save + menu release, never installs. */
 init(&editor);editor.menu_kind=MENU_SETTINGS;
 assert(menu_result(&editor,"GKD_MENU_READY kind=SETTINGS\nGKD_MENU_RESULT=UPDATE kind=SETTINGS\n")<0);
 editor.settings_save_status=2;
 assert(menu_result(&editor,"GKD_MENU_READY kind=SETTINGS\nGKD_MENU_RESULT=UPDATE kind=SETTINGS\n")==GKD_LIFECYCLE_EVENT_CANCEL);
 assert(editor.update_from_settings&&editor.update_entry_deadline&&!editor.update_requested);
 editor.lifecycle.state=GKD_LIFECYCLE_MENU;update_prepare(&editor);
 assert(editor.purpose==JOB_NONE); /* Wait until input and idle lease release. */
 /* A clean UI receipt alone is never authority to persist or report saved. */
 init(&editor);editor.menu_kind=MENU_SETTINGS;
 assert(menu_result(&editor,"GKD_MENU_READY kind=SETTINGS\nGKD_MENU_RESULT=SAVED kind=SETTINGS\n")<0&&errno==EPROTO);
 assert(menu_result(&editor,"GKD_MENU_READY kind=SETTINGS\nGKD_MENU_RESULT=TIMED_OUT kind=SETTINGS\n")==GKD_LIFECYCLE_EVENT_CANCEL);
 assert(menu_result(&editor,"GKD_MENU_READY kind=SETTINGS\nGKD_MENU_RESULT=SELECTED kind=SETTINGS index=0\n")<0);
 for(unsigned kind=0;kind<2U;kind++){
  editor.menu_kind=(int)kind;
  const char *name=kind?"POWER":"USB";char receipt[128];
  snprintf(receipt,sizeof(receipt),"GKD_MENU_READY kind=%s\nGKD_MENU_RESULT=TIMED_OUT kind=%s\n",name,name);
  for(unsigned value=0;value<3U;value++){
   editor.settings.power_default=editor.settings.usb_default=value;
   assert(menu_result(&editor,receipt)==GKD_LIFECYCLE_EVENT_CANCEL);
  }
 }
 editor.menu_kind=MENU_SETTINGS;

 int failure=0,code=0;
 assert(!settings_failure("GKD_APP_SETTINGS=FAILED state=unknown errno=28\n",&failure,&code)&&!failure&&code==28);
 assert(!settings_failure("GKD_APP_SETTINGS=FAILED state=recoverable errno=116\n",&failure,&code)&&failure&&code==116);
 assert(settings_failure("GKD_APP_SETTINGS=FAILED state=unknown errno=+28\n",&failure,&code)<0);
 assert(settings_failure("GKD_APP_SETTINGS=FAILED state=unknown errno=028\n",&failure,&code)<0);
 assert(settings_failure("GKD_APP_SETTINGS=FAILED state=unknown errno=28\n\n",&failure,&code)<0);
 /* Drive the exact unknown receipt through real child polling and fatal menu exit. */
 write_file(APP_MENU,"#!/bin/sh\nprintf 'GKD_MENU_READY kind=SETTINGS\\n'\nsleep 0.3\nexit 1\n",0700);
 write_file(APP_CONFIG_STORE,"#!/bin/sh\nprintf 'GKD_APP_SETTINGS=FAILED state=unknown errno=28\\n'\nexit 1\n",0700);
 init(&editor);editor.session.ready.init=getpid();releases=idle_releases;
 assert(!dispatch_command(&editor,"settings-menu"));operation(&editor);until=now_ms()+2000U;
 while(!editor.menu_ready&&now_ms()<until){jobs(&editor);usleep(1000);}
 assert(editor.menu_ready&&!editor.job.deadline&&!settings_save(&editor,command_text,editor.job.pid));
 while(editor.settings_save_status==1&&now_ms()<until){jobs(&editor);usleep(1000);}
 assert(editor.settings_save_status==-2&&editor.terminal_error==28&&
  !strcmp(editor.settings_receipt,"GKD_APP_SETTINGS=FAILED state=unknown errno=28\n")&&idle_releases==releases);
 assert(!strcmp(osd_text,"FAILED")&&osd_icon==GKD_UI_OSD_ICON_FAILURE&&osd_ttl==2000U&&!editor.settings_osd_renew);
 while(editor.purpose==JOB_MENU&&now_ms()<until){jobs(&editor);usleep(1000);}
 assert(editor.purpose==JOB_NONE&&editor.terminal_error==28&&idle_releases==releases+1U);
 puts("SETTINGS_UNKNOWN_FAILURE_PASS exact-helper-errno/fatal-menu/lease-cleanup");
 assert(!unlink(APP_GENERATION));
 puts("SETTINGS_SAVE_OSD_PASS shared-presentation/save-icon/clear-progress/menu-exit-before-success/cancel-or-crash-no-success/failed");
 puts("SETTINGS_SERVICE_PASS idle-busy-no-menu/cancel-no-save/peer-pid/lease-loss/strict-input/transaction-holds-menu/noncancel/save-receipt/timeout-cancels");
}

static void fps_service_tests(void)
{
 struct service view;init(&view);view.settings.show_fps=1;
 struct gkd_fps_counter_page *page=mmap(NULL,GKD_FPS_COUNTER_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
 assert(page!=MAP_FAILED);int life[2];assert(!pipe2(life,O_CLOEXEC|O_NONBLOCK));
 memset(page,0,GKD_FPS_COUNTER_BYTES);page->magic=GKD_FPS_COUNTER_MAGIC;page->version=GKD_FPS_COUNTER_VERSION;
 page->bytes=GKD_FPS_COUNTER_BYTES;page->session_hi=1;page->session_lo=2;page->producer_state=GKD_FPS_PRODUCER_ATTACHED;
 view.fps.peer_pidfd=(int)syscall(SYS_pidfd_open,getpid(),0U);assert(view.fps.peer_pidfd>=0);
 view.fps.host=view.session.ready.host;view.fps.init=view.session.ready.init;
 view.fps.page=page;view.fps.lifetime_fd=life[0];view.fps.session_hi=1;view.fps.session_lo=2;
 view.fps.state=GKD_APP_FPS_UNAVAILABLE;
 uint64_t start=now_ms();fps_tick(&view,start);assert(view.fps_visible);
 page->successful_flips=60;fps_tick(&view,start+1000U);assert(view.fps_visible&&!strcmp(osd_text,"FPS 60"));
 unsigned clears_before=osd_clears;
 assert(!osd(&view,"SHOT SAVED",3U,-1,0,3000U)&&!view.fps_visible&&osd_clears==clears_before+1U);
 unsigned sends_before=osd_sends;fps_tick(&view,start+1100U);
 assert(!view.fps_visible&&osd_sends==sends_before&&!strcmp(osd_text,"SHOT SAVED"));
 fps_tick(&view,view.transient_until+1U);assert(view.fps_visible);
 assert(!dispatch_command(&view,"osd-yield")&&!view.fps_visible);
 sends_before=osd_sends;fps_tick(&view,now_ms());assert(osd_sends==sends_before);
 view.transient_until=0;view.button_osd_until=0;osd_busy=1;clears_before=osd_clears;
 fps_tick(&view,start+4000U);assert(!view.fps_visible&&osd_clears==clears_before);
 osd_busy=0;fps_tick(&view,view.fps_retry+1U);assert(view.fps_visible);
 view.lifecycle.state=GKD_LIFECYCLE_MENU;fps_tick(&view,start+4500U);assert(!view.fps_visible);
 view.lifecycle.state=GKD_LIFECYCLE_DEBUG;fps_tick(&view,start+4520U);assert(!view.fps_visible);
 view.lifecycle.state=GKD_LIFECYCLE_STORAGE;fps_tick(&view,start+4540U);assert(!view.fps_visible);
 view.lifecycle.state=GKD_LIFECYCLE_SUSPENDED;fps_tick(&view,start+4560U);assert(!view.fps_visible);
 view.lifecycle.state=GKD_LIFECYCLE_ACTIVE;view.settings.show_fps=0;fps_tick(&view,start+4580U);assert(!view.fps_visible);
 view.settings.show_fps=1;assert(!close(life[1]));fps_tick(&view,start+5000U);
 assert(view.fps_visible&&!strcmp(osd_text,"FPS --")&&view.fps.state==GKD_APP_FPS_UNAVAILABLE);
 ++view.session.ready.host;fps_tick(&view,start+5020U);
 assert(!view.fps_visible&&view.fps.peer_pidfd<0&&view.fps.state==GKD_APP_FPS_NO_GAME);
 puts("FPS_SERVICE_PASS real-counter/only-game/disabled/menu/USB/suspend/screenshot-ttl/controls-yield/contention/restore/unsupported/session-end");
}

static void usb_edge_tests(void)
{
 struct service s;init(&s);s.usb_observed=s.usb_stable=0;
 unsigned before=osd_sends;battery_percent=76;battery_invalid=0;
 write_file(APP_USB_ONLINE,"1\n",0600);
 usb_events(&s,1000U);usb_events(&s,1400U);assert(osd_sends==before&&!s.usb_pending);
 usb_events(&s,1500U);
 assert(s.menu_usb_required&&s.menu_kind==MENU_USB&&!s.usb_pending&&
  s.lifecycle.state==GKD_LIFECYCLE_MENU_OPENING);
 assert(osd_sends==before+1U&&!strcmp(osd_text,"POWER 76%")&&osd_icon==2U&&!s.usb_notice_pending);
 uint32_t connected_pixels[GKD_UI_OSD_WIDTH*GKD_UI_OSD_HEIGHT];
 memcpy(connected_pixels,s.osd_pixels,sizeof(connected_pixels));
 usb_events(&s,1600U);assert(osd_sends==before+1U);
 /* Disconnect must not wait for a menu selection; notification retries when
  * the existing controls plane is busy. Rapid replug replaces a stale edge. */
 init(&s);s.usb_observed=s.usb_stable=1;osd_busy=1;
 write_file(APP_USB_ONLINE,"0\n",0600);
 usb_events(&s,2000U);usb_events(&s,2500U);
 assert(s.usb_notice_pending==1&&!s.usb_pending&&osd_sends==before+1U);
 osd_busy=0;usb_events(&s,2600U);
 assert(!s.usb_notice_pending&&osd_sends==before+2U&&!strcmp(osd_text,"POWER 76%")&&osd_icon==2U);
 assert(!memcmp(connected_pixels,s.osd_pixels,sizeof(connected_pixels)));
 usb_events(&s,2700U);assert(osd_sends==before+2U);
 s.lifecycle.state=GKD_LIFECYCLE_DEBUG;s.usb_notice_pending=1;
 usb_power_notice(&s);assert(s.usb_notice_pending==1);
 s.usb_notice_pending=2;s.lifecycle.state=GKD_LIFECYCLE_ACTIVE;s.transient_until=0;
 usb_power_notice(&s);assert(!s.usb_notice_pending&&!strcmp(osd_text,"POWER 76%"));
 battery_invalid=1;s.usb_notice_pending=1;s.transient_until=0;usb_power_notice(&s);
 assert(!strcmp(osd_text,"POWER --%"));battery_invalid=0;
 /* A boot power notice stays queued until the update result has been visible. */
 init(&s);assert(!result_osd(&s,1,SETTINGS_OSD_MS));before=osd_sends;
 s.usb_notice_pending=2;usb_power_notice(&s);
 assert(s.usb_notice_pending==2&&osd_sends==before&&!strcmp(osd_text,"SUCCESS"));
 s.transient_until=0;usb_power_notice(&s);
 assert(!s.usb_notice_pending&&osd_sends==before+1&&!strcmp(osd_text,"POWER 76%"));
 usb_power_notice(&s);assert(osd_sends==before+1);
 init(&s);s.menu_usb_required=1;assert(!dispatch_command(&s,"power-menu")&&!s.menu_usb_required);
 init(&s);s.menu_usb_required=1;assert(!dispatch_command(&s,"usb-menu")&&!s.menu_usb_required);
 puts("USB_POWER_EDGES=PASS debounce/insert-required/unplug/once/busy-retry/export-defer/latest-edge/manual-cancel/identical-power-pixels");
}

static void idle_menu_clock_tests(void)
{
 const char *commands[]={"settings-menu","power-menu","usb-menu"};
 write_file(APP_GENERATION,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n",0444);
 write_file(APP_MENU,"#!/bin/sh\nprintf 'GKD_MENU_READY kind=%s\\n' \"$2\"\nsleep 0.05\nprintf 'GKD_MENU_RESULT=CANCELLED kind=%s\\n' \"$2\"\n",0700);
 for(unsigned kind=0;kind<3U;kind++){
  struct service s;int pipes[3][2];init(&s);
  gkd_app_events_init(&s.events);
  for(unsigned i=0;i<3U;i++)assert(!pipe2(pipes[i],O_NONBLOCK|O_CLOEXEC));
  s.events.observer.physical_fd=pipes[0][0];s.events.observer.virtual_fd=pipes[1][0];
  s.events.power_fd=pipes[2][0];s.events_live=1;
  assert(!dispatch_command(&s,commands[kind]));operation(&s);
  uint64_t entered=now_ms();s.last_activity=entered-600000U;s.settings.auto_suspend_seconds=300U;
  /* Exclusive menu input cannot refresh the service observer. A long-open
   * menu must stay open, then a clean cancel starts a fresh idle interval. */
  input_events(&s,entered);
  assert(s.lifecycle.state==GKD_LIFECYCLE_MENU_OPENING||s.lifecycle.state==GKD_LIFECYCLE_MENU);
  reach(&s,GKD_LIFECYCLE_ACTIVE);assert(s.last_activity>=entered);
  uint64_t resumed=s.last_activity;
  input_events(&s,resumed+299999U);assert(s.lifecycle.state==GKD_LIFECYCLE_ACTIVE);
  /* A held physical key is activity even when it emits no repeat events. */
  s.events.state[0][KEY_DOWN]=1;
  input_events(&s,resumed+300000U);
  assert(s.lifecycle.state==GKD_LIFECYCLE_ACTIVE&&s.last_activity==resumed+300000U);
  s.events.state[0][KEY_DOWN]=0;resumed=s.last_activity;
  input_events(&s,resumed+299999U);assert(s.lifecycle.state==GKD_LIFECYCLE_ACTIVE);
  s.settings.auto_suspend_seconds=0;
  input_events(&s,resumed+600000U);assert(s.lifecycle.state==GKD_LIFECYCLE_ACTIVE);
  s.settings.auto_suspend_seconds=300U;
  input_events(&s,resumed+300000U);
  assert(s.power_event==GKD_LIFECYCLE_EVENT_SUSPEND&&s.lifecycle.state!=GKD_LIFECYCLE_ACTIVE);
  for(unsigned i=0;i<3U;i++){close(pipes[i][0]);close(pipes[i][1]);}
 }
 assert(!unlink(APP_GENERATION));
 puts("AUTO_SLEEP_CLOCK=PASS saved/cancel-settings-power-usb/long-menu/held-key/disabled/exact-threshold");
}

static void critical_battery_tests(void)
{
 struct service s;
 const uint64_t t=100000U;
 battery_invalid=0;battery_powered=0;battery_percent=5;
 init(&s);s.settings.battery_poll_ms=1;s.settings.battery_suspend_delay_ms=15000;
 battery(&s,t);assert(s.battery_suspend_since==t&&s.critical_notified&&osd_icon==2U&&osd_level==5&&osd_critical==1&&!strcmp(osd_text,"POWER LOW"));
 battery(&s,t+14999U);assert(s.lifecycle.state==GKD_LIFECYCLE_ACTIVE);
 battery(&s,t+15000U);
 assert(s.power_event==GKD_LIFECYCLE_EVENT_SUSPEND&&s.request.action==GKD_LIFECYCLE_ACTION_POWER_QUIESCE&&!s.battery_suspend_since);
 /* Recovery, invalid telemetry, cable power and disabled policy cancel a pending grace. */
 for(unsigned reason=0;reason<4U;reason++){
  init(&s);s.settings.battery_poll_ms=1;
  battery(&s,t);assert(s.battery_suspend_since);
  if(reason==0)battery_percent=6;
  if(reason==1)battery_invalid=1;
  if(reason==2)battery_powered=1;
  if(reason==3)s.settings.battery_suspend_enabled=0;
  battery(&s,t+20000U);
  assert(!s.battery_suspend_since&&s.lifecycle.state==GKD_LIFECYCLE_ACTIVE);
  battery_percent=5;battery_invalid=battery_powered=0;
 }
 /* Unsafe owners never start the countdown or dispatch a power action. */
 for(unsigned owner=0;owner<6U;owner++){
  init(&s);s.settings.battery_poll_ms=1;
  if(owner==0)s.lifecycle.state=GKD_LIFECYCLE_STORAGE;
  if(owner==1)s.lifecycle.state=GKD_LIFECYCLE_MENU;
  if(owner==2)s.update_requested=1;
  if(owner==3)s.settings_save_status=1;
  if(owner==4)s.trial_checked=1;
  if(owner==5)s.stopping=1;
  battery(&s,t);battery(&s,t+20000U);
  assert(!s.battery_suspend_since&&s.request.action==GKD_LIFECYCLE_ACTION_NONE);
 }
 /* Default 6-10% stays a percentage; it must not start a warning/countdown. */
 for(int level=6;level<=10;level++){
  init(&s);s.settings.battery_poll_ms=1;battery_percent=level;
  unsigned before=osd_sends;battery(&s,t);
  assert(!s.low_notified&&!s.critical_notified&&!s.battery_suspend_since&&osd_sends==before);
 }
 /* A separately configured earlier warning remains supported. */
 init(&s);s.settings.battery_low=10;s.settings.battery_poll_ms=1;battery_percent=10;
 battery(&s,t);assert(s.low_notified&&!s.critical_notified&&!s.battery_suspend_since&&osd_icon==2U&&osd_level==10&&osd_critical==1&&!strcmp(osd_text,"POWER LOW"));
 battery_percent=29;
 puts("GKD_CRITICAL_BATTERY=PASS threshold/grace/invalid/power/recovery/disabled/transaction-ownership/icon");
}
static void card_monitor_tests(void)
{
 struct service s;uint64_t generation;
 write_file(APP_USB,"#!/bin/sh\nexit 0\n",0700);
 write_file(APP_GAME_DISKSEQ,"10\n",0600);assert(!game_card_generation(&generation)&&generation==10);
 write_file(APP_GAME_DISKSEQ,"+10\n",0600);assert(game_card_generation(&generation)<0);
 write_file(APP_GAME_DISKSEQ,"10oops\n",0600);assert(game_card_generation(&generation)<0);
 write_file(APP_GAME_DISKSEQ,"10\n",0600);init(&s);game_card_poll(&s,1000);
 assert(s.card_generation==10&&!s.card_refreshing);
 assert(!unlink(APP_GAME_DISKSEQ));game_card_poll(&s,1200);
 assert(s.card_generation==10&&!s.card_refreshing);
 game_card_poll(&s,1400);assert(s.card_refreshing&&!s.card_generation&&s.request.action==GKD_LIFECYCLE_ACTION_DISPLAY_FREEZE);
 unsigned old_stops=stops,old_starts=starts;
 reach(&s,GKD_LIFECYCLE_ACTIVE);
 assert(stops==old_stops+1&&starts==old_starts&&!s.card_refreshing&&
        !s.lifecycle.app_running&&!s.lifecycle.card_present&&s.status_kind==6U);
 write_file(APP_GAME_DISKSEQ,"13\n",0600);game_card_poll(&s,1600);game_card_poll(&s,1800);
 assert(s.card_refreshing&&s.card_generation==13);
 reach(&s,GKD_LIFECYCLE_WAIT_APP_READY);
 s.session.state=GKD_SESSION_READY;reach(&s,GKD_LIFECYCLE_ACTIVE);
 assert(starts==old_starts+1&&s.lifecycle.app_ready&&s.lifecycle.card_present);
 gkd_app_events_close(&s.events);

 init(&s);write_file(APP_GAME_DISKSEQ,"11\n",0600);game_card_poll(&s,1000);
 write_file(APP_GAME_DISKSEQ,"12\n",0600);s.lifecycle.state=GKD_LIFECYCLE_STORAGE;s.lifecycle.usb=GKD_LIFECYCLE_USB_GAME;
 game_card_poll(&s,1200);game_card_poll(&s,1400);assert(!s.card_refreshing&&s.card_generation==11);
 s.lifecycle.usb=GKD_LIFECYCLE_USB_NONE;s.lifecycle.state=GKD_LIFECYCLE_RECOVERY;s.lifecycle.failed_action=GKD_LIFECYCLE_ACTION_GAME_MOUNT;
 game_card_poll(&s,1600);assert(s.card_refreshing&&s.card_generation==12);
 assert(!unlink(APP_GAME_DISKSEQ));
 puts("GKD_CARD_MONITOR=PASS absence/reinsert/debounce/export-defer/recovery/strict-generation");
}
static void card_page_tests(void)
{
 real_card_lease=1;
 struct service s;init(&s);s.card_initialized=1;s.card_generation=0;
 s.usb_stable=1;s.charge_until=999999U;
 unsigned pages=card_pages,sends=status_sends,hides=status_hides;
 assert(!status_screen(&s,1000)&&s.status_kind==6U&&s.events.exclusive);
 assert(card_pages==pages+1U&&!strcmp(card_body,"INSERT GAME CARD"));
 assert(!status_screen(&s,1400)&&status_sends==sends+1U);
 assert(!status_screen(&s,1500)&&status_sends==sends+2U);
 /* The same page is replaced in place while the asynchronous media flow runs. */
 s.card_refreshing=1;
 assert(!gkd_app_lifecycle_event(&s.lifecycle,GKD_LIFECYCLE_EVENT_CARD_REFRESH,0));
 unsigned spinning=loading_draws;
 assert(!status_screen(&s,1600)&&s.status_kind==7U&&loading_draws==spinning+1U);
 assert(status_hides==hides&&s.events.exclusive);
 uint64_t deadline=now_ms()+4000U;
 while(s.lifecycle.state!=GKD_LIFECYCLE_WAIT_APP_READY){
  assert(now_ms()<deadline);session(&s);jobs(&s);operation(&s);
  assert(!s.terminal_error);assert(!status_screen(&s,now_ms()));usleep(1000);
 }
 assert(s.card_refreshing&&status_hides==hides&&s.notice_controls_pending&&s.events.exclusive&&s.events.observer.menu_guard.lease_fd==-1);
 sends=status_sends;assert(!status_screen(&s,s.status_renew)&&status_sends==sends+1U);
 s.card_generation=12;s.session.state=GKD_SESSION_READY;
 session(&s);assert(!s.notice_controls_pending&&s.events.exclusive&&s.events.observer.menu_guard.lease_fd>=0);
 s.charge_until=0;reach(&s,GKD_LIFECYCLE_ACTIVE);
 assert(!s.card_refreshing&&!s.status_kind&&!s.events.exclusive);
 /* No-card cold boot owns the page without starting the card-dependent frontend. */
 init(&s);s.boot_ready=0;s.events_live=0;s.card_initialized=1;
 assert(!status_screen(&s,6000)&&s.status_kind==6U&&s.events.exclusive);
 assert(!start_without_frontend(&s));
 assert(s.boot_ready&&s.events.exclusive&&!s.terminal_error&&
        !s.lifecycle.app_running&&!s.lifecycle.card_present);
 struct gkd_menu_guard_controls startup;uint64_t epoch;gkd_menu_guard_controls_init(&startup);
 assert(gkd_menu_guard_controls_acquire(&startup,&epoch)==1);
 /* Explicit menu entry can release the page; A/B on the page have no handler. */
 s.charge_until=0;unsigned old_closes=event_closes;
 write_file(APP_MENU,"#!/bin/sh\nprintf 'GKD_MENU_READY kind=POWER\\n'\nsleep 0.02\nprintf 'GKD_MENU_RESULT=CANCELLED kind=POWER\\n'\n",0700);
 assert(!dispatch_command(&s,"power-menu"));
 reach(&s,GKD_LIFECYCLE_ACTIVE);assert(event_closes>old_closes&&!s.terminal_error);
 /* No frontend means no application namespace for optional ICS jobs. A stale
  * desire must never reopen the stopped init process after card removal. */
 assert(!s.lifecycle.app_running&&!s.network_desired);
 s.network_desired=1;network_poll(&s,now_ms()+3000U);
 assert(!s.network_running&&!s.network_dirty&&s.network_etc==-1);
 gkd_app_events_close(&s.events);
 /* The disabled page must not disable the existing inactivity timer. */
 init(&s);gkd_app_events_init(&s.events);int pipes[3][2];
 for(unsigned i=0;i<3U;i++)assert(!pipe2(pipes[i],O_NONBLOCK|O_CLOEXEC));
 s.events.observer.physical_fd=pipes[0][0];s.events.observer.virtual_fd=pipes[1][0];
 s.events.power_fd=pipes[2][0];s.events_live=1;s.card_initialized=1;
 assert(!status_screen(&s,7000)&&s.status_kind==6U);
 s.settings.auto_suspend_seconds=300U;s.last_activity=7000U;
 input_events(&s,306999U);assert(s.lifecycle.state==GKD_LIFECYCLE_ACTIVE&&!s.terminal_error);
 input_events(&s,307000U);
 assert(s.power_event==GKD_LIFECYCLE_EVENT_SUSPEND&&s.lifecycle.state!=GKD_LIFECYCLE_ACTIVE&&!s.terminal_error);
 for(unsigned i=0;i<3U;i++){close(pipes[i][0]);close(pipes[i][1]);}
 gkd_app_events_close(&s.events);
 /* A persistent no-card page cannot defer critical battery protection. */
 init(&s);s.card_initialized=1;assert(!status_screen(&s,8000)&&s.status_kind==6U);
 battery_percent=5;battery_powered=0;s.settings.battery_poll_ms=1;
 s.settings.battery_suspend_delay_ms=15000U;
 battery(&s,8000);assert(s.battery_suspend_since==8000U&&s.critical_notified);
 battery(&s,22999);assert(s.lifecycle.state==GKD_LIFECYCLE_ACTIVE);
 battery(&s,23000);assert(s.power_event==GKD_LIFECYCLE_EVENT_SUSPEND&&!s.battery_suspend_since);
 battery_percent=29;gkd_app_events_close(&s.events);
 real_card_lease=0;
 puts("GKD_CARD_PAGE=PASS shared-text/disabled-actions/renewal/no-charge-overwrite/refresh-no-hide/cold-boot-owner/menu-release");
}
static void usb_loading_tests(void)
{
 write_file(APP_USB,"#!/bin/sh\nif [ \"$1\" = save ]; then\n [ \"$2\" = 3 ] && [ \"${#3}\" = 64 ] || exit 2\n sleep 0.05\n if [ \"${GKD_CONFIG_UNKNOWN-0}\" = 1 ]; then printf 'GKD_APP_SETTINGS=FAILED state=unknown errno=5\\n'; exit 1; fi\n if [ \"${GKD_USB_FAIL-0}\" = 1 ]; then printf 'GKD_APP_SETTINGS=FAILED state=recoverable errno=5\\n'; exit 1; fi\n printf 'GKD_APP_SETTINGS=SAVED generation=%s\\n' \"$3\"; exit 0\nfi\n[ \"${GKD_USB_FAIL-0}\" != 1 ] || exit 1\nprintf 'USB=PASS mode=%s\\n' \"$1\"\n",0700);
 struct service s;
 real_card_lease=1;
 write_file(APP_MENU,"#!/bin/sh\nprintf 'GKD_MENU_READY kind=USB\\n'\nsleep 0.01\nprintf 'GKD_MENU_RESULT=SELECTED kind=USB index=%s\\n' \"$GKD_MENU_CHOICE\"\n",0700);
 for(unsigned mode=1;mode<=2;mode++){
  init(&s);write_file(APP_GAME_DISKSEQ,"12\n",0600);s.card_initialized=1;s.card_generation=12;
  s.settings.effects=mode==1;s.settings.chinese=mode==2;s.cn_font=test_font;
  assert(!setenv("GKD_MENU_CHOICE",mode==1?"1":"2",1));
  unsigned pages=loading_draws;
  assert(!dispatch_command(&s,"usb-menu"));
  reach(&s,mode==1?GKD_LIFECYCLE_STORAGE:GKD_LIFECYCLE_DEBUG);
  assert(loading_draws>pages);
  assert(s.status_kind==mode&&!s.card_refreshing&&!s.terminal_error);
  pages=loading_draws;
  assert(!dispatch_command(&s,"return"));
  reach(&s,GKD_LIFECYCLE_WAIT_APP_READY);
  assert(s.status_kind==8U&&s.notice_controls_pending&&s.events.exclusive);
  assert(s.events.observer.menu_guard.lease_fd==-1);
  unsigned sends=status_sends;
  assert(!status_screen(&s,s.status_renew)&&status_sends==sends+1);
  struct gkd_menu_guard_controls controls;uint64_t epoch;
  gkd_menu_guard_controls_init(&controls);
  assert(!gkd_menu_guard_controls_acquire(&controls,&epoch));
  s.session.state=GKD_SESSION_READY;session(&s);
  assert(s.notice_controls_pending&&!s.terminal_error);
  gkd_menu_guard_controls_release(&controls);session(&s);
  assert(!s.notice_controls_pending&&s.events.exclusive&&s.events.observer.menu_guard.lease_fd>=0);
  reach(&s,GKD_LIFECYCLE_ACTIVE);
  assert(loading_draws>pages);
  assert(!s.status_kind&&!s.events.exclusive&&!s.notice_controls_pending&&!s.terminal_error);
  gkd_app_events_close(&s.events);
 }
 /* Export failure replaces LOADING with the existing failed-action page. */
 init(&s);assert(!setenv("GKD_MENU_CHOICE","1",1));assert(!setenv("GKD_USB_FAIL","1",1));
 assert(!dispatch_command(&s,"usb-menu"));reach(&s,GKD_LIFECYCLE_RECOVERY);
 assert(s.status_kind==3U&&!gkd_app_lifecycle_usb_transition(&s.lifecycle));
 gkd_app_events_close(&s.events);assert(!unsetenv("GKD_USB_FAIL"));real_card_lease=0;
 puts("GKD_USB_LOADING=PASS storage-enter/leave/debug-enter/leave/READY-renewal/real-startup-lock/existing-spinner/animated-pixels/failure-replacement");
}
static void waiting_cleanup_tests(void)
{
 struct service s;init(&s);struct ucred peer={.pid=getpid(),.uid=0};
 assert(game_wait(&s,&peer,1)<0&&errno==EPERM&&!s.game_wait_peer);
 trusted_wait_peer=peer.pid;
 s.settings.effects=1;s.status_kind=5U;s.status_expires=now_ms()+2000U;
 unsigned cleared=osd_clears;
 assert(!game_wait(&s,&peer,1)&&s.status_kind==11U&&!s.events.exclusive);
 int pin=s.game_wait_pin;
 assert(loading_draws&&status_transition==0U&&osd_clears==cleared+1);
 unsigned spins=loading_draws,frame=loading_frame;
 uint16_t before_pixels[GKD_UI_WIDTH*GKD_UI_HEIGHT];memcpy(before_pixels,s.status_pixels,sizeof(before_pixels));
 assert(!status_screen(&s,s.status_renew-1U)&&loading_draws==spins);
 assert(!status_screen(&s,s.status_renew)&&loading_draws==spins+1U&&loading_frame!=frame);
 assert(memcmp(before_pixels,s.status_pixels,sizeof(before_pixels)));
 for(unsigned step=2;step<=8U;step++){
  memcpy(before_pixels,s.status_pixels,sizeof(before_pixels));
  assert(!status_screen(&s,s.status_renew)&&loading_draws==spins+step);
  assert(memcmp(before_pixels,s.status_pixels,sizeof(before_pixels)));
 }
 assert(game_wait(&s,&peer,1)<0&&errno==EBUSY&&s.game_wait_pin==pin);
 struct ucred other=peer;other.pid++;
 assert(game_wait(&s,&other,0)<0&&errno==EPERM&&s.game_wait_pin==pin);
 assert(!game_wait(&s,&peer,0)&&!s.game_wait_peer);
 assert(fcntl(pin,F_GETFD)<0&&errno==EBADF);
 assert(!status_screen(&s,now_ms())&&!s.status_kind&&!s.status_hiding);
 /* Fast work must never disappear inside an unfinished entrance animation. */
 assert(!game_wait(&s,&peer,1)&&s.status_kind==11U&&!s.status_hiding);
 assert(!game_wait(&s,&peer,0));
 assert(!status_screen(&s,now_ms()));assert(!status_screen(&s,now_ms()+1000)&&!s.status_kind);
 pid_t child=fork();assert(child>=0);if(!child){for(;;)pause();}
 trusted_wait_peer=peer.pid=child;
 assert(!game_wait(&s,&peer,1));pin=s.game_wait_pin;
 assert(!kill(child,SIGKILL));assert(waitpid(child,NULL,0)==child);
 assert(!status_screen(&s,now_ms())&&!s.game_wait_peer);
 assert(fcntl(pin,F_GETFD)<0&&errno==EBADF);
 assert(!status_screen(&s,now_ms()+1000)&&!s.status_kind);
 trusted_wait_peer=peer.pid=getpid();
 assert(!game_wait(&s,&peer,1));
 assert(!game_wait(&s,&peer,-1)&&!s.game_wait_peer&&!strcmp(osd_text,"FAILED")&&osd_critical);
 assert(!status_screen(&s,now_ms()));assert(!status_screen(&s,now_ms()+1000)&&!s.status_kind);
 trusted_wait_peer=0;
 /* Screenshot progress begins only after the worker has captured pixels. */
 init(&s);write_file(APP_GAME,"#!/bin/sh\nsleep 0.05\nprintf 'GKD_SCREENSHOT_CAPTURED\\n'\nsleep 0.08\nprintf 'GKD_SCREENSHOT_RESULT=success filename=/media/sdcard/screenshots/test.png\\n'\n",0700);
 char *argv[]={APP_GAME,NULL};assert(!job_start(&s,argv,5000U,JOB_SCREENSHOT));
 unsigned before=osd_sends;jobs(&s);assert(osd_sends==before);
 uint64_t until=now_ms()+2000;
 unsigned shot_spins=loading_draws;
 while(s.purpose!=JOB_NONE&&now_ms()<until){jobs(&s);assert(!status_screen(&s,now_ms()));if(s.job_osd_renew)assert(s.status_kind==12U);usleep(1000);}
 assert(loading_draws>shot_spins&&!s.status_kind);
 assert(s.purpose==JOB_NONE&&osd_sends>before&&!s.job_osd_renew);
 assert(!strcmp(osd_text,"SHOT SAVED")&&osd_icon==3U&&!osd_critical&&!s.terminal_error);
 puts("GKD_WAIT_CLEANUP=PASS trusted-peer-only/pidfd-death/foreign-end/reentrant-reject/rapid-repeat/no-input-lock/screenshot-capture-before-progress");
}
static void boot_update_loading_tests(void)
{
 struct service s;
 const int exits[]={0,10,20,1};
 write_file(APP_FB,"",0600);
 for(unsigned n=0;n<sizeof(exits)/sizeof(exits[0]);n++){
  char body[160];snprintf(body,sizeof(body),"#!/bin/sh\n[ \"$1\" = boot ] || exit 99\nsleep %s\nexit %d\n",n?"0.02":"2",exits[n]);
  write_file(APP_UPDATE,body,0700);memset(&s,0,sizeof(s));
  unsigned frames=loading_draws,failures=osd_sends,clears=status_clears;
  if(exits[n]==1)assert(!rename(APP_CONFIG,APP_CONFIG ".boot-test-save"));
  assert(update_boot_loading(&s)==exits[n]);
  if(exits[n]==1)assert(!rename(APP_CONFIG ".boot-test-save",APP_CONFIG));
  assert(loading_draws>frames&&status_clears==clears+1);
  if(!n)assert(loading_draws>=frames+8);
  assert(osd_sends==failures+(exits[n]==1));
  if(exits[n]==1)assert(osd_icon==GKD_UI_OSD_ICON_FAILURE&&!strcmp(osd_text,"FAILED"));
 }
 /* Missing UI must not suppress or cancel the update's control exit code. */
 write_file(APP_UPDATE,"#!/bin/sh\nexit 20\n",0700);memset(&s,0,sizeof(s));boot_display_error=1;
 assert(update_boot_loading(&s)==20);boot_display_error=0;
 /* A termination signal to the renderer waits for the armed child. */
 write_file(APP_UPDATE,"#!/bin/sh\nkill -TERM \"$PPID\"\nsleep 0.02\nexit 10\n",0700);memset(&s,0,sizeof(s));
 assert(update_boot_loading(&s)==10&&!interrupted);
 init(&s);s.purpose=JOB_UPDATE_GOOD;unsigned frames=loading_draws;
 assert(!status_screen(&s,now_ms())&&s.status_kind==4&&loading_draws>frames);
 s.purpose=JOB_NONE;assert(!status_screen(&s,now_ms())&&!s.status_kind);
 gkd_app_events_close(&s.events);
 assert(!unlink(APP_UPDATE));assert(!unlink(APP_FB));
 puts("BOOT_UPDATE_LOADING_PASS shared-animation/exit0-10-20/failure-OSD-without-settings/display-failure-keeps-child/TERM-waits/trial-good-loading");
}
int main(void)
{
 struct service s;
 assert(!gkd_ui_font_load(&test_font,"/out/fallback.psf"));
 write_file(APP_MENU,"#!/bin/sh\nsleep \"${GKD_MENU_READY_DELAY-0}\"\nprintf 'GKD_MENU_READY kind=%s\\n' \"$2\"\ntrap 'printf \"GKD_MENU_RESULT=CANCELLED kind=%s\\n\" \"$2\"; exit 0' TERM\nsleep \"${GKD_MENU_RESULT_DELAY-0.01}\"\nprintf 'GKD_MENU_RESULT=SELECTED kind=%s index=%s\\n' \"$2\" \"$GKD_MENU_CHOICE\"\n",0700);
 write_file(APP_USB,"#!/bin/sh\nif [ \"$1\" = save ]; then\n [ \"$2\" = 3 ] && [ \"${#3}\" = 64 ] || exit 2\n sleep 0.05\n if [ \"${GKD_CONFIG_UNKNOWN-0}\" = 1 ]; then printf 'GKD_APP_SETTINGS=FAILED state=unknown errno=5\\n'; exit 1; fi\n if [ \"${GKD_USB_FAIL-0}\" = 1 ]; then printf 'GKD_APP_SETTINGS=FAILED state=recoverable errno=5\\n'; exit 1; fi\n printf 'GKD_APP_SETTINGS=SAVED generation=%s\\n' \"$3\"; exit 0\nfi\n[ \"${GKD_USB_FAIL-0}\" != 1 ] || exit 1\nprintf 'USB=PASS mode=%s\\n' \"$1\"\n",0700);
 write_file(APP_LUN,"\n",0600);
 init(&s);assert(!setenv("GKD_MENU_CHOICE","1",1));assert(!dispatch_command(&s,"usb-menu"));
 reach(&s,GKD_LIFECYCLE_STORAGE);assert(!status_screen(&s,now_ms()));assert(!status_draws&&status_sends>0&&osd_sends==1&&osd_icon==6U&&!strcmp(osd_text,"STORAGE"));
 assert(stops==1&&cleaned==1&&freezes==1&&!pauses&&!unmounts);
 assert(s.session.pid<0&&!s.media.paused);
 assert(!dispatch_command(&s,"return"));reach(&s,GKD_LIFECYCLE_WAIT_APP_READY);
 assert(starts==1&&!thaws&&s.session_generation==2);
 s.session.state=GKD_SESSION_READY;reach(&s,GKD_LIFECYCLE_ACTIVE);
 assert(status_hides==0&&status_clears>0&&!s.status_kind&&status_transition==0U&&thaws==1);
 assert(!setenv("GKD_MENU_CHOICE","2",1));assert(!dispatch_command(&s,"usb-menu"));
 reach(&s,GKD_LIFECYCLE_DEBUG);assert(stops==2&&cleaned==2&&freezes==2&&starts==1&&thaws==1);
 assert(!dispatch_command(&s,"return"));reach(&s,GKD_LIFECYCLE_WAIT_APP_READY);
 assert(starts==2&&validated==1&&thaws==1&&s.session_generation==3);
 s.session.state=GKD_SESSION_READY;reach(&s,GKD_LIFECYCLE_ACTIVE);assert(thaws==2);
 assert(dispatch_command(&s,"arbitrary-shell")<0&&errno==EINVAL);
 /* A running game holds the launcher lock: storage must fail before the
  * display freezes or the frontend stops, then return to the same app. */
 struct service busy_storage;init(&busy_storage);
 unsigned stops_before_busy=stops,freezes_before_busy=freezes;
 idle_result=GKD_APP_IDLE_BUSY;assert(!setenv("GKD_MENU_CHOICE","1",1));
 assert(!dispatch_command(&busy_storage,"usb-menu"));
 reach(&busy_storage,GKD_LIFECYCLE_RECOVERY);
 assert(busy_storage.lifecycle.failed_action==GKD_LIFECYCLE_ACTION_GAME_IDLE_CHECK);
 assert(stops==stops_before_busy&&freezes==freezes_before_busy&&busy_storage.lifecycle.app_running);
 assert(busy_storage.storage_busy_recover_at&&
        busy_storage.storage_busy_recover_at!=UINT64_MAX);
 storage_busy_recover(&busy_storage,busy_storage.storage_busy_recover_at);
 reach(&busy_storage,GKD_LIFECYCLE_ACTIVE);
 assert(!busy_storage.storage_busy_recover_at);
 assert(stops==stops_before_busy&&busy_storage.lifecycle.app_running);
 idle_result=GKD_APP_IDLE_ACQUIRED;
 /* A failed recovery-screen transfer keeps the POWER_QUIESCE guard; a
  * completed transfer releases it exactly once when ACTIVE resumes. */
 struct service recovery;init(&recovery);
 int recovery_guard=open("/dev/null",O_RDONLY|O_CLOEXEC);assert(recovery_guard>=0);
 recovery.power_guard.lease_fd=recovery_guard;
 recovery.lifecycle.state=GKD_LIFECYCLE_RECOVERY;event_reuse_failure=1;
 assert(status_screen(&recovery,1000U)<0&&errno==EIO&&
  recovery.power_guard.lease_fd==recovery_guard&&!recovery.events.exclusive);
 event_reuse_failure=0;
 assert(!status_screen(&recovery,1000U)&&recovery.events.exclusive&&
  recovery.power_guard.lease_fd==-1&&recovery.events.observer.menu_guard.lease_fd==recovery_guard);
 assert(!strcmp(osd_text,"FAILED")&&osd_icon==GKD_UI_OSD_ICON_FAILURE&&osd_critical&&recovery.status_kind==3U);
 unsigned closes_before=event_closes;
 recovery.lifecycle.state=GKD_LIFECYCLE_ACTIVE;
 assert(!status_screen(&recovery,1001U));
 assert(!status_screen(&recovery,1241U)&&!recovery.events.exclusive&&
  event_closes==closes_before+1U);
 errno=0;assert(fcntl(recovery_guard,F_GETFD)<0&&errno==EBADF);
 /* POWER_RESUME reaches ACTIVE with an already-transferred recovery guard. */
 init(&recovery);recovery_guard=open("/dev/null",O_RDONLY|O_CLOEXEC);assert(recovery_guard>=0);
 recovery.events.exclusive=1;recovery.events.observer.menu_guard.lease_fd=recovery_guard;
 recovery.lifecycle.state=GKD_LIFECYCLE_SUSPENDED;recovery.lifecycle.power_quiesced=1;
 recovery.lifecycle.app_paused=1;recovery.media.paused=1;recovery.media.mounted=1;
 assert(!gkd_app_lifecycle_event(&recovery.lifecycle,GKD_LIFECYCLE_EVENT_RETURN,0));
 operation(&recovery);
 assert(recovery.lifecycle.state==GKD_LIFECYCLE_ACTIVE&&!recovery.events.exclusive&&
  recovery.power_guard.lease_fd==-1);
 errno=0;assert(fcntl(recovery_guard,F_GETFD)<0&&errno==EBADF);
 /* Actual renderer, fixed lease renewal and same-owner expiry. */
 struct service view;init(&view);
 unsigned full_before=status_sends,draw_before=osd_draws,send_before=osd_sends;
 view.lifecycle.state=GKD_LIFECYCLE_DEBUG;assert(!status_screen(&view,1000U));
 assert(osd_icon==7U&&!strcmp(osd_text,"DEBUG")&&osd_level==-1&&osd_ttl==2000U&&osd_fade==160U);
 unsigned sequence=osd_last_sequence;
 for(uint64_t t=1100U;t<=6100U;t+=100U)assert(!status_screen(&view,t));
 assert(osd_sends==send_before+11U&&osd_draws==draw_before+1U&&osd_last_sequence>sequence&&status_sends==full_before);
 view.lifecycle.state=GKD_LIFECYCLE_ACTIVE;
 assert(!status_screen(&view,6150U)&&view.status_hiding==6310U&&osd_ttl==160U);
 assert(!status_screen(&view,6310U)&&!view.status_kind&&!view.events.exclusive);
 init(&view);view.usb_stable=1;view.charge_until=2600U;battery_percent=76;
 send_before=osd_sends;assert(!status_screen(&view,1000U));
 assert(!strcmp(osd_text,"POWER 76%")&&osd_icon==2U&&osd_level==76&&osd_ttl==1600U&&!view.events.exclusive);
 for(uint64_t t=1100U;t<=5000U;t+=100U)assert(!status_screen(&view,t));
 assert(osd_sends==send_before+1U&&!view.status_kind);
 for(int level=0;level<=100;level++){
  init(&view);view.usb_stable=1;view.charge_until=2600U;battery_percent=level;
  assert(!status_screen(&view,1000U)&&osd_level==level);
  char expected[24];
  if(level<=5)snprintf(expected,sizeof(expected),"POWER LOW");
  else snprintf(expected,sizeof(expected),"POWER %d%%",level);
  assert(!strcmp(osd_text,expected));
 }
 init(&view);view.usb_stable=1;view.charge_until=2600U;battery_invalid=1;
 assert(!status_screen(&view,1000U)&&!strcmp(osd_text,"POWER --%")&&osd_level==-1);
 battery_invalid=0;battery_percent=29;
 init(&view);view.usb_stable=1;view.charge_until=2600U;osd_busy=1;
 unsigned clears_before=osd_clears;send_before=osd_sends;
 assert(!status_screen(&view,1000U)&&!view.status_kind);
 unsigned busy_draws=osd_draws;
 assert(!status_screen(&view,1020U)&&osd_draws==busy_draws);
 assert(!status_screen(&view,1100U)&&osd_draws==busy_draws+1U);
 assert(!status_screen(&view,2590U)&&!view.status_kind);
 osd_busy=0;assert(!status_screen(&view,2700U));
 assert(osd_sends==send_before&&osd_clears==clears_before);
 view.lifecycle.state=GKD_LIFECYCLE_STORAGE;osd_busy=1;
 assert(!status_screen(&view,3000U)&&!view.status_kind);
 osd_busy=0;assert(!status_screen(&view,3500U)&&view.status_kind==1U);
 osd_error=1;assert(status_screen(&view,4000U)<0&&errno==EIO);osd_error=0;
 init(&view);view.lifecycle.state=GKD_LIFECYCLE_STORAGE;view.usb_observed=view.usb_stable=1;
 assert(!status_screen(&view,1000U));write_file(APP_USB_ONLINE,"0\n",0600);
 usb_events(&view,1100U);usb_events(&view,1500U);assert(view.lifecycle.state==GKD_LIFECYCLE_STORAGE);
 usb_events(&view,1600U);assert(view.request.action==GKD_LIFECYCLE_ACTION_USB_CLEAR_LUN&&!view.charge_until);
 assert(hide_status(&view,1600U)==1&&hide_status(&view,1760U)==0);
 init(&view);view.usb_stable=1;view.charge_until=2600U;assert(!status_screen(&view,1000U));
 assert(!osd(&view,"SHOT SAVED",3U,-1,0,1000U)&&!view.charge_until&&!view.status_kind);
 send_before=osd_sends;assert(!status_screen(&view,1500U)&&osd_sends==send_before);
 FILE *config=fopen(APP_CONFIG,"r");assert(config);char contents[65536];
 size_t bytes=fread(contents,1,sizeof(contents)-1U,config);assert(feof(config)&&!ferror(config));fclose(config);contents[bytes]=0;
 char *effect=strstr(contents,"ui_dynamic_effects=enabled\n");assert(effect);
 char disabled[65536];size_t prefix=(size_t)(effect-contents);
 memcpy(disabled,contents,prefix);disabled[prefix]=0;strcat(disabled,"ui_dynamic_effects=disabled\n");strcat(disabled,effect+strlen("ui_dynamic_effects=enabled\n"));
 write_file(APP_CONFIG,disabled,0600);
 init(&view);view.lifecycle.state=GKD_LIFECYCLE_DEBUG;assert(!status_screen(&view,1000U)&&osd_fade==0U);
 clears_before=osd_clears;assert(!hide_status(&view,1100U)&&osd_clears==clears_before+1U&&!view.status_kind);
 write_file(APP_CONFIG,contents,0600);
 init(&view);view.usb_stable=1;view.charge_until=now_ms()+1600U;
 assert(!status_screen(&view,now_ms()));
 clears_before=osd_clears;
 service_control(&view,"osd-yield","GKD_APPLICATION_COMMAND=ACCEPTED errno=0\n");
 assert(!view.charge_until&&!view.status_kind&&osd_clears==clears_before+1U&&view.button_osd_until>now_ms());
 assert(osd(&view,"BAT LOW",2U,10,0,1000U)<0&&errno==EBUSY);
 clears_before=osd_clears;
 fire_and_forget_control(&view,"osd-yield");
 assert(osd_clears==clears_before+1U&&view.button_osd_until>now_ms());
 send_before=osd_sends;assert(!status_screen(&view,now_ms())&&osd_sends==send_before);
 view.lifecycle.state=GKD_LIFECYCLE_DEBUG;
 assert(dispatch_command(&view,"osd-yield")<0&&errno==EBUSY&&osd_clears==clears_before+1U);
 puts("GKD_APP_USB_OSD=PASS actual-renderer/renew/charge-once/battery/contention/detach/effects");

 assert(!setenv("GKD_MENU_READY_DELAY","0.05",1));assert(!setenv("GKD_MENU_RESULT_DELAY","0.2",1));
 unsigned before_unmount=unmounts;
 init(&s);assert(!setenv("GKD_MENU_CHOICE","1",1));assert(!dispatch_command(&s,"usb-menu"));operation(&s);
 assert(s.lifecycle.state==GKD_LIFECYCLE_MENU_OPENING&&!s.menu_ready&&s.purpose==JOB_MENU);
 s.stopping=1;assert(!finish_stopping(&s));assert(!s.terminal_error&&unmounts==before_unmount&&s.stopped_host==901);
 assert(!setenv("GKD_MENU_READY_DELAY","0",1));
 init(&s);assert(!dispatch_command(&s,"usb-menu"));operation(&s);
 uint64_t menu_deadline=now_ms()+4000;
 while(!s.menu_ready&&now_ms()<menu_deadline){jobs(&s);usleep(1000);}
 assert(s.menu_ready&&s.lifecycle.state==GKD_LIFECYCLE_MENU&&s.purpose==JOB_MENU);
 s.stopping=1;assert(!finish_stopping(&s));assert(!s.terminal_error&&unmounts==before_unmount&&s.stopped_host==901);
 assert(!unsetenv("GKD_MENU_READY_DELAY"));assert(!unsetenv("GKD_MENU_RESULT_DELAY"));
 assert(!control_reply("usb-menu","GKD_APPLICATION_COMMAND=ACCEPTED errno=0\n"));
 assert(control_reply("usb-menu","OK\n"));
 assert(control_reply("usb-menu",""));
 assert(control_reply("usb-menu","GKD_APPLICATION_COMMAND=ACCEPTED errno=0\nEXTRA\n"));
 unsigned before_stop=stops,before_clean=cleaned;
 init(&s);s.boot_ready=0;s.session.state=GKD_SESSION_STARTING;s.session.ready.host=0;s.stopping=1;
 before_stop=stops;before_clean=cleaned;assert(!finish_stopping(&s));
 assert(s.stopped_host==901&&stops==before_stop+1&&cleaned==before_clean+1&&!s.terminal_error);
 init(&s);assert(!setenv("GKD_MENU_CHOICE","1",1));assert(!setenv("GKD_USB_FAIL","1",1));
 assert(!dispatch_command(&s,"usb-menu"));reach(&s,GKD_LIFECYCLE_RECOVERY);
 assert(!s.media.paused&&!s.lifecycle.app_running&&s.lifecycle.last_error);
 assert(!unsetenv("GKD_USB_FAIL"));
 unsigned freezes_before=freezes,thaws_before=thaws;
 init(&s);stop_failure=1;assert(!setenv("GKD_MENU_CHOICE","2",1));assert(!dispatch_command(&s,"usb-menu"));
 reach(&s,GKD_LIFECYCLE_RECOVERY);assert(s.lifecycle.failed_action==GKD_LIFECYCLE_ACTION_APP_STOP);
 assert(freezes==freezes_before+1U&&thaws==thaws_before);
 stop_failure=0;
 before_stop=stops;before_clean=cleaned;
 cleanup_failures=1;init(&s);assert(!setenv("GKD_MENU_CHOICE","2",1));assert(!dispatch_command(&s,"usb-menu"));
 reach(&s,GKD_LIFECYCLE_RECOVERY);
 assert(s.lifecycle.failed_action==GKD_LIFECYCLE_ACTION_APP_STOP&&s.session.pid<0&&stops==before_stop+1&&cleaned==before_clean+1);
 assert(!dispatch_command(&s,"retry"));reach(&s,GKD_LIFECYCLE_DEBUG);
 assert(stops==before_stop+1&&cleaned==before_clean+2);
 init(&s);s.power_event=GKD_LIFECYCLE_EVENT_REBOOT;
 int guard_fd=open("/dev/null",O_RDONLY|O_CLOEXEC);assert(guard_fd>=0);s.power_guard.lease_fd=guard_fd;
 before_stop=stops;before_clean=cleaned;cleanup_failures=1;
 assert(!gkd_app_lifecycle_event(&s.lifecycle,GKD_LIFECYCLE_EVENT_REBOOT,0));
 assert(s.request.action==GKD_LIFECYCLE_ACTION_POWER_QUIESCE);
 operation(&s);assert(s.session.pid<0&&stops==before_stop+1&&s.power_guard.lease_fd==-1&&s.events.observer.menu_guard.lease_fd==guard_fd);
 session(&s);
 assert(s.lifecycle.state==GKD_LIFECYCLE_RECOVERY&&s.lifecycle.failed_action==GKD_LIFECYCLE_ACTION_POWER_QUIESCE);
 assert(cleaned==before_clean+1&&s.power_guard.lease_fd==-1&&s.events.observer.menu_guard.lease_fd==guard_fd&&!s.terminal_error);
 assert(!status_screen(&s,now_ms())&&s.events.exclusive&&s.power_guard.lease_fd==-1&&
  s.events.observer.menu_guard.lease_fd==guard_fd&&!s.terminal_error);
 assert(hide_status(&s,now_ms())==1&&hide_status(&s,now_ms()+1000U)==0);
 assert(!dispatch_command(&s,"retry"));operation(&s);
 assert(stops==before_stop+1&&cleaned==before_clean+2&&s.lifecycle.power_quiesced);
 assert(s.request.action==GKD_LIFECYCLE_ACTION_POWER_CARD_RELEASE&&s.events.exclusive&&
  s.events.observer.menu_guard.lease_fd==guard_fd&&!s.terminal_error);
 gkd_app_events_close(&s.events);errno=0;assert(fcntl(guard_fd,F_GETFD)<0&&errno==EBADF);
 init(&s);
 write_file(APP_GAME,"#!/bin/sh\nprintf 'ACTIVE\\n'\n",0700);
 assert(!game_job(&s,0));
 while(s.purpose==JOB_GAME_CHECK){jobs(&s);usleep(1000);}
 assert(s.purpose==JOB_GAME_EXIT);
 while(s.purpose!=JOB_NONE){jobs(&s);usleep(1000);}
 assert(s.lifecycle.state==GKD_LIFECYCLE_ACTIVE&&!s.terminal_error);
 write_file(APP_GAME,"#!/bin/sh\ncase \"$1\" in check) printf 'ACTIVE\\n';; exit) printf 'EXITED\\n';; esac\n",0700);
 assert(!game_job(&s,0));while(s.purpose!=JOB_NONE){jobs(&s);usleep(1000);}
 assert(s.lifecycle.state==GKD_LIFECYCLE_ACTIVE&&s.session.ready.application==903);
 assert(s.last_activity&&!s.terminal_error);
 write_file(APP_GAME,"#!/bin/sh\nprintf 'STALE\\n'\nexit 1\n",0700);
 assert(!game_job(&s,0));while(s.purpose!=JOB_NONE){jobs(&s);usleep(1000);}
 assert(s.lifecycle.state==GKD_LIFECYCLE_ACTIVE&&s.request.action==GKD_LIFECYCLE_ACTION_NONE);
 write_file(APP_GAME,"#!/bin/sh\nprintf 'IDLE\\n'\n",0700);
 assert(!game_job(&s,0));while(s.purpose!=JOB_NONE){jobs(&s);usleep(1000);}
 assert(s.lifecycle.state==GKD_LIFECYCLE_MENU_OPENING&&s.menu_kind==1);
 init(&s);s.usb_stable=1;assert(!setenv("GKD_MENU_CHOICE","0",1));
 assert(!dispatch_command(&s,"usb-menu"));reach(&s,GKD_LIFECYCLE_ACTIVE);
 assert(s.charge_until>now_ms()&&s.charge_until<=now_ms()+s.settings.usb_notify_ms);
 assert(!setenv("GKD_MENU_CHOICE","1",1));
 init(&s);s.session.ready.init=getpid();s.usb_stable=1;s.network_desired=1;
 assert(!mkdir("/media/data",0700)||errno==EEXIST);
 assert(!mkdir("/media/data/local",0700)||errno==EEXIST);
 assert(!mkdir("/media/data/local/etc",0700)||errno==EEXIST);
 write_file(APP_NETWORK,"#!/bin/sh\n[ -d /proc/self/fd/3 ] || exit 1\ncase \"$1\" in stop) [ \"${GKD_NETWORK_STOP_FAIL-0}\" = 0 ] || exit 1; printf 'GKD_APPLICATION_NETWORK=stopped\\n';; poll) sleep 0.05; printf 'GKD_APPLICATION_NETWORK=online fixture=1\\n';; esac\n",0700);
 network_poll(&s,now_ms());assert(s.network_running&&s.network_dirty);
 assert(!setenv("GKD_MENU_CHOICE","1",1));assert(!dispatch_command(&s,"usb-menu"));
 reach(&s,GKD_LIFECYCLE_STORAGE);assert(!s.network_running&&!s.network_dirty&&!s.network_desired);
 assert(!dispatch_command(&s,"return"));reach(&s,GKD_LIFECYCLE_WAIT_APP_READY);
 s.session.state=GKD_SESSION_READY;reach(&s,GKD_LIFECYCLE_ACTIVE);
 assert(s.network_desired);
 s.session.ready.init=getpid();
 network_poll(&s,now_ms()+3000U);assert(s.network_running);
 assert(!setenv("GKD_NETWORK_STOP_FAIL","1",1));
 unsigned before_storage_stop=stops;
 assert(!dispatch_command(&s,"usb-menu"));reach(&s,GKD_LIFECYCLE_RECOVERY);
 assert(stops==before_storage_stop&&s.network_dirty&&s.network_error);
 assert(!unsetenv("GKD_NETWORK_STOP_FAIL"));
 assert(!dispatch_command(&s,"retry"));reach(&s,GKD_LIFECYCLE_STORAGE);
 assert(!s.network_dirty&&stops==before_storage_stop+1U);

 init(&s);s.session.ready.init=getpid();s.network_desired=1;
 network_poll(&s,now_ms());assert(s.network_running&&s.network_dirty);
 while(s.network_running){network_poll(&s,now_ms());usleep(1000);}
 assert(!setenv("GKD_NETWORK_STOP_FAIL","1",1));s.stopping=1;
 stop_progress(&s);
 while(s.network_running){network_poll(&s,now_ms());usleep(1000);}
 assert(s.network_error&&!s.stop_host_requested&&s.session.pid==901&&!stop_progress(&s));
 assert(!dispatch_command(&s,"retry")&&!s.network_error);
 assert(!unsetenv("GKD_NETWORK_STOP_FAIL"));
 before_stop=stops;assert(!finish_stopping(&s));
 assert(stops==before_stop+1&&s.stopped_host==901&&!s.network_dirty&&s.network_etc==-1&&!s.terminal_error);

 init(&s);s.usb_stable=1;s.network_desired=0;s.lifecycle.state=GKD_LIFECYCLE_SUSPENDED;
 s.lifecycle.power_quiesced=1;s.lifecycle.app_paused=1;s.media.paused=1;
 assert(!gkd_app_lifecycle_event(&s.lifecycle,GKD_LIFECYCLE_EVENT_RETURN,0));operation(&s);
 assert(s.lifecycle.state==GKD_LIFECYCLE_ACTIVE&&!s.lifecycle.power_quiesced&&!s.media.paused&&s.network_desired);

 init(&s);s.session.ready.init=getpid();
 write_file(APP_GENERATION,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n",0444);
 idle_result=GKD_APP_IDLE_BUSY;
 assert(dispatch_command(&s,"config-save")<0&&errno==EBUSY&&s.settings_job.pid<=0);
 idle_result=GKD_APP_IDLE_ACQUIRED;
 unsigned cli_releases=idle_releases;
 assert(!dispatch_command(&s,"config-save"));
 assert(s.purpose==JOB_CONFIG_SAVE&&s.config_save_status==1&&s.settings_save_status==1);
 assert(s.settings_job.transaction&&!s.settings_job.deadline&&s.job.pid<=0);
 assert(gkd_app_job_cancel(&s.settings_job,now_ms())<0&&errno==EBUSY);
 assert(dispatch_command(&s,"usb-menu")<0&&errno==EBUSY);
 assert(dispatch_command(&s,"config-save")<0&&errno==EBUSY);
 s.stopping=1;assert(!stop_progress(&s)&&!s.stop_host_requested);s.stopping=0;
 while(s.purpose!=JOB_NONE){jobs(&s);usleep(1000);}
 assert(s.config_save_status==2&&!s.config_save_error&&idle_releases==cli_releases+1U);
 assert(!setenv("GKD_USB_FAIL","1",1));assert(!dispatch_command(&s,"config-save"));
 while(s.purpose!=JOB_NONE){jobs(&s);usleep(1000);}
 assert(s.config_save_status==-1&&s.config_save_error);
 assert(!unsetenv("GKD_USB_FAIL"));
 assert(!setenv("GKD_CONFIG_UNKNOWN","1",1));assert(!dispatch_command(&s,"config-save"));
 while(s.purpose!=JOB_NONE){jobs(&s);usleep(1000);}
 assert(s.config_save_status==-2&&s.config_save_error&&s.terminal_error);
 assert(!unsetenv("GKD_CONFIG_UNKNOWN"));
 write_file(APP_RUNTIME_ID,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n",0444);
 write_file(APP_PREPARE,"#!/bin/sh\n[ \"$6\" = --inspect ] || exit 1\nprintf 'GKDSU_INSPECT=PASS package=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb from=3.5 to=3.6\\n'\n",0700);
 assert(!mkdir("/media/sdcard",0700)||errno==EEXIST);
 init(&s);s.session.ready.init=getpid();write_file(APP_GAME,"#!/bin/sh\nprintf 'ACTIVE\\n'\n",0700);
 assert(!dispatch_command(&s,"update-entry"));assert(s.update_entry_deadline);
 assert(dispatch_command(&s,"usb-menu")<0&&errno==EBUSY);
 s.update_check_after=0;update_prepare(&s);assert(s.purpose==JOB_UPDATE_CHECK);
 while(s.purpose!=JOB_NONE){jobs(&s);usleep(1000);}
 assert(s.update_entry_deadline&&!s.update_requested&&s.update_status==1);
 write_file(APP_GAME,"#!/bin/sh\nprintf 'IDLE\\n'\n",0700);
 s.update_check_after=0;update_prepare(&s);
 while(s.purpose!=JOB_NONE){jobs(&s);usleep(1000);}
 assert(!s.update_entry_deadline&&!s.update_requested&&s.update_lease_owned&&s.menu_kind==MENU_UPDATE);
 assert(s.request.action==GKD_LIFECYCLE_ACTION_MENU_ACQUIRE&&!strcmp(s.update_from,"3.5")&&!strcmp(s.update_to,"3.6"));
 assert(menu_result(&s,"GKD_MENU_READY kind=UPDATE\nGKD_MENU_RESULT=CANCELLED kind=UPDATE\n")==GKD_LIFECYCLE_EVENT_CANCEL&&!s.update_requested);
 assert(menu_result(&s,"GKD_MENU_READY kind=UPDATE\nGKD_MENU_RESULT=SELECTED kind=UPDATE index=0\n")<0&&!s.update_requested);
 assert(menu_result(&s,"GKD_MENU_READY kind=UPDATE\nGKD_MENU_RESULT=SELECTED kind=UPDATE index=2\n")==GKD_LIFECYCLE_EVENT_CANCEL&&s.update_requested);
 update_release(&s);
 init(&s);write_file(APP_GAME,"#!/bin/sh\nprintf 'MALFORMED\\n'\n",0700);
 assert(!dispatch_command(&s,"update-entry"));s.update_check_after=0;update_prepare(&s);
 while(s.purpose!=JOB_NONE){jobs(&s);usleep(1000);}
 assert(!s.update_entry_deadline&&!s.update_requested&&s.update_status==-1);
 write_file(APP_RUNTIME_ID,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n",0444);
 write_file(APP_USB_ONLINE,"0\n",0600);
 write_file(APP_PREPARE,"#!/bin/sh\n[ -d /proc/self/fd/3 ] || exit 1\n[ \"$1\" = /proc/self/fd/3/gkd-update/system.gkdupdate ] || exit 1\n[ \"$GKD_PREPARE_FAIL\" != 1 ] || exit 1\ncase \"$GKD_PREPARE_RECEIPT\" in wrong) printf 'GKD_PREPARE=PASS\\n';; empty) :;; extra) printf 'GKDSU_PREPARE=PASS package_bytes=4096 p1_offset=512 kernel_offset=1024\\nEXTRA\\n';; *) printf 'GKDSU_PREPARE=PASS package_bytes=4096 p1_offset=512 kernel_offset=1024\\n';; esac\n",0700);
 assert(!mkdir("/media/sdcard",0700)||errno==EEXIST);
 init(&s);s.session.ready.init=getpid();
 write_file(APP_GAME,"#!/bin/sh\nprintf 'ACTIVE\\n'\n",0700);
 assert(!dispatch_command(&s,"update"));
 drain_jobs(&s);
 assert(s.update_status==-1&&!s.update_requested&&s.lifecycle.state==GKD_LIFECYCLE_ACTIVE);
 write_file(APP_GAME,"#!/bin/sh\nprintf 'IDLE\\n'\n",0700);
 s.update_requested=1;s.update_lease_owned=1;memset(s.update_package,'b',64);s.update_package[64]=0;
 update_prepare(&s);
 assert(s.update_status==-1&&s.update_error==EAGAIN&&!s.update_lock);
 battery_percent=30;s.update_requested=1;s.update_lease_owned=1;assert(!setenv("GKD_PREPARE_FAIL","1",1));
 update_prepare(&s);assert(s.purpose==JOB_UPDATE_PREPARE&&s.job.transaction&&!s.job.deadline);
 drain_jobs(&s);assert(s.update_status==-1&&!s.update_lock&&s.lifecycle.state==GKD_LIFECYCLE_ACTIVE);
 assert(!unsetenv("GKD_PREPARE_FAIL"));
 const char *bad_prepare[]={"wrong","empty","extra"};
 for(unsigned i=0;i<sizeof(bad_prepare)/sizeof(bad_prepare[0]);i++){
  init(&s);s.session.ready.init=getpid();s.update_requested=1;s.update_lease_owned=1;memset(s.update_package,'b',64);s.update_package[64]=0;
  assert(!setenv("GKD_PREPARE_RECEIPT",bad_prepare[i],1));update_prepare(&s);
  assert(s.purpose==JOB_UPDATE_PREPARE&&s.job.transaction&&!s.job.deadline);
  if(!i){
   pid_t transaction_pid=s.job.pid;
   service_control(&s,"suspend","GKD_APPLICATION_COMMAND=REJECTED errno=16\n");
   assert(s.purpose==JOB_UPDATE_PREPARE&&s.job.pid==transaction_pid&&s.job.transaction&&!s.job.deadline);
   assert(s.update_status==2&&s.lifecycle.state==GKD_LIFECYCLE_ACTIVE&&s.request.action==GKD_LIFECYCLE_ACTION_NONE);
  }
  drain_jobs(&s);
  assert(s.update_status==-1&&s.update_error==EPROTO&&!s.update_lock&&s.lifecycle.state==GKD_LIFECYCLE_ACTIVE);
 }
 assert(!unsetenv("GKD_PREPARE_RECEIPT"));
 battery_invalid=1;write_file(APP_USB_ONLINE,"1\n",0600);
 init(&s);s.session.ready.init=getpid();s.update_requested=1;s.update_lease_owned=1;memset(s.update_package,'b',64);s.update_package[64]=0;update_prepare(&s);
 assert(s.purpose==JOB_UPDATE_PREPARE&&s.job.transaction);
 s.session.pid=-1;s.session.state=GKD_SESSION_FAILED;s.session.error=ECHILD;session(&s);
 assert(s.lifecycle.state==GKD_LIFECYCLE_RECOVERY&&s.lifecycle.failed_action==GKD_LIFECYCLE_ACTION_APP_START);
 drain_jobs(&s);
 assert(s.update_status==4&&!s.update_error&&!s.update_lock&&s.request.action==GKD_LIFECYCLE_ACTION_NONE);
 assert(!dispatch_command(&s,"retry"));reach(&s,GKD_LIFECYCLE_WAIT_APP_READY);
 assert(s.update_status==4&&s.request.action==GKD_LIFECYCLE_ACTION_NONE);
 s.session.state=GKD_SESSION_READY;reach(&s,GKD_LIFECYCLE_ACTIVE);
 update_prepare(&s);
 assert(s.update_status==3&&s.request.action==GKD_LIFECYCLE_ACTION_POWER_QUIESCE);
 assert(s.power_event==GKD_LIFECYCLE_EVENT_REBOOT&&!s.lifecycle.menu_owned);

 /* A successful prepare must drop its actual root reference before stopping
  * the worker; otherwise the worker cannot detach its mounted loop. */
 teardown_pin_fd=open("/dev/null",O_RDONLY|O_CLOEXEC);assert(teardown_pin_fd>=0);
 s.update_lease.pin.root_fd=teardown_pin_fd;
 unsigned released_before_stop=idle_releases;before_stop=stops;
 uint64_t stop_deadline=now_ms()+4000U;
 while(stops==before_stop&&now_ms()<stop_deadline){operation(&s);usleep(1000);}
 assert(stops==before_stop+1&&!s.update_lease_owned&&idle_releases==released_before_stop+1);
 session(&s);
 assert(s.request.action==GKD_LIFECYCLE_ACTION_POWER_CARD_RELEASE&&s.update_status==3);
 teardown_pin_fd=-1;gkd_app_events_close(&s.events);gkd_menu_guard_owner_exit(&s.power_guard);
 /* Fail each update reboot step. Report FAILED through the real control
  * protocol and never automatically start another reboot or prepare. */
 for(unsigned failed_step=0;failed_step<3;failed_step++){
  init(&s);s.update_status=3;s.power_event=GKD_LIFECYCLE_EVENT_REBOOT;
  assert(!gkd_app_lifecycle_event(&s.lifecycle,GKD_LIFECYCLE_EVENT_REBOOT,0));
  for(unsigned step=0;step<failed_step;step++)finish(&s,0);
  enum gkd_app_lifecycle_action failed_action=s.request.action;
  finish(&s,EIO);
  assert(s.lifecycle.state==GKD_LIFECYCLE_RECOVERY&&s.lifecycle.failed_action==failed_action);
  assert(s.update_status==-1&&s.update_error==EIO);
  service_control(&s,"update-status","GKD_APPLICATION_UPDATE=FAILED errno=5\n");
  update_prepare(&s);update_prepare(&s);
  assert(s.update_status==-1&&s.request.action==GKD_LIFECYCLE_ACTION_NONE&&s.purpose==JOB_NONE);
 }
 assert(!unlink(APP_PREPARE));assert(!unlink(APP_USB_ONLINE));

 (void)unlink(APP_TRIAL_MARKER);
 init(&s);s.trial_checked=0;
 assert(dispatch_command(&s,"update")<0&&errno==EBUSY);
 write_file(APP_MANAGEMENT,"#!/bin/sh\nprintf 'GKD_APP_MANAGEMENT=READY\\n'\n",0700);
 trial_health(&s);assert(s.purpose==JOB_MANAGEMENT&&s.trial_checked==1);
 drain_jobs(&s);assert(s.trial_checked==2&&!s.trial_pending&&!identity_reads);
 init(&s);s.trial_checked=0;write_file(APP_TRIAL_MARKER,"trial\n",0600);
 write_file(APP_MANAGEMENT,"#!/bin/sh\nexit 1\n",0700);
 trial_health(&s);drain_jobs(&s);
 assert(s.terminal_error&&s.trial_pending&&!identity_reads);
 write_file(APP_MANAGEMENT,"#!/bin/sh\nprintf 'GKD_APP_MANAGEMENT=READY\\n'\n",0700);
 write_file(APP_UPDATE,"#!/bin/sh\n[ \"$1\" = mark-good ] || exit 1\ncase \"$GKD_GOOD_RECEIPT\" in wrong) printf 'MARK_GOOD=PASS\\n';; empty) :;; extra) printf 'GKD_APPLICATION_UPDATE=MARK_GOOD outcome=complete target=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\\nEXTRA\\n';; *) printf 'GKD_APPLICATION_UPDATE=MARK_GOOD outcome=complete target=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\\n';; esac\n",0700);
 const char *bad_good[]={"wrong","empty","extra"};
 for(unsigned i=0;i<sizeof(bad_good)/sizeof(bad_good[0]);i++){
  unsigned reads=identity_reads;init(&s);s.trial_checked=0;
  assert(!setenv("GKD_GOOD_RECEIPT",bad_good[i],1));trial_health(&s);drain_jobs(&s);
  assert(s.terminal_error==EPROTO&&s.trial_checked==1&&s.trial_pending&&identity_reads==reads);
 }
 assert(!unsetenv("GKD_GOOD_RECEIPT"));
 init(&s);s.trial_checked=0;trial_health(&s);
 while(s.purpose==JOB_MANAGEMENT){jobs(&s);usleep(1000);}
 assert(s.purpose==JOB_UPDATE_GOOD&&s.job.transaction&&!s.job.deadline);
 unsigned reads=identity_reads;drain_jobs(&s);
 assert(!s.terminal_error&&s.trial_checked==2&&!s.trial_pending&&identity_reads==reads+1);
 assert(!unlink(APP_MANAGEMENT));assert(!unlink(APP_UPDATE));assert(!unlink(APP_TRIAL_MARKER));assert(!unlink(APP_RUNTIME_ID));

 init(&s);s.usb_stable=1;assert(!setenv("GKD_MENU_CHOICE","0",1));
 assert(!dispatch_command(&s,"usb-menu"));reach(&s,GKD_LIFECYCLE_ACTIVE);
 assert(s.charge_until>now_ms()&&s.charge_until<=now_ms()+s.settings.usb_notify_ms);
 assert(!setenv("GKD_MENU_CHOICE","1",1));
 init(&s);s.session.ready.init=getpid();s.usb_stable=1;s.network_desired=1;
 network_poll(&s,now_ms());assert(s.network_etc>=0&&s.network_dirty);
 int pinned_dns=s.network_etc;
 while(s.network_running){network_poll(&s,now_ms());usleep(1000);}
 s.session.ready.init=2147483647;s.network_desired=0;
 network_poll(&s,now_ms());assert(s.network_running);
 while(s.network_running){network_poll(&s,now_ms());usleep(1000);}
 assert(!s.network_dirty&&!s.network_error&&s.network_etc==-1);
 errno=0;assert(fcntl(pinned_dns,F_GETFD)<0&&errno==EBADF);
 critical_battery_tests();
 fps_service_tests();
 settings_service_tests();
 usb_edge_tests();
 idle_menu_clock_tests();
 card_monitor_tests();
 assert(!setenv("GKD_MENU_CHOICE","0",1));
 card_page_tests();usb_loading_tests();waiting_cleanup_tests();
 boot_update_loading_tests();
 assert(!unlink(APP_NETWORK));
 assert(!unlink(APP_GAME));
 assert(!unlink(APP_MENU));assert(!unlink(APP_USB));assert(!unlink(APP_LUN));
 puts("GKD_APP_SERVICE_FIXTURE=PASS real-job/menu-result/storage-restart/debug-ready/fail-export/fail-stop/game/network-stop-barrier/retry/config-save/update-gates/transaction/exact-receipts/recover-then-reboot/update-entry/composite-trial-good/menu-stop/control-receipt/reaped-cleanup-retry/power-quiesce-retry/transaction-suspend-busy/power-resume/network-stop-explicit-retry/ICS-cleanup-after-init-exit");
 return 0;
}
