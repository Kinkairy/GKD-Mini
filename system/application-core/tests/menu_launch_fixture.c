/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#define GKD_MENU_CONFIG_DEFAULT "/out/builtin.conf"
#define GKD_MENU_DEVICE "/out/route-device"
#include "../source/gkd-app-menu-launch.c"
#include <assert.h>
#include <stdarg.h>
static ino_t route_inode;static dev_t route_device;
static unsigned configurations,pulses;
static struct gkd_menu_vt_config configured;
int __real_fstat(int,struct stat *);
int __wrap_fstat(int fd,struct stat *st)
{
 int rc=__real_fstat(fd,st);
 if(!rc&&st->st_ino==route_inode&&st->st_dev==route_device)st->st_mode=S_IFCHR|0600;
 return rc;
}
int __wrap_ioctl(int fd,unsigned long request,...)
{
 (void)fd;va_list ap;va_start(ap,request);
 if(request==GKD_MENU_VT_CONFIG){configured=*(struct gkd_menu_vt_config *)va_arg(ap,void *);configurations++;}
 else if(request==GKD_MENU_VT_PULSE)pulses++;
 else assert(request==GKD_MENU_VT_CANCEL);
 va_end(ap);return 0;
}
static void write_file(const char *path,const char *text)
{
 int fd=open(path,O_CREAT|O_TRUNC|O_WRONLY|O_CLOEXEC,0600);assert(fd>=0);
 assert(write(fd,text,strlen(text))==(ssize_t)strlen(text));assert(!close(fd));
}
static void settings(const char *text,const char *style)
{
 const char *start=strstr(text,"input_style="),*end;
 assert(start&&(end=strchr(start,'\n')));
 FILE *f=fopen("/var/run/gkd-app/input-config/current/effective.conf","w");assert(f);
 assert(fwrite(text,1,(size_t)(start-text),f)==(size_t)(start-text));
 assert(fprintf(f,"input_style=%s%s",style,end)>0);assert(!fclose(f));
}
int main(int argc,char **argv)
{
 assert(argc==2&&geteuid()==0);
 assert(!system("mkdir -p /var/run/gkd-app/input-config/current /media/data/local/etc/gkd-mini"));
 int fd=open(argv[1],O_RDONLY);assert(fd>=0);char original[16384];ssize_t n=read(fd,original,sizeof(original)-1);assert(n>0);original[n]=0;close(fd);
 write_file(GKD_MENU_DEVICE,"");struct stat st;assert(!stat(GKD_MENU_DEVICE,&st));route_inode=st.st_ino;route_device=st.st_dev;
 struct gkd_app_menu_launch m=GKD_APP_MENU_LAUNCH_INIT;
 settings(original,"raw");write_file(GKD_MENU_CONFIG_DEFAULT,"invalid");
 assert(gkd_app_menu_prepare(&m,-1,"bad","bad",0,NULL)<0&&m.fd<0);
 write_file("/out/opk","fixture-only-image");int opk=open("/out/opk",O_RDONLY);assert(opk>=0);char hash[65];assert(!digest(opk,hash));
 char base[1024],user[2048];
 snprintf(base,sizeof(base),"version=2\n[core]\nopk_sha256=%s\ndesktop=game.desktop\nexec=game\naction=native\nmap.l1=59\n",hash);
 write_file(GKD_MENU_CONFIG_DEFAULT,base);
 settings(original,"xbox");assert(!gkd_app_menu_prepare(&m,opk,"game.desktop","game",0,NULL));
 assert(m.fd>=0&&configurations==1&&!pulses&&configured.version==2&&configured.count==1&&configured.keys[0]==KEY_HOME&&configured.map_count==6);
 gkd_app_menu_close(&m);
 const char *catalog="/media/data/local/etc/gkd-mini/input-routing.conf";
 snprintf(user,sizeof(user),"%s[side-attack]\nopk_sha256=%s\ndesktop=game.desktop\nexec=game\naction=native\nrom=/media/sdcard/roms/Test Game.rom\nmap.side_dot=KEY_SPACE\n",base,hash);
 write_file(catalog,user);settings(original,"ps");char *game[]={"/media/sdcard/roms/Test Game.rom"};
 assert(!gkd_app_menu_prepare(&m,opk,"game.desktop","game",1,game));assert(!strcmp(m.profile.id,"side-attack")&&!pulses);
 unsigned target=0;for(unsigned i=0;i<configured.map_count;i++)if(configured.maps[i].source==KEY_LEFTCTRL)target=configured.maps[i].target;
 assert(target==KEY_SPACE);gkd_app_menu_close(&m);
 assert(!gkd_app_menu_prepare(&m,opk,"different.desktop","game",1,game));assert(!m.profile.id[0]&&!configured.count);gkd_app_menu_close(&m);
 write_file(catalog,"invalid");assert(gkd_app_menu_prepare(&m,opk,"game.desktop","game",1,game)<0&&m.fd<0);
 assert(!unlink(catalog));assert(!symlink(GKD_MENU_CONFIG_DEFAULT,catalog));assert(gkd_app_menu_prepare(&m,opk,"game.desktop","game",0,NULL)<0&&m.fd<0);assert(!unlink(catalog));
 assert(!chmod("/media/data/local/etc/gkd-mini",0777));assert(gkd_app_menu_prepare(&m,opk,"game.desktop","game",0,NULL)<0&&errno==EPERM&&m.fd<0);
 assert(!chmod("/media/data/local/etc/gkd-mini",0755));
 write_file(catalog,base);
 settings(original,"raw");assert(!gkd_app_menu_prepare(&m,opk,"game.desktop","game",0,NULL)&&m.fd>=0&&
                              configured.trigger==KEY_HOME&&configured.map_count==1&&configured.count==1&&
                              configured.keys[0]==KEY_HOME&&configured.maps[0].source==KEY_END&&
                              configured.maps[0].target==0&&!pulses);
 gkd_app_menu_close(&m);
 close(opk);puts("GKD_INPUT_LAUNCH=PASS real-config/hash/selection raw-menu-route/no-auto-open/native/game-override/unknown/invalid/symlink/parent-permissions");return 0;
}
