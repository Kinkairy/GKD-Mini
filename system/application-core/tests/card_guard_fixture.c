/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#define GKD_CARD_PROC "/tmp/gkd-card-guard-fixture/proc"
#define GKD_CARD_SYS "/tmp/gkd-card-guard-fixture/sys"
#define GKD_CARD_DEV "/tmp/gkd-card-guard-fixture/dev"
#define main gkd_card_main
#include "../source/gkd-app-card-guard.c"
#undef main
#include <assert.h>
#include <stdarg.h>
static int loop_mode;
int __real_stat(const char *,struct stat *);
int __wrap_stat(const char *p,struct stat *s)
{
    if(!strcmp(p,GKD_CARD_DEV "/mmcblk0")||!strcmp(p,GKD_CARD_DEV "/mmcblk0p1")||
       !strcmp(p,GKD_CARD_DEV "/mmcblk0p2")||!strcmp(p,GKD_CARD_DEV "/mmcblk0p3")){
        memset(s,0,sizeof(*s));s->st_mode=S_IFBLK|0600;
        s->st_rdev=makedev(179,strstr(p,"p1")?1:strstr(p,"p2")?2:
            strstr(p,"p3")?3:0);return 0;
    }
    return __real_stat(p,s);
}
int __wrap_ioctl(int fd,unsigned long request,...)
{
    va_list ap;(void)fd;assert(request==LOOP_GET_STATUS64);
    va_start(ap,request);struct loop_info64 *i=va_arg(ap,struct loop_info64 *);va_end(ap);
    if(loop_mode==0){errno=ENXIO;return -1;}
    if(loop_mode==3){errno=EIO;return -1;}
    memset(i,0,sizeof(*i));
    if(loop_mode==6||loop_mode==7||loop_mode==8){
        i->lo_device=(uint64_t)makedev(0,19); /* devtmpfs containing block node */
        i->lo_rdevice=(uint64_t)makedev(179,loop_mode==6?3:loop_mode==8?0:9);
    }else i->lo_device=(uint64_t)makedev(179,
        loop_mode==1?1:loop_mode==4?3:loop_mode==5?2:9);
    return 0;
}
static void put(const char *p,const char *text)
{
    FILE *f=fopen(p,"w");assert(f);assert(fputs(text,f)>=0);assert(!fclose(f));
}
int main(void)
{
    char cmd[1024],mountpath[256];int n;
    n=snprintf(cmd,sizeof(cmd),"mkdir -p " GKD_CARD_PROC "/%ld " GKD_CARD_SYS "/class/block/mmcblk0/mmcblk0p1 " GKD_CARD_SYS "/class/block/mmcblk0/mmcblk0p3 " GKD_CARD_SYS "/class/block/mmcblk0p3 " GKD_CARD_SYS "/block/loop0 " GKD_CARD_DEV,(long)getpid());
    assert(n>0&&n<(int)sizeof(cmd));assert(!system(cmd));
    snprintf(mountpath,sizeof(mountpath),GKD_CARD_PROC "/%ld/mountinfo",(long)getpid());
    put(GKD_CARD_SYS "/class/block/mmcblk0/dev","179:0\n");
    put(GKD_CARD_SYS "/class/block/mmcblk0/mmcblk0p1/dev","179:1\n");
    put(GKD_CARD_SYS "/class/block/mmcblk0/mmcblk0p3/dev","179:3\n");
    put(GKD_CARD_SYS "/class/block/mmcblk0p3/dev","179:3\n");
    put(GKD_CARD_PROC "/swaps","Filename Type Size Used Priority\n");
    put(GKD_CARD_DEV "/loop0","");
    put(mountpath,"12 1 0:1 / / rw - tmpfs none rw\n");
    char *args[]={"guard","mmcblk0",NULL};
    assert(!gkd_card_main(2,args));
    put(mountpath,"12 1 179:1 / /data rw - ext4 /dev/root rw\n");
    assert(gkd_card_main(2,args)==1&&errno==EBUSY); /* alias still blocked */
    put(mountpath,"12 1 179:0 / /data ro - ext4 /unrelated-spelling ro\n");
    assert(gkd_card_main(2,args)==1&&errno==EBUSY); /* read-only is still mounted */
    put(mountpath,"bad record\n");assert(gkd_card_main(2,args)==1);
    assert(!unlink(mountpath));assert(gkd_card_main(2,args)==1); /* live unreadable */
    put(mountpath,"12 1 179:9 / /other rw - ext4 /dev/mmcblk1p1 rw\n");
    assert(!gkd_card_main(2,args));
    put(GKD_CARD_PROC "/swaps","Filename Type Size Used Priority\n" GKD_CARD_DEV "/mmcblk0p1 partition 10 0 -1\n");
    assert(gkd_card_main(2,args)==1&&errno==EBUSY);
    put(GKD_CARD_PROC "/swaps","Filename Type Size Used Priority\n");
    loop_mode=1;assert(gkd_card_main(2,args)==1&&errno==EBUSY);
    loop_mode=2;assert(!gkd_card_main(2,args));
    loop_mode=3;assert(gkd_card_main(2,args)==1);
    loop_mode=0;put(GKD_CARD_SYS "/class/block/mmcblk0/dev","179:2\n");
    assert(gkd_card_main(2,args)==1);

    put(GKD_CARD_SYS "/class/block/mmcblk0/dev","179:0\n");
    char *p3args[]={"guard","mmcblk0p3",NULL};
    char *badargs[]={"guard","mmcblk0p2",NULL};
    put(mountpath,"12 1 179:1 / /p1 ro - ext4 /dev/root ro\n"
                  "13 1 179:2 / /p2 rw - ext3 /dev/root rw\n");
    put(GKD_CARD_PROC "/swaps","Filename Type Size Used Priority\n"
        GKD_CARD_DEV "/mmcblk0p1 partition 10 0 -1\n"
        GKD_CARD_DEV "/mmcblk0p2 partition 10 0 -1\n");
    assert(!gkd_card_main(2,p3args)); /* live P1/P2 mounts and swaps permitted */
    put(GKD_CARD_PROC "/swaps","Filename Type Size Used Priority\n");
    loop_mode=1;assert(!gkd_card_main(2,p3args)); /* P1 loop permitted */
    loop_mode=5;assert(!gkd_card_main(2,p3args)); /* P2 loop permitted */
    loop_mode=0;
    put(mountpath,"12 1 179:3 / /swap ro - ext4 /alias ro\n");
    assert(gkd_card_main(2,p3args)==1&&errno==EBUSY);
    put(mountpath,"12 1 179:1 / /p1 ro - ext4 /dev/root ro\n");
    put(GKD_CARD_PROC "/swaps","Filename Type Size Used Priority\n"
        GKD_CARD_DEV "/mmcblk0p3 partition 10 0 -1\n");
    assert(gkd_card_main(2,p3args)==1&&errno==EBUSY);
    put(GKD_CARD_PROC "/swaps","Filename Type Size Used Priority\n");
    loop_mode=4;assert(gkd_card_main(2,p3args)==1&&errno==EBUSY);
    loop_mode=6;assert(gkd_card_main(2,p3args)==1&&errno==EBUSY);
    loop_mode=8;assert(gkd_card_main(2,p3args)==1&&errno==EBUSY); /* whole parent overlaps P3 */
    loop_mode=7;assert(!gkd_card_main(2,p3args));
    loop_mode=0;put(GKD_CARD_SYS "/class/block/mmcblk0p3/dev","179:4\n");
    assert(gkd_card_main(2,p3args)==1);
    put(GKD_CARD_SYS "/class/block/mmcblk0p3/dev","179:3\n");
    put(GKD_CARD_SYS "/class/block/mmcblk0/dev","179:4\n");
    assert(gkd_card_main(2,p3args)==1);
    assert(gkd_card_main(2,badargs)==64);
    puts("GKD_CARD_GUARD_FIXTURE=PASS alias/ro/malformed/live-read/swap/loop/identity/p3-parent-overlap-p1p2-live-block-rdev");
    return 0;
}
