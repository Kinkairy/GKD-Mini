/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
/* Include the production check; unreferenced lifecycle sections are discarded. */
#include "../source/gkd-app-prepare.c"
#include <assert.h>
#include <dirent.h>
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>

static int injected_error;
int __real_fstatfs(int fd, struct statfs *fs);
int __wrap_fstatfs(int fd, struct statfs *fs)
{
    if (injected_error) { errno = injected_error; return -1; }
    return __real_fstatfs(fd, fs);
}
static unsigned fd_count(void)
{
    DIR *d=opendir("/proc/self/fd"); assert(d);
    unsigned n=0; while(readdir(d)) n++;
    assert(!closedir(d)); return n;
}
static void check(int expected)
{
    unsigned before=fd_count();
    errno=0; int rc=empty_inittab(), saved=errno;
    assert(expected ? rc==-1 && saved==expected : rc==0);
    assert(fd_count()==before);
}
/* Reproduction model of pinned libc.a fstatvfs.os 0x174..0x1a4:
 * select the FIRST stat(mnt_dir).st_dev match, then parse its mount flags.
 * This is evidence only, never a production compatibility implementation. */
static int old_readonly_guess(void)
{
    struct stat target, candidate;
    assert(!stat("/etc/inittab",&target));
    FILE *f=setmntent("/proc/mounts","r"); assert(f);
    struct mntent *m; int result=-1;
    while((m=getmntent(f))) {
        if(!stat(m->mnt_dir,&candidate) && target.st_dev==candidate.st_dev) {
            assert(!strcmp(m->mnt_dir,"/run/gkd-menu-owner"));
            result=hasmntopt(m,"ro")!=NULL; break;
        }
    }
    endmntent(f); assert(result>=0); return result;
}
static void dir(const char *p) { assert(!mkdir(p,0700)); }
static void file(const char *p)
{
    int fd=open(p,O_CREAT|O_EXCL|O_RDWR,0400); assert(fd>=0); assert(!close(fd));
}
int main(int argc,char **argv)
{
    assert(argc==2 && geteuid()==0);
    assert(!mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL));
    assert(!chdir(argv[1]));
    dir("source"); dir("root");
    assert(!mount("tmpfs","source","tmpfs",MS_NOSUID|MS_NODEV,"mode=0700,size=1m"));
    dir("source/guard"); file("source/inittab");
    int source=open("source/inittab",O_RDWR|O_CLOEXEC); assert(source>=0);
    dir("root/etc"); dir("root/run"); dir("root/run/gkd-menu-owner");
    dir("root/proc"); file("root/etc/inittab");
    assert(!mount("proc","root/proc","proc",MS_NOSUID|MS_NODEV|MS_NOEXEC,NULL));
    assert(!mount("source/guard","root/run/gkd-menu-owner",NULL,MS_BIND,NULL));
    assert(!mount("source/inittab","root/etc/inittab",NULL,MS_BIND,NULL));
    assert(!mount(NULL,"root/etc/inittab",NULL,MS_BIND|MS_REMOUNT|MS_RDONLY|MS_NOSUID|MS_NODEV,NULL));
    assert(!chroot("root") && !chdir("/"));
    assert(old_readonly_guess()==0); check(0);
    puts("PASS same-device RW guard before RO inittab: old guess rejects; exact-fd accepts");
    assert(!mount(NULL,"/etc/inittab",NULL,MS_BIND|MS_REMOUNT|MS_NOSUID|MS_NODEV,NULL));
    check(EPERM);
    assert(!mount(NULL,"/run/gkd-menu-owner",NULL,MS_BIND|MS_REMOUNT|MS_RDONLY|MS_NOSUID|MS_NODEV,NULL));
    assert(old_readonly_guess()==1); check(EPERM);
    puts("PASS same-device RO guard before RW inittab: old guess accepts; exact-fd refuses");
    assert(!mount(NULL,"/etc/inittab",NULL,MS_BIND|MS_REMOUNT|MS_RDONLY|MS_NOSUID|MS_NODEV,NULL));
    check(0);
    assert(!fchmod(source,0620)); check(EPERM); assert(!fchmod(source,0400));
    assert(write(source,"x",1)==1); check(EPERM); assert(!ftruncate(source,0)); check(0);
    injected_error=EIO; check(EIO); injected_error=ENOSYS; check(ENOSYS);
    injected_error=0; check(0); assert(!close(source));
    assert(!umount("/etc/inittab")); assert(!unlink("/etc/inittab"));
    assert(!symlink("/run/gkd-menu-owner","/etc/inittab")); check(ELOOP);
    assert(!unlink("/etc/inittab")); check(ENOENT);
    dir("/etc/inittab"); check(EPERM);
    puts("PASS RW, writable mode, nonempty, syscall errors, symlink, missing, directory refusal; fd count unchanged");
    return 0;
}
