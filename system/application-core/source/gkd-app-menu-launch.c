/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-menu-launch.h"
#include "gkd-menu-vt.h"
#include "gkd-app-settings.h"
#include "gkd-input-style.h"
#include "gkd-input-keys.h"
#include "gkd-update-sha256.h"
#include <linux/input.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef GKD_MENU_CONFIG_DEFAULT
#define GKD_MENU_CONFIG_DEFAULT "/var/run/gkd-app/input-routing.conf"
#endif
#ifndef GKD_MENU_DEVICE
#define GKD_MENU_DEVICE "/dev/gkd-input-route"
#endif
/* Pin every user-config directory. A writable or symlinked ancestor is a
 * configuration error, never grounds to silently choose built-in bindings. */
static int user_catalog(struct gkd_menu_config *catalog,unsigned *line)
{
 static const char *const components[]={"media","data","local","etc","gkd-mini"};
 int dir=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC),result=-1,fd=-1;
 if(dir<0)return -1;
 for(unsigned i=0;i<sizeof(components)/sizeof(components[0]);i++) {
  int next=openat(dir,components[i],O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
  if(next<0)goto done;
  struct stat st;
  if(fstat(next,&st)||!S_ISDIR(st.st_mode)||st.st_uid||(st.st_mode&0022)){
   close(next);errno=EPERM;goto done;
  }
  close(dir);dir=next;
 }
 fd=openat(dir,"input-routing.conf",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
 if(fd>=0)result=gkd_menu_config_load_fd(fd,catalog,line);
done:;
 int saved=errno;if(fd>=0)close(fd);close(dir);errno=saved;return result;
}
static int digest(int fd,char out[65])
{
 struct stat a,b;unsigned char bytes[32768],hash[32];struct gkdu_sha256 state;off_t offset=0;
 if(fstat(fd,&a)||!S_ISREG(a.st_mode)){errno=EINVAL;return -1;}
 gkdu_sha256_init(&state);
 for(;;){
  ssize_t n=pread(fd,bytes,sizeof(bytes),offset);if(n<0&&errno==EINTR)continue;
  if(n<0)return -1;
  if(!n)break;
  gkdu_sha256_update(&state,bytes,(size_t)n);offset+=n;
 }
 if(fstat(fd,&b))return -1;
 if(offset!=a.st_size||a.st_size!=b.st_size||a.st_mtim.tv_sec!=b.st_mtim.tv_sec||
    a.st_mtim.tv_nsec!=b.st_mtim.tv_nsec||a.st_ctim.tv_sec!=b.st_ctim.tv_sec||
    a.st_ctim.tv_nsec!=b.st_ctim.tv_nsec){errno=ESTALE;return -1;}
 gkdu_sha256_final(&state,hash);
 for(unsigned i=0;i<32;i++)sprintf(out+2*i,"%02x",hash[i]);
 return 0;
}
int gkd_app_menu_prepare(struct gkd_app_menu_launch *m,int opk,const char *desktop,const char *exec,int argc,char *const argv[])
{
 if(!m||m->fd>=0){errno=EINVAL;return -1;}
 struct gkd_app_settings settings;
 if(gkd_app_settings_load("/var/run/gkd-app/input-config/current/effective.conf",&settings))return -1;
 struct gkd_menu_config *catalog=malloc(sizeof(*catalog));char hash[65];unsigned line=0;
 if(!catalog)return -1;
 int rc=user_catalog(catalog,&line);
 if(rc&&errno==ENOENT)rc=gkd_menu_config_load(GKD_MENU_CONFIG_DEFAULT,catalog,&line);
 if(rc||digest(opk,hash)){int e=errno;free(catalog);errno=e;return -1;}
 rc=gkd_menu_config_select_game(catalog,hash,desktop,exec,argc,argv,&m->profile);free(catalog);
 if(rc<0)return -1;
 struct gkd_menu_vt_config config;unsigned short trigger,brightness;
 if(gkd_input_key_code(settings.menu_key,&trigger)||gkd_input_key_code(settings.brightness_key,&brightness)||
    gkd_input_route_compile(settings.input_style,trigger,brightness,&m->profile,&config)<0)return -1;
 int fd=open(GKD_MENU_DEVICE,O_RDWR|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);struct stat st;
 if(fd<0)return -1;
 if(fstat(fd,&st)||!S_ISCHR(st.st_mode)||st.st_uid||(st.st_mode&0077)){
  close(fd);errno=EPERM;return -1;
 }
 if(ioctl(fd,GKD_MENU_VT_CONFIG,&config)){int e=errno;close(fd);errno=e;return -1;}
 m->fd=fd;m->base=config;m->portrait=0;m->orientation_disabled=0;
 fprintf(stderr,"GKD_INPUT_SESSION=READY style=%s maps=%u profile=%s action=%s keys=%u\n",gkd_input_style_name(settings.input_style),config.map_count,m->profile.id[0]?m->profile.id:"unknown",gkd_menu_action_name(m->profile.action),config.count);
 return 0;
}
int gkd_app_menu_orientation(struct gkd_app_menu_launch *m,const struct gkd_game_orientation *state)
{
 if(!m||m->fd<0||m->orientation_disabled)return 0;
 int portrait=gkd_game_orientation_valid(state)&&state->aspect==GKD_ASPECT_PORTRAIT;
 if(portrait==m->portrait)return 0;
 struct gkd_menu_vt_config next=m->base;
 if(portrait&&gkd_input_route_portrait(&m->base,&m->profile,&next)){
  if(errno!=EOPNOTSUPP&&errno!=E2BIG)return -1;
  m->orientation_disabled=1;
  fprintf(stderr,"GKD_PORTRAIT_INPUT=UNAVAILABLE reason=binding-conflict errno=%d\n",errno);
  return 0;
 }
 struct gkd_input_route_repeat_config request={.route=next};
 if(portrait)request.repeat=(struct gkd_input_autofire_config){KEY_LEFTCTRL,50,50,0};
 if(ioctl(m->fd,GKD_INPUT_ROUTE_REPEAT,&request)){
  if(errno==EBUSY)return 1;
  /* Older RC3.6 kernels remain usable, but cannot run this new feature. */
  if(errno==ENOTTY&&!m->portrait){
   m->orientation_disabled=1;
   fprintf(stderr,"GKD_PORTRAIT_INPUT=UNAVAILABLE reason=kernel-update-required\n");return 0;
  }
  return -1;
 }
 m->portrait=portrait;
 fprintf(stderr,"GKD_PORTRAIT_INPUT=%s source=dot-and-a target=current-y repeat_hz=%d\n",portrait?"ENABLED":"RESTORED",portrait?10:0);
 return 0;
}
int gkd_app_menu_pulse(struct gkd_app_menu_launch *m)
{
 if(!m||m->fd<0){errno=ENOTSUP;return -1;}
 return ioctl(m->fd,GKD_MENU_VT_PULSE);
}
int gkd_app_menu_receipt(struct gkd_app_menu_launch *m)
{
 if(!m||m->fd<0){errno=EBADF;return -1;}
 struct gkd_menu_vt_receipt receipt;ssize_t n;
 do n=read(m->fd,&receipt,sizeof(receipt));while(n<0&&errno==EINTR);
 if(n<0)return -1;
 if(n!=(ssize_t)sizeof(receipt)||receipt.version!=GKD_MENU_VT_VERSION||receipt.error<0||receipt.error>4095){errno=EPROTO;return -1;}
 if(receipt.error){errno=receipt.error;return -1;}return 0;
}
int gkd_app_menu_cancel(struct gkd_app_menu_launch *m)
{
 if(!m||m->fd<0)return 0;
 if(ioctl(m->fd,GKD_MENU_VT_CANCEL))return -1;
 if(gkd_app_menu_receipt(m)&&errno!=ECANCELED&&errno!=EAGAIN)return -1;
 return 0;
}
void gkd_app_menu_close(struct gkd_app_menu_launch *m)
{
 if(m&&m->fd>=0){close(m->fd);m->fd=-1;}
 if(m){m->portrait=0;m->orientation_disabled=0;memset(&m->base,0,sizeof(m->base));}
}
