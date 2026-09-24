/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_STORAGE_H
#define GKD_APP_STORAGE_H
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#define GKD_STORAGE_POLICY "system-core-gamecard-v1"
/* Only called after CLONE_NEWNS and MS_PRIVATE. No fallback to system HOME. */
static inline int gkd_storage_game(void)
{
 struct stat card,device,parent; struct statvfs space;
 if(stat("/media/sdcard",&card)||stat("/media",&parent)||
    stat("/dev/mmcblk1p1",&device))return -1;
 if(!S_ISBLK(device.st_mode)||card.st_dev!=device.st_rdev||card.st_dev==parent.st_dev){errno=ENODEV;return -1;}
 if(statvfs("/media/sdcard",&space))return -1;
 if(space.f_flag&ST_RDONLY){errno=EROFS;return -1;}
 if(!space.f_bavail){errno=ENOSPC;return -1;}
 int root=open("/media/sdcard",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
 if(root<0)return -1;
 const char *names[]={"emulators","bios"};
 for(unsigned i=0;i<2;i++){
  if(mkdirat(root,names[i],0755)&&errno!=EEXIST){int e=errno;close(root);errno=e;return -1;}
  int fd=openat(root,names[i],O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
  if(fd<0){int e=errno;close(root);errno=e;return -1;}
  struct stat st;
  if(fstat(fd,&st)||st.st_dev!=card.st_dev){close(fd);close(root);errno=EXDEV;return -1;}
  if(i==0){
   if(mkdirat(fd,"home",0755)&&errno!=EEXIST){int e=errno;close(fd);close(root);errno=e;return -1;}
   int home=openat(fd,"home",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
   if(home<0){int e=errno;close(fd);close(root);errno=e;return -1;}
   if(fstat(home,&st)||st.st_dev!=card.st_dev){close(home);close(fd);close(root);errno=EXDEV;return -1;}
   close(home);
  }
  close(fd);
 }
 close(root);
 if(mount("/media/data","/media/data",NULL,MS_BIND,NULL)||
    mount(NULL,"/media/data",NULL,MS_BIND|MS_REMOUNT|MS_RDONLY|MS_NOSUID|MS_NODEV,NULL)||
    mount("/media/sdcard/emulators/home","/media/data/local/home",NULL,MS_BIND,NULL)||
    mount("/media/sdcard/bios","/media/data/bios",NULL,MS_BIND,NULL))return -1;
 return setenv("HOME","/media/sdcard/emulators/home",1);
}
/* Create only the frontend state directory on the actual game card. The same
 * hook runs at boot and after remount; links never fall back to system storage. */
static inline int gkd_storage_preferences(const char *prefix)
{
 char path[256];struct stat card,device;int a=-1,b=-1,c=-1,rc=-1;
 if(snprintf(path,sizeof(path),"%s/media/sdcard",prefix)>=(int)sizeof(path)){errno=ENAMETOOLONG;return -1;}
 if(stat(path,&card)||stat("/dev/mmcblk1p1",&device))return -1;
 if(!S_ISBLK(device.st_mode)||card.st_dev!=device.st_rdev){errno=ENODEV;return -1;}
 a=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(a<0)return -1;
 if(mkdirat(a,"frontend",0755)&&errno!=EEXIST)goto done;
 b=openat(a,"frontend",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(b<0)goto done;
 if(mkdirat(b,"simplemenu",0755)&&errno!=EEXIST)goto done;
 c=openat(b,"simplemenu",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(c<0)goto done;
 if(mkdirat(c,"rom_preferences",0755)&&errno!=EEXIST)goto done;
 int d=openat(c,"rom_preferences",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(d<0)goto done;
 close(d);
 int f=openat(c,"favorites.sav",O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0644);
 if(f>=0)close(f);else if(errno!=EEXIST)goto done;
 rc=0;
 done:;int saved=errno;if(c>=0)close(c);if(b>=0)close(b);close(a);errno=saved;return rc;
}
/* Copy the bounded system menu seed into RAM, never emulator/user directories. */
static inline int gkd_storage_seed(int src,int dst,unsigned depth,size_t *budget)
{
 if(depth>4){errno=ELOOP;return -1;}
 DIR *d=fdopendir(dup(src));if(!d)return -1;
 struct dirent *ent;int rc=0;
 for(;;){
  errno=0;ent=readdir(d);if(!ent){if(errno)rc=-1;break;}
  if(!strcmp(ent->d_name,".")||!strcmp(ent->d_name,".."))continue;
  struct stat st;if(fstatat(src,ent->d_name,&st,AT_SYMLINK_NOFOLLOW)){rc=-1;break;}
  int a=-1,b=-1;
  if(S_ISDIR(st.st_mode)){
   if(mkdirat(dst,ent->d_name,0755)&&errno!=EEXIST){rc=-1;break;}
   a=openat(src,ent->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
   b=openat(dst,ent->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
   if(a<0||b<0||gkd_storage_seed(a,b,depth+1,budget))rc=-1;
  }else if(S_ISREG(st.st_mode)&&st.st_size>=0&&(size_t)st.st_size<=*budget){
   *budget-=(size_t)st.st_size;
   a=openat(src,ent->d_name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
   b=openat(dst,ent->d_name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0644);
   if(a<0||b<0)rc=-1;
   char buf[4096];ssize_t n;
   while(!rc&&(n=read(a,buf,sizeof(buf)))>0){ssize_t off=0;while(off<n){ssize_t w=write(b,buf+off,(size_t)(n-off));if(w<=0){rc=-1;break;}off+=w;}}
   if(!rc&&n<0)rc=-1;
  }else{errno=EINVAL;rc=-1;}
  int saved=errno;if(a>=0)close(a);if(b>=0)close(b);errno=saved;if(rc)break;
 }
 int saved=errno;closedir(d);errno=saved;return rc;
}
static inline int gkd_storage_frontend(void)
{
 const char *home="/newroot/media/data/local/home/.simplemenu";
 int src=open("/newroot/media/data/local/home/.simplemenu",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
 if(src<0)return -1;
 int rc=mount("tmpfs",home,"tmpfs",MS_NOSUID|MS_NODEV|MS_NOEXEC,"size=16m,mode=0755");
 int dst=-1;size_t budget=2U*1024U*1024U;
 if(!rc){
  if(!rc){dst=open("/newroot/media/data/local/home/.simplemenu",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dst<0)rc=-1;}
  if(!rc)rc=gkd_storage_seed(src,dst,0,&budget);
  /* Frontend settings are system settings; keep the single persistent config.
   * Personal selections/favourites belong to the game card, logs/cache to RAM. */
  if(!rc){char config[96];snprintf(config,sizeof(config),"/proc/self/fd/%d/config.ini",src);
   rc=mount(config,"/newroot/media/data/local/home/.simplemenu/config.ini",NULL,MS_BIND,NULL);}
  if(!rc){
   (void)gkd_storage_preferences("/newroot");
   const char *names[]={"favorites.sav","last_state.sav","rom_preferences"};
   for(unsigned i=0;i<3&&!rc;i++){
    char target[128];snprintf(target,sizeof(target),"/media/sdcard/frontend/simplemenu/%s",names[i]);
    if(unlinkat(dst,names[i],0)&&errno!=ENOENT){rc=-1;break;}
    if(symlinkat(target,dst,names[i]))rc=-1;
   }
  }
  if(!rc)rc=mkdir("/newroot/media/data/local/home/.simplemenu/themes",0755);
  if(!rc)rc=mount("/newroot/mnt/SimpleMenu/themes","/newroot/media/data/local/home/.simplemenu/themes",NULL,MS_BIND,NULL);
  if(!rc)rc=mount(NULL,"/newroot/media/data/local/home/.simplemenu/themes",NULL,MS_BIND|MS_REMOUNT|MS_RDONLY|MS_NOSUID|MS_NODEV|MS_NOEXEC,NULL);
 }
 int saved=errno;close(src);if(dst>=0)close(dst);errno=saved;return rc;
}
#endif
