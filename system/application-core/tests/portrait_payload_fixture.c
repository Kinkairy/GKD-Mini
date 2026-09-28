/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-payload.h"
#include "gkd-app-menu-launch.h"
#include "gkd-app-orientation.h"
#include "gkd-app-game-control.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
struct shared {struct gkd_orientation_page page;int portrait;};
static struct shared *shared;
static int route_fd,updates,busy_once,fatal_restore;
static volatile sig_atomic_t stopping;
static void stop(int sig){(void)sig;stopping=1;}
int __wrap_ioctl(int fd,unsigned long command,...)
{
 assert(fd==route_fd);
 if(command==GKD_MENU_VT_CANCEL)return 0;
 assert(command==GKD_INPUT_ROUTE_REPEAT);
 if(busy_once){busy_once=0;errno=EBUSY;return -1;}
 va_list ap;va_start(ap,command);struct gkd_input_route_repeat_config *request=va_arg(ap,void *);va_end(ap);
 struct gkd_menu_vt_config *c=&request->route;
 int portrait=0;
 for(unsigned i=0;i<c->map_count;i++)if(c->maps[i].source==KEY_LEFTCTRL)portrait=c->maps[i].target==KEY_LEFTSHIFT;
 assert(portrait?(request->repeat.source==KEY_LEFTCTRL&&request->repeat.on_ms==50&&request->repeat.off_ms==50):!request->repeat.source);
 if(fatal_restore&&!portrait){errno=EIO;return -1;}
 __atomic_store_n(&shared->portrait,portrait,__ATOMIC_RELEASE);updates++;return 0;
}
static void observed(int portrait)
{
 for(int i=0;i<250;i++){
  if(__atomic_load_n(&shared->portrait,__ATOMIC_ACQUIRE)==portrait)return;
  usleep(10000);
 }
 assert(!"orientation route was not updated");
}
int main(void)
{
 setbuf(stdout,NULL);signal(SIGUSR1,stop);
 shared=mmap(NULL,sizeof(*shared),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
 assert(shared!=MAP_FAILED);
 for(int mode=0;mode<6;mode++){
  memset(shared,0,sizeof(*shared));updates=0;stopping=0;busy_once=mode==3;fatal_restore=mode==5;
  struct gkd_app_menu_launch menu=GKD_APP_MENU_LAUNCH_INIT;
  struct gkd_menu_profile profile={.action=GKD_MENU_NATIVE};
  assert(gkd_input_route_compile(0,KEY_HOME,KEY_END,&profile,&menu.base)==1);
  route_fd=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK);assert(route_fd>=0);menu.fd=route_fd;
  struct gkd_app_orientation orientation=GKD_APP_ORIENTATION_INIT;
  orientation.launch=(struct gkd_game_orientation){1,GKD_ASPECT_PORTRAIT,0,GKD_ORIENTATION_UNKNOWN,GKD_ORIENTATION_UNKNOWN,GKD_ORIGIN_WS_FOOTER,GKD_SCOPE_LAUNCH_ROM,0};
  int life[2]={-1,-1};pid_t helper=-1;
  if(mode==3||mode==5){
   assert(!pipe2(life,O_CLOEXEC));orientation.lifetime=life[0];orientation.page=&shared->page;
   orientation.launch=(struct gkd_game_orientation)GKD_GAME_ORIENTATION_INIT;
   helper=fork();assert(helper>=0);
   if(!helper){
    close(route_fd);close(life[0]);
    if(mode==5){
     __atomic_store_n(&shared->page.aspect,GKD_ASPECT_PORTRAIT,__ATOMIC_RELEASE);observed(1);
     __atomic_store_n(&shared->page.aspect,GKD_ASPECT_LANDSCAPE,__ATOMIC_RELEASE);close(life[1]);_exit(0);
    }
    unsigned aspects[]={GKD_ASPECT_PORTRAIT,GKD_ASPECT_LANDSCAPE,GKD_ASPECT_PORTRAIT,GKD_ASPECT_UNKNOWN,GKD_ASPECT_PORTRAIT};
    for(unsigned i=0;i<sizeof(aspects)/sizeof(aspects[0]);i++){
     __atomic_store_n(&shared->page.aspect,aspects[i],__ATOMIC_RELEASE);
     observed(aspects[i]==GKD_ASPECT_PORTRAIT);
    }
    close(life[1]);observed(0);_exit(0);
   }
   close(life[1]);
  }else if(mode==4){
   pid_t owner=getpid();helper=fork();assert(helper>=0);
   if(!helper){close(route_fd);observed(1);assert(!kill(owner,SIGUSR1));_exit(0);}
  }
  char *normal[]={"/bin/true",NULL};char *failed[]={"/no-gkd-executable",NULL};
  char *crash[]={"/bin/sh","-c","kill -SEGV $$",NULL};
  char *dynamic[]={"/bin/sleep","2",NULL};char *forced[]={"/bin/sleep","30",NULL};
  char **args=mode==0?normal:mode==1?failed:mode==2?crash:(mode==3||mode==5)?dynamic:forced;
  int listener=gkd_app_game_listen(7000+mode);assert(listener>=0);
  struct gkd_app_payload_result result;
  int rc=gkd_app_payload_run_orientation(args,"/",listener,7000+mode,&stopping,NULL,&menu,&orientation,&result);
  assert(mode==5?(rc==-1&&errno==EIO):rc==0);
  assert(result.reaped&&result.forced==(mode==4||mode==5));
  int code=WIFEXITED(result.wait_status)?WEXITSTATUS(result.wait_status):128+WTERMSIG(result.wait_status);
  assert(code==(mode==1?127:mode==2?139:(mode==4||mode==5)?137:0));
  if(helper>0){int status;assert(waitpid(helper,&status,0)==helper&&WIFEXITED(status)&&!WEXITSTATUS(status));}
  if(mode==3)assert(updates==6&&!menu.portrait&&!busy_once);
  else assert(updates==1&&menu.portrait);
  /* This is the launcher cleanup boundary before SM resumes. Actual kernel
   * release and the first native A press are tested by input_route_fixture. */
  gkd_app_menu_close(&menu);assert(menu.fd==-1&&!menu.portrait&&!menu.orientation_disabled);
  assert(fcntl(route_fd,F_GETFD)<0&&errno==EBADF);
  if(life[0]>=0)close(life[0]);
  close(listener);
  printf("GKD_PORTRAIT_PAYLOAD=PASS case=%d updates=%d exit=%d lease_closed=1\n",mode,updates,code);
 }
 assert(!munmap(shared,sizeof(*shared)));return 0;
}
