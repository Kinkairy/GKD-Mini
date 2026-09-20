// SPDX-License-Identifier: GPL-2.0
#define main recovery_ui_main
#include "../source/gkd-recovery-ui.c"
#undef main
#include <assert.h>
struct observations { volatile unsigned frames, presents, starts, stops, cutoff, restart, synced, keys, failures; unsigned fail, delay; };
static struct observations *obs;
void __wrap_gkd_ui_render_loading(struct gkd_ui_surface *s,const struct gkd_ui_config *c,const struct gkd_ui_font *f,unsigned frame)
{ (void)s;(void)c;(void)f;(void)frame; ++obs->frames; }
void __wrap_gkd_ui_render_status(struct gkd_ui_surface *s,const struct gkd_ui_config *c,const struct gkd_ui_font *f,const char *text,int critical)
{ (void)s;(void)c;(void)f; if(critical){assert(!strcmp(text,"FAILED"));++obs->failures;} }
int __wrap_fsync(int fd) { (void)fd; ++obs->presents; return 0; }
int __wrap_execve(const char *p,char *const argv[],char *const envp[])
{
 (void)envp; assert(obs->frames>0 && obs->presents>0);
 if(!strcmp(p,GKD_RECOVERY_USB_PROGRAM)) {
  if(!strcmp(argv[1],GKD_RECOVERY_USB_START)) {++obs->starts; if(obs->fail==1U)_exit(1);}
  else {++obs->stops;if(obs->fail==2U)_exit(1);}
 }
 if(obs->delay)usleep(obs->delay);
 _exit(0);
}
void __wrap_sync(void){assert(obs->frames>0);++obs->synced;}
int __wrap_reboot(int command){assert(command==RB_AUTOBOOT);assert(obs->stops==1);++obs->restart;errno=EIO;return -1;}
int gkd_r_poweroff(void){assert(obs->frames>0 && obs->presents>0);++obs->cutoff;if(obs->delay)usleep(obs->delay);errno=EIO;return -1;}
int __wrap_gkd_input_owner_next_key(struct gkd_input_owner *owner){(void)owner;++obs->keys;return KEY_LEFTALT;}
int __wrap_gkd_ui_draw_osd(struct gkd_ui_surface *s,const struct gkd_ui_config *c,const struct gkd_ui_font *f,const struct gkd_ui_osd *message,unsigned opacity)
{ (void)s;(void)c;(void)f;assert(!strcmp(message->text,"FAILED")&&message->icon==GKD_UI_OSD_ICON_FAILURE&&message->critical&&opacity==255U);++obs->failures;return 0; }
static void reset(void){memset(obs,0,sizeof(*obs));obs->delay=70000;}
int main(void)
{
 struct runtime r;memset(&r,0,sizeof(r));r.fb_fd=-1;r.frame_bytes=320U*240U*2U;
 r.mapping=calloc(1,r.frame_bytes);r.draw_buffer=calloc(1,r.frame_bytes);assert(r.mapping&&r.draw_buffer);
 r.surface.pixels=r.draw_buffer;r.surface.width=320;r.surface.height=240;r.surface.stride=320;
 gkd_ui_config_defaults(&r.config);r.config.loading_interval_ms=10;
 obs=mmap(NULL,sizeof(*obs),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);assert(obs!=MAP_FAILED);
 reset();r.result_notice=0;assert(action_export(&r)==0);assert(obs->starts==1&&obs->stops==1&&obs->keys==1&&obs->frames>3&&obs->failures==0);
 reset();r.result_notice=0;obs->delay=0;char *args[]={"/test",NULL};assert(run_program_loading(&r,args[0],args)==0);assert(obs->frames>=1&&obs->presents>=1);
 reset();r.result_notice=0;obs->fail=1;assert(run_action(&r,0)<0);assert(obs->starts==1&&obs->stops==0&&r.result_notice==1);
 reset();r.result_notice=0;obs->fail=2;assert(run_action(&r,0)<0);assert(obs->starts==1&&obs->stops==1&&r.result_notice==1);
 reset();r.result_notice=0;assert(run_action(&r,2)<0);assert(obs->restart==1&&obs->synced==1&&obs->stops==1&&r.result_notice==1&&obs->frames>1);
 reset();r.result_notice=0;obs->fail=2;assert(run_action(&r,2)<0);assert(obs->restart==0&&obs->synced==0&&r.result_notice==1);
 reset();r.result_notice=0;assert(run_action(&r,3)<0);assert(obs->cutoff==1&&obs->restart==0&&obs->stops==0&&obs->synced==0&&r.result_notice==1&&obs->frames>1);
 reset();r.result_notice=0;assert(action_recovery(&r,"update")==0);assert(obs->frames>1);
 assert(!render_result_notice(&r,0U)&&obs->failures==1);
 munmap(obs,sizeof(*obs));free(r.mapping);free(r.draw_buffer);
 puts("GKD_RECOVERY_WAIT=PASS publish-before-work/export-enter-return/fast-complete/power-animated/failure-returns-menu-with-shared-osd/no-shutdown-network-stop");return 0;
}
