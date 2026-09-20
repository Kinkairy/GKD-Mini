/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-profile.h"
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/mount.h>

int gkd_app_bind_readonly(const char *source, const char *target)
{
    if (mount(source, target, NULL, MS_BIND, NULL)) return -1;
    return mount(NULL, target, NULL,
                 MS_BIND | MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV, NULL);
}

int gkd_app_profile_mount(const char *profile, const char *target)
{
    char source_library[PATH_MAX], target_library[PATH_MAX];
    if (!profile || !target) { errno = EINVAL; return -1; }
    int a = snprintf(source_library, sizeof(source_library),
                     "%s/libgkd-sm-present.so", profile);
    int b = snprintf(target_library, sizeof(target_library),
                     "%s/libgkd-sm-present.so", target);
    if (a < 0 || b < 0) { errno = EINVAL; return -1; }
    if ((size_t)a >= sizeof(source_library) || (size_t)b >= sizeof(target_library)) {
        errno = ENAMETOOLONG; return -1;
    }
    if (gkd_app_bind_readonly(profile, target)) return -1;
    /* A nonrecursive directory bind exposes the underlying empty placeholder,
     * not the library mounted on it. Carry only this required file mount into
     * the child; unrelated nested mounts must not become application inputs. */
    if(gkd_app_bind_readonly(source_library, target_library))return -1;
    a=snprintf(source_library,sizeof(source_library),"%s/libgkd-fps-present.so",profile);
    b=snprintf(target_library,sizeof(target_library),"%s/libgkd-fps-present.so",target);
    if(a<0||b<0||(size_t)a>=sizeof(source_library)||(size_t)b>=sizeof(target_library)){errno=ENAMETOOLONG;return -1;}
    if(gkd_app_bind_readonly(source_library,target_library))return -1;
    a=snprintf(source_library,sizeof(source_library),"%s/gkd-app-game",profile);
    b=snprintf(target_library,sizeof(target_library),"%s/gkd-app-game",target);
    if(a<0||b<0||(size_t)a>=sizeof(source_library)||(size_t)b>=sizeof(target_library)){errno=ENAMETOOLONG;return -1;}
    if(gkd_app_bind_readonly(source_library,target_library))return -1;
    a=snprintf(source_library,sizeof(source_library),"%s/input-routing.conf",profile);
    b=snprintf(target_library,sizeof(target_library),"%s/input-routing.conf",target);
    if(a<0||b<0||(size_t)a>=sizeof(source_library)||(size_t)b>=sizeof(target_library)){errno=ENAMETOOLONG;return -1;}
    if(gkd_app_bind_readonly(source_library,target_library))return -1;
    a=snprintf(source_library,sizeof(source_library),"%s/input-config",profile);
    b=snprintf(target_library,sizeof(target_library),"%s/input-config",target);
    if(a<0||b<0||(size_t)a>=sizeof(source_library)||(size_t)b>=sizeof(target_library)){errno=ENAMETOOLONG;return -1;}
    return gkd_app_bind_readonly(source_library,target_library);
}

/* New A action enters through the existing launcher item, with no old executor. */
int gkd_app_update_entry_mount(const char *socket_path,const char *service_path,const char *root)
{
 char directory[PATH_MAX],socket_target[PATH_MAX],entry[PATH_MAX];struct stat st;
 if(!socket_path||!service_path||!root){errno=EINVAL;return -1;}
 if(snprintf(directory,sizeof(directory),"%s/var/run/gkd-application",root)>=(int)sizeof(directory)||
    snprintf(socket_target,sizeof(socket_target),"%s/control.sock",directory)>=(int)sizeof(socket_target)||
    snprintf(entry,sizeof(entry),"%s/usr/sbin/gkd-system-update",root)>=(int)sizeof(entry)){
  errno=ENAMETOOLONG;return -1;
 }
 if(lstat(socket_path,&st)||!S_ISSOCK(st.st_mode)||st.st_uid||(st.st_mode&0077)||
    lstat(service_path,&st)||!S_ISREG(st.st_mode)||st.st_uid||(st.st_mode&0022)||!(st.st_mode&0111)||
    lstat(entry,&st)||!S_ISREG(st.st_mode)){errno=EPERM;return -1;}
 if(mkdir(directory,0700))return -1;
 int placeholder=open(socket_target,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);
 if(placeholder<0)return -1;
 if(close(placeholder))return -1;
 if(gkd_app_bind_readonly(socket_path,socket_target))return -1;
 return gkd_app_bind_readonly(service_path,entry);
}

#ifndef GKD_APP_PROFILE_RUN
#define GKD_APP_PROFILE_RUN "/run"
#endif
int gkd_app_profile_cleanup(pid_t host_pid)
{
    static const char *const files[]={"inittab","libgkd-sm-present.so","libgkd-fps-present.so",
                                      "child-output.log","gkd-app-game","loop-owner","input-routing.conf","input-config"};
    char name[64];struct stat parent_stat,dir_stat,st;DIR *listing=NULL;
    int parent=-1,dir=-1,rc=-1,saved;
    if(host_pid<=1){errno=EINVAL;return -1;}
    snprintf(name,sizeof(name),"gkd-app-profile.%ld",(long)host_pid);
    parent=open(GKD_APP_PROFILE_RUN,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if(parent<0)return -1;
    if(fstat(parent,&parent_stat))goto out;
    dir=openat(parent,name,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if(dir<0)goto out;
    if(fstat(dir,&dir_stat)||dir_stat.st_uid||dir_stat.st_mode&0077||dir_stat.st_dev!=parent_stat.st_dev){errno=EPERM;goto out;}
    listing=fdopendir(dup(dir));if(!listing)goto out;
    for(;;){
        struct dirent *entry;errno=0;entry=readdir(listing);
        if(!entry){if(errno)goto out;break;}
        if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
        unsigned i;for(i=0;i<sizeof(files)/sizeof(files[0]);i++)if(!strcmp(entry->d_name,files[i]))break;
        if(i==sizeof(files)/sizeof(files[0])){errno=EPERM;goto out;}
        if(fstatat(dir,entry->d_name,&st,AT_SYMLINK_NOFOLLOW)||(i==7U?!S_ISDIR(st.st_mode):!S_ISREG(st.st_mode))||
           st.st_dev!=dir_stat.st_dev||st.st_uid||(i!=7U&&st.st_nlink!=1U)){errno=EPERM;goto out;}
    }
    for(unsigned i=0;i<sizeof(files)/sizeof(files[0]);i++)if(unlinkat(dir,files[i],i==7U?AT_REMOVEDIR:0)&&errno!=ENOENT)goto out;
    rc=unlinkat(parent,name,AT_REMOVEDIR);
out:
    saved=errno;if(listing)closedir(listing);if(dir>=0)close(dir);close(parent);errno=saved;return rc;
}
