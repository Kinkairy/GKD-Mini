/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-identity.h"
#include "gkd-update-sha256.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#ifndef GKD_APP_IDENTITY_DEVICE
#define GKD_APP_IDENTITY_DEVICE "/dev/mmcblk0"
#define GKD_APP_IDENTITY_CID "/sys/class/block/mmcblk0/device/cid"
#define GKD_APP_IDENTITY_P1_START "/sys/class/block/mmcblk0p1/start"
#endif
static int text(const char *path,char *buf,size_t size)
{
 int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW),rc=-1,saved;ssize_t n;
 if(fd<0)return -1;
 n=read(fd,buf,size);
 if(n>0&&(size_t)n<size&&buf[n-1]=='\n'&&!memchr(buf,0,(size_t)n)){buf[n-1]=0;rc=0;}
 else errno=EPROTO;
 saved=errno;close(fd);errno=saved;return rc;
}
int gkd_app_identity_read(struct gkd_app_identity *out)
{
 struct gkd_app_identity result={0};struct stat st;struct gkdu_sha256 hash;
 unsigned char buf[65536];char start[32],cid[40];int fd=-1,rc=-1,saved;off_t offset=0;
 const off_t prefix=20*1024*1024;
 if(!out){errno=EINVAL;return -1;}
 if(text(GKD_APP_IDENTITY_CID,cid,sizeof(cid))||strlen(cid)!=32U||
    strspn(cid,"0123456789abcdef")!=32U||
    text(GKD_APP_IDENTITY_P1_START,start,sizeof(start))||strcmp(start,"40960")){errno=EPROTO;return -1;}
 strcpy(result.cid,cid);
 fd=open(GKD_APP_IDENTITY_DEVICE,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0)return -1;
 if(fstat(fd,&st)||!S_ISBLK(st.st_mode)||major(st.st_rdev)!=179U||minor(st.st_rdev)!=0U||
    ioctl(fd,BLKGETSIZE64,&result.bytes)||result.bytes<=(uint64_t)prefix){errno=ENODEV;goto done;}
 gkdu_sha256_init(&hash);
 while(offset<prefix){
  ssize_t n=pread(fd,buf,sizeof(buf),offset);
  if(n<0&&errno==EINTR)continue;
  if(n<=0){if(!n)errno=EIO;goto done;}
  gkdu_sha256_update(&hash,buf,(size_t)n);offset+=n;
 }
 gkdu_sha256_final(&hash,result.prefix_sha256);
 /* Media replacement while hashing cannot become an accepted snapshot. */
 if(text(GKD_APP_IDENTITY_CID,cid,sizeof(cid))||strcmp(result.cid,cid)){errno=ESTALE;goto done;}
 rc=0;
done:
 saved=errno;close(fd);if(!rc)*out=result;errno=saved;return rc;
}
int gkd_app_identity_verify(const struct gkd_app_identity *accepted)
{
 struct gkd_app_identity current;
 if(!accepted){errno=EINVAL;return -1;}
 if(gkd_app_identity_read(&current))return -1;
 if(accepted->bytes!=current.bytes||strcmp(accepted->cid,current.cid)||
    memcmp(accepted->prefix_sha256,current.prefix_sha256,32U)){errno=ESTALE;return -1;}
 return 0;
}
