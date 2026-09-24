/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <assert.h>
#include <sched.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include "gkd-app-storage.h"
static void dir(const char *p){assert(!mkdir(p,0755)||errno==EEXIST);}
static void put(const char *p){int fd=open(p,O_WRONLY|O_CREAT|O_TRUNC,0644);assert(fd>=0);assert(write(fd,"test",4)==4);close(fd);}
static void test(unsigned mode){
 assert(!unshare(CLONE_NEWNS));assert(!mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL));
 dir("/media");dir("/media/data");assert(!mount("tmpfs","/media/data","tmpfs",0,"size=1m"));
 dir("/media/data/local");dir("/media/data/local/home");dir("/media/data/local/etc");dir("/media/data/bios");put("/media/data/local/etc/test");
 dir("/media/sdcard");
 if(mode!=1)assert(!mount("tmpfs","/media/sdcard","tmpfs",0,"size=1m"));
 struct stat st;assert(!stat("/media/sdcard",&st));assert(!mknod("/dev/mmcblk1p1",S_IFBLK|0600,st.st_dev));
 if(mode==2)assert(!mount(NULL,"/media/sdcard",NULL,MS_REMOUNT|MS_RDONLY,NULL));
 if(mode==3){dir("/media/sdcard/emulators");assert(!symlink("/media/data/local/home","/media/sdcard/emulators/home"));}
 if(mode==4){int f=open("/media/sdcard/full",O_CREAT|O_WRONLY,0644);assert(f>=0);char b[4096]={0};while(write(f,b,sizeof b)>0){}assert(errno==ENOSPC);close(f);}
 if(mode==0){assert(!gkd_storage_preferences(""));put("/media/sdcard/frontend/simplemenu/favorites.sav");assert(!gkd_storage_preferences(""));struct stat kept;assert(!stat("/media/sdcard/frontend/simplemenu/favorites.sav",&kept)&&kept.st_size==4);}
 int rc=gkd_storage_game();
 if(mode){assert(rc<0);assert(!access("/media/data/local/etc/test",W_OK));}
 else{
  assert(!rc);assert(!strcmp(getenv("HOME"),"/media/sdcard/emulators/home"));
  int f=open("/media/data/local/etc/test",O_WRONLY);assert(f<0&&errno==EROFS);
  put("/media/data/local/home/game.sav");assert(!access("/media/sdcard/emulators/home/game.sav",F_OK));
  put("/media/data/bios/test");assert(!access("/media/sdcard/bios/test",F_OK));
 }
 assert(!unlink("/dev/mmcblk1p1"));
}
static void frontend(unsigned mode){
 assert(!unshare(CLONE_NEWNS));assert(!mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL));
 dir("/newroot");assert(!mount("tmpfs","/newroot","tmpfs",0,"size=8m"));
 const char *dirs[]={"/newroot/media","/newroot/media/data","/newroot/media/data/local","/newroot/media/data/local/home","/newroot/media/data/local/home/.simplemenu","/newroot/media/data/local/home/.gkdmini","/newroot/mnt","/newroot/mnt/SimpleMenu","/newroot/mnt/SimpleMenu/themes"};
 for(unsigned i=0;i<sizeof(dirs)/sizeof(*dirs);i++)dir(dirs[i]);
 put("/newroot/media/data/local/home/.gkdmini/hardware-state");
 put("/newroot/mnt/SimpleMenu/themes/theme.dat");
 const char *config="/newroot/media/data/local/home/.simplemenu/config.ini";
 if(mode==1)assert(!symlink("/etc/passwd",config));
 else{put(config);if(mode==2){int fd=open(config,O_WRONLY);assert(fd>=0);assert(!ftruncate(fd,3*1024*1024));close(fd);}}
 int old=open(config,O_RDONLY);assert(old>=0);
 int rc=gkd_storage_frontend();
 if(mode)assert(rc<0);
 else{
  assert(!rc);put(config);
  assert(!access("/newroot/media/data/local/home/.gkdmini/hardware-state",W_OK));
  int fd=open("/newroot/media/data/local/home/.simplemenu/themes/theme.dat",O_WRONLY);assert(fd<0&&errno==EROFS);
  struct stat a,b;assert(!fstat(old,&a));assert(!stat(config,&b));assert(a.st_dev==b.st_dev);
  char link[160];ssize_t n=readlink("/newroot/media/data/local/home/.simplemenu/favorites.sav",link,sizeof(link)-1);assert(n>0);link[n]=0;assert(!strcmp(link,"/media/sdcard/frontend/simplemenu/favorites.sav"));
 }
 close(old);
}
int main(void){for(unsigned i=0;i<8;i++){pid_t p=fork();assert(p>=0);if(!p){if(i<5)test(i);else frontend(i-5);_exit(0);}int status;assert(waitpid(p,&status,0)==p);assert(WIFEXITED(status)&&!WEXITSTATUS(status));}puts("STORAGE_TEST=PASS 8 cases: game normal/absent/readonly/symlink/full; frontend volatile/unsafe/oversize; system core state preserved");return 0;}
