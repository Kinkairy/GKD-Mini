/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#define GKD_APP_CONFIG_RUNTIME "/tmp/gkd-config-store"
#define GKD_APP_CONFIG_EMBEDDED "/tmp/gkd-config-store/profile.conf"
#define GKD_APP_CONFIG_P2 "/tmp/gkd-config-store/device"
#define GKD_APP_CONFIG_EXEC "/bin/true"
#define GKD_APP_CONFIG_GUARD "/bin/true"
#define main gkd_config_store_main
#include "../source/gkd-app-config-store.c"
#undef main
#include <assert.h>
static dev_t expected_device;static int wrong_device,fail_sync;
int __real_stat(const char *,struct stat *);
int __wrap_stat(const char *path,struct stat *st)
{
 if(strcmp(path,GKD_APP_CONFIG_P2))return __real_stat(path,st);
 memset(st,0,sizeof(*st));st->st_mode=S_IFBLK|0600;st->st_rdev=expected_device+(wrong_device?1:0);return 0;
}
int __real_fsync(int fd);
int __wrap_fsync(int fd){if(fail_sync){errno=EIO;return -1;}return __real_fsync(fd);}
static void file(const char *path,const char *text)
{FILE *f=fopen(path,"w");assert(f);assert(fputs(text,f)>=0);assert(!fclose(f));assert(!chmod(path,0600));}
static void equals(const char *path,const char *text)
{char buf[256]={0};FILE *f=fopen(path,"r");assert(f);size_t n=fread(buf,1,sizeof(buf),f);assert(!ferror(f)&&feof(f)&&n<sizeof(buf));fclose(f);assert(!strcmp(buf,text));}
int main(void)
{
 const char *runtime=GKD_APP_CONFIG_RUNTIME "/config.override.conf";
 const char *override="/tmp/gkd-config-store/etc/gkd-mini/gdkmini.override.conf";
 assert(!mkdir(GKD_APP_CONFIG_RUNTIME,0700));assert(!mkdir(GKD_APP_CONFIG_RUNTIME "/etc",0700));
 int etc=open(GKD_APP_CONFIG_RUNTIME "/etc",O_RDONLY|O_DIRECTORY);assert(etc>=0);
 struct stat st;assert(!fstat(etc,&st));expected_device=st.st_dev;
 file(GKD_APP_CONFIG_EMBEDDED,"ui_dynamic_effects=enabled\n");
 assert(!load_override(etc));equals(runtime,"ui_dynamic_effects=enabled\n");
 assert(!mkdir(GKD_APP_CONFIG_RUNTIME "/etc/gkd-mini",0700));
 file(override,"ui_dynamic_effects=disabled\n");
 assert(!load_override(etc));equals(runtime,"ui_dynamic_effects=disabled\n");
 assert(!unlink(override));assert(!symlink(GKD_APP_CONFIG_EMBEDDED,override));
 assert(load_override(etc)<0);equals(runtime,"ui_dynamic_effects=disabled\n");
 assert(!unlink(override));file(override,"ui_dynamic_effects=enabled\n");fail_sync=1;
 assert(load_override(etc)<0);equals(runtime,"ui_dynamic_effects=disabled\n");fail_sync=0;
 wrong_device=1;assert(load_override(etc)<0&&errno==EPERM);wrong_device=0;
 assert(!load_override(etc));equals(runtime,"ui_dynamic_effects=enabled\n");
 assert(!chmod(GKD_APP_CONFIG_RUNTIME,0777));assert(load_override(etc)<0&&errno==EPERM);
 assert(!chmod(GKD_APP_CONFIG_RUNTIME,0700));
 close(etc);
 assert(!unlink(override));assert(!unlink(runtime));assert(!unlink(GKD_APP_CONFIG_EMBEDDED));
 assert(!rmdir(GKD_APP_CONFIG_RUNTIME "/etc/gkd-mini"));assert(!rmdir(GKD_APP_CONFIG_RUNTIME "/etc"));assert(!rmdir(GKD_APP_CONFIG_RUNTIME));
 puts("GKD_APP_CONFIG_STORE_FIXTURE=PASS defaults/override/load/no-symlink/no-old-fallback/fsync/identity/permissions");
 return 0;
}
