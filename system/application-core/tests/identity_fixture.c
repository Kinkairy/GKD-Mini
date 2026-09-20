/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-identity.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
static int failed_size;
int __real_fstat(int fd,struct stat *st);
int __wrap_fstat(int fd,struct stat *st)
{
 int r=__real_fstat(fd,st);if(!r){st->st_mode=S_IFBLK|0600;st->st_rdev=makedev(179,0);}return r;
}
int __wrap_ioctl(int fd,unsigned long request,...)
{
 (void)fd;assert(request==BLKGETSIZE64);va_list ap;va_start(ap,request);
 uint64_t *size=va_arg(ap,uint64_t *);va_end(ap);
 if(failed_size){errno=EIO;return -1;}*size=32U*1024U*1024U;return 0;
}
static void file(const char *path,const char *text)
{FILE *f=fopen(path,"w");assert(f);assert(fputs(text,f)>=0);assert(!fclose(f));}
int main(void)
{
 const char *device="/tmp/gkd-identity-device",*cid="/tmp/gkd-identity-cid",*start="/tmp/gkd-identity-start";
 int fd=open(device,O_RDWR|O_CREAT|O_EXCL,0600);assert(fd>=0);assert(!ftruncate(fd,32U*1024U*1024U));
 file(cid,"fe34325344313647200000bb5e01613f\n");file(start,"40960\n");
 struct gkd_app_identity original,copy;
 assert(!gkd_app_identity_read(&original));copy=original;assert(!gkd_app_identity_verify(&original));
 char byte=1;assert(pwrite(fd,&byte,1,25U*1024U*1024U)==1);assert(!gkd_app_identity_verify(&original));
 assert(pwrite(fd,&byte,1,1024)==1);assert(gkd_app_identity_verify(&original)<0&&errno==ESTALE);
 file(start,"40961\n");assert(gkd_app_identity_read(&copy)<0&&!memcmp(&copy,&original,sizeof(copy)));
 file(start,"40960\n");file(cid,"invalid\n");assert(gkd_app_identity_read(&copy)<0);
 file(cid,"fe34325344313647200000bb5e01613f\n");failed_size=1;assert(gkd_app_identity_read(&copy)<0);
 close(fd);assert(!unlink(device));assert(!unlink(cid));assert(!unlink(start));
 puts("GKD_APP_IDENTITY_FIXTURE=PASS boot-prefix/partition-data/layout/cid/device/atomic-output");
 return 0;
}
