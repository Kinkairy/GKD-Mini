/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#define GKD_MENU_CONFIG_DEFAULT "/out/builtin.conf"
#define GKD_MENU_DEVICE "/out/route-device"
#include "../source/gkd-app-menu-launch.c"
#include <assert.h>
#include <stdarg.h>
static ino_t route_inode;static dev_t route_device;
static unsigned configurations,pulses,updates;static int update_error;
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
 else if(request==GKD_INPUT_ROUTE_REPEAT){
  updates++;
  if(update_error){va_end(ap);errno=update_error;return -1;}
  struct gkd_input_route_repeat_config *request=va_arg(ap,void *);
  configured=request->route;
  assert(request->repeat.reserved==0);
  assert(request->repeat.source==0||
         (request->repeat.source==KEY_LEFTCTRL&&request->repeat.on_ms==50&&request->repeat.off_ms==50));
 }
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
static unsigned output_key(const struct gkd_menu_vt_config *c,unsigned source)
{
 if(source==c->trigger)return 0;
 for(unsigned i=0;i<c->map_count;i++)if(c->maps[i].source==source)return c->maps[i].target;
 return source;
}
static void orientation_cases(struct gkd_app_menu_launch *m)
{
 struct gkd_game_orientation state=GKD_GAME_ORIENTATION_INIT;
 struct gkd_menu_vt_config base=configured;
 unsigned calls=updates;
 assert(!gkd_app_menu_orientation(m,&state)&&updates==calls);
 state.aspect=GKD_ASPECT_LANDSCAPE;state.source=GKD_ORIGIN_WS_FOOTER;state.scope=GKD_SCOPE_LAUNCH_ROM;
 assert(!gkd_app_menu_orientation(m,&state)&&updates==calls);
 state.aspect=GKD_ASPECT_PORTRAIT;update_error=EBUSY;
 assert(gkd_app_menu_orientation(m,&state)==1&&!m->portrait&&!memcmp(&configured,&base,sizeof(base)));
 update_error=0;assert(!gkd_app_menu_orientation(m,&state)&&m->portrait);
 assert(output_key(&configured,KEY_LEFTCTRL)==output_key(&base,KEY_LEFTSHIFT));
 assert(output_key(&configured,KEY_LEFTALT)==output_key(&base,KEY_LEFTALT));
 assert(output_key(&configured,KEY_SPACE)==output_key(&base,KEY_LEFTCTRL));
 assert(output_key(&configured,KEY_LEFTSHIFT)==output_key(&base,KEY_LEFTSHIFT));
 calls=updates;assert(!gkd_app_menu_orientation(m,&state)&&updates==calls);
 state.aspect=GKD_ASPECT_LANDSCAPE;
 assert(!gkd_app_menu_orientation(m,&state)&&!m->portrait&&!memcmp(&configured,&base,sizeof(base)));
 state.aspect=GKD_ASPECT_PORTRAIT;assert(!gkd_app_menu_orientation(m,&state));
 state.reserved=1;assert(!gkd_app_menu_orientation(m,&state)&&!m->portrait&&!memcmp(&configured,&base,sizeof(base)));
 state.reserved=0;assert(!gkd_app_menu_orientation(m,&state));
 struct gkd_game_orientation unknown=GKD_GAME_ORIENTATION_INIT;
 update_error=EIO;assert(gkd_app_menu_orientation(m,&unknown)==-1&&errno==EIO&&m->portrait);
 update_error=0;
 assert(!gkd_app_menu_orientation(m,&unknown)&&!m->portrait&&!memcmp(&configured,&base,sizeof(base)));
 update_error=ENOTTY;assert(!gkd_app_menu_orientation(m,&state)&&m->orientation_disabled&&!m->portrait);
 calls=updates;assert(!gkd_app_menu_orientation(m,&state)&&updates==calls);update_error=0;
}
int main(int argc,char **argv)
{
 assert(argc==2&&geteuid()==0);
 struct gkd_menu_vt_config route={.version=GKD_MENU_VT_VERSION,.trigger=KEY_HOME},out,sentinel;
 struct gkd_menu_profile profile={0};
 memset(&sentinel,0xa5,sizeof(sentinel));out=sentinel;
 route.trigger=KEY_LEFTCTRL;assert(gkd_input_route_portrait(&route,&profile,&out)==-1&&errno==EOPNOTSUPP);
 route.trigger=KEY_LEFTSHIFT;assert(gkd_input_route_portrait(&route,&profile,&out)==-1&&errno==EOPNOTSUPP);
 profile.portrait_map_count=1;profile.portrait_maps[0]=(struct gkd_input_route_map){KEY_SPACE,GKD_PORTRAIT_ORIGINAL_A};
 route.trigger=KEY_SPACE;assert(gkd_input_route_portrait(&route,&profile,&out)==-1&&errno==EOPNOTSUPP);
 profile.portrait_map_count=0;
 route.trigger=KEY_HOME;route.map_count=1;route.maps[0]=(struct gkd_input_route_map){KEY_LEFTCTRL,0};
 assert(gkd_input_route_portrait(&route,&profile,&out)==-1&&errno==EOPNOTSUPP);
 route.maps[0].source=KEY_LEFTSHIFT;assert(gkd_input_route_portrait(&route,&profile,&out)==-1&&errno==EOPNOTSUPP);
 route.maps[0].source=KEY_SPACE;profile.portrait_map_count=1;profile.portrait_maps[0]=(struct gkd_input_route_map){KEY_SPACE,GKD_PORTRAIT_ORIGINAL_A};
 assert(gkd_input_route_portrait(&route,&profile,&out)==-1&&errno==EOPNOTSUPP);profile.portrait_map_count=0;
 route.map_count=GKD_INPUT_ROUTE_MAPS;
 for(unsigned i=0;i<route.map_count;i++)route.maps[i]=(struct gkd_input_route_map){300+i,KEY_ENTER};
 assert(gkd_input_route_portrait(&route,&profile,&out)==-1&&errno==E2BIG);
 assert(!memcmp(&out,&sentinel,sizeof(out)));
 route.map_count=0;profile.portrait_map_count=1;
 profile.portrait_maps[0]=(struct gkd_input_route_map){KEY_SPACE,GKD_PORTRAIT_ORIGINAL_A};
 assert(!gkd_input_route_portrait(&route,&profile,&out));
 assert(output_key(&out,KEY_LEFTCTRL)==KEY_LEFTSHIFT&&output_key(&out,KEY_SPACE)==KEY_LEFTCTRL);
 profile.portrait_maps[0].target=GKD_PORTRAIT_CURRENT_Y;
 assert(!gkd_input_route_portrait(&route,&profile,&out));
 assert(output_key(&out,KEY_SPACE)==KEY_LEFTSHIFT);
 profile.portrait_maps[0].target=KEY_ENTER;
 assert(!gkd_input_route_portrait(&route,&profile,&out));
 assert(output_key(&out,KEY_SPACE)==KEY_ENTER);
 profile.portrait_map_count=0;

 assert(!system("mkdir -p /var/run/gkd-app/input-config/current /media/data/local/etc/gkd-mini"));
 int fd=open(argv[1],O_RDONLY);assert(fd>=0);char original[16384];ssize_t n=read(fd,original,sizeof(original)-1);assert(n>0);original[n]=0;close(fd);
 /* Bind the expected Y identity to production configuration, not DTS labels. */
 assert(strstr(original,"input_map_y=KEY_LEFTSHIFT\n"));
 assert(strstr(original,"input_map_x=KEY_SPACE\n"));
 write_file(GKD_MENU_DEVICE,"");struct stat st;assert(!stat(GKD_MENU_DEVICE,&st));route_inode=st.st_ino;route_device=st.st_dev;
 struct gkd_app_menu_launch m=GKD_APP_MENU_LAUNCH_INIT;
 settings(original,"raw");write_file(GKD_MENU_CONFIG_DEFAULT,"invalid");
 assert(gkd_app_menu_prepare(&m,-1,"bad","bad",0,NULL)<0&&m.fd<0);
 write_file("/out/opk","fixture-only-image");int opk=open("/out/opk",O_RDONLY);assert(opk>=0);char hash[65];assert(!digest(opk,hash));
 char base[1024],user[2048];
 snprintf(base,sizeof(base),"version=2\n[core]\nopk_sha256=%s\ndesktop=game.desktop\nexec=game\naction=native\nmap.l1=59\nportrait.map.x=original-a\n",hash);
 write_file(GKD_MENU_CONFIG_DEFAULT,base);
 settings(original,"xbox");assert(!gkd_app_menu_prepare(&m,opk,"game.desktop","game",0,NULL));
 assert(m.fd>=0&&configurations==1&&!pulses&&configured.version==2&&configured.count==1&&configured.keys[0]==KEY_HOME&&configured.map_count==6);
 orientation_cases(&m);gkd_app_menu_close(&m);assert(!m.portrait&&!m.orientation_disabled);
 const char *catalog="/media/data/local/etc/gkd-mini/input-routing.conf";
 snprintf(user,sizeof(user),"%s[side-attack]\nopk_sha256=%s\ndesktop=game.desktop\nexec=game\naction=native\nrom=/media/sdcard/roms/Test Game.rom\nmap.side_dot=KEY_SPACE\n",base,hash);
 write_file(catalog,user);settings(original,"ps");char *game[]={"/media/sdcard/roms/Test Game.rom"};
 assert(!gkd_app_menu_prepare(&m,opk,"game.desktop","game",1,game));assert(!strcmp(m.profile.id,"side-attack")&&!pulses);
 unsigned target=0;for(unsigned i=0;i<configured.map_count;i++)if(configured.maps[i].source==KEY_LEFTCTRL)target=configured.maps[i].target;
 assert(target==KEY_SPACE);orientation_cases(&m);gkd_app_menu_close(&m);
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
 orientation_cases(&m);
 /* A real close releases the lease; no portrait state reaches the next game. */
 m.orientation_disabled=0;
 struct gkd_game_orientation portrait={GKD_ORIENTATION_VERSION,GKD_ASPECT_PORTRAIT,0,GKD_ORIENTATION_UNKNOWN,GKD_ORIENTATION_UNKNOWN,GKD_ORIGIN_WS_FOOTER,GKD_SCOPE_LAUNCH_ROM,0};
 assert(!gkd_app_menu_orientation(&m,&portrait)&&m.portrait);
 int old=m.fd;gkd_app_menu_close(&m);assert(m.fd==-1&&!m.portrait&&fcntl(old,F_GETFD)<0&&errno==EBADF);
 assert(!gkd_app_menu_prepare(&m,opk,"game.desktop","game",0,NULL));
 assert(!m.portrait&&output_key(&configured,KEY_LEFTCTRL)==KEY_LEFTCTRL&&output_key(&configured,KEY_SPACE)==KEY_SPACE);gkd_app_menu_close(&m);
 puts("GKD_PORTRAIT_LAUNCH=PASS raw/xbox/ps/busy/landscape/unknown/invalid/old-kernel/close/relaunch");
 close(opk);puts("GKD_INPUT_LAUNCH=PASS real-config/hash/selection raw-menu-route/no-auto-open/native/game-override/unknown/invalid/symlink/parent-permissions");return 0;
}
