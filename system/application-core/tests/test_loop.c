/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-loop.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/loop.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#define OWNER "gkd-app-0123456789abcdef0123456789abcdef"
#define OTHER "gkd-app-fedcba9876543210fedcba9876543210"

struct slot { int exists, busy, attached, backing_readonly; struct loop_info64 info; };
static struct slot slots[8];
static int scenario, force_clear_fail, mount_calls, clear_calls, close_calls, last_mount_flags, last_mount_index;
static int set_status_calls, set_fd_calls, get_calls;
enum { OK, ALL_BUSY, ABSENT, RACE, GET_EIO, BAD_IMAGE, NONREG_IMAGE, FSTAT_EIO, NONBLOCK,
       WRONG_MAJOR, SYMLINK, STATUS_FAIL, MOUNT_FAIL,
       CLEAR_FAIL, POSTCLEAR_BOUND, BAD_INFO, CLEANUP_BAD, FOREIGN, OLD_TAG };

static void init(void)
{
    memset(slots, 0, sizeof(slots));
    scenario = OK; force_clear_fail = 0; mount_calls = clear_calls = close_calls = last_mount_flags = last_mount_index = 0;
    set_status_calls = set_fd_calls = get_calls = 0;
    for (int i = 0; i < 8; ++i) slots[i].exists = 1;
}
static int indexfd(int fd) { return fd - 10; }
static void info_for(struct loop_info64 *p, const char *tag)
{
    memset(p, 0, sizeof(*p)); p->lo_flags = 0x200007U;
    memcpy(p->lo_file_name, tag, strlen(tag) + 1);
    p->lo_device = (0x22 & 0xff) | ((unsigned long long)0x11 << 8);
    p->lo_inode = 0x1234;
}

int __wrap_open(const char *path, int flags, ...)
{
    (void)flags;
    int n = -1;
    if (sscanf(path, "/dev/loop%d", &n) == 1 && n >= 0 && n < 8) {
        assert(flags == (O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        if (scenario == SYMLINK) { errno = ELOOP; return -1; }
        if (!slots[n].exists) { errno = ENOENT; return -1; }
        return 10 + n;
    }
    errno = ENOENT; return -1;
}
int __wrap_fstat(int fd, struct stat *st)
{
    memset(st, 0, sizeof(*st));
    if (fd == 100) { st->st_mode = scenario == NONREG_IMAGE ? S_IFBLK : S_IFREG; st->st_dev = makedev(0x11, 0x22); st->st_ino = 0x1234; return 0; }
    if (fd >= 10 && fd < 18) {
        if (scenario == FSTAT_EIO) { errno = EIO; return -1; }
        st->st_mode = scenario == NONBLOCK ? S_IFCHR : S_IFBLK;
        st->st_rdev = makedev(scenario == WRONG_MAJOR ? 8 : 7, fd - 10); return 0;
    }
    errno = EBADF; return -1;
}
int __wrap_fcntl(int fd, int cmd, ...)
{
    if (fd != 100 || cmd != F_GETFL) { errno = EBADF; return -1; }
    return scenario == BAD_IMAGE ? O_WRONLY : O_RDONLY;
}
int __wrap_close(int fd) { if (fd >= 10 && fd < 18) ++close_calls; return 0; }
int __wrap_mount(const char *source, const char *target, const char *type, unsigned long flags, const void *data)
{
    (void)data; assert(sscanf(source, "/dev/loop%d", &last_mount_index) == 1);
    assert(last_mount_index >= 0 && last_mount_index < 8); assert(!strcmp(target, "/mnt/a")); assert(!strcmp(type, "squashfs"));
    ++mount_calls; last_mount_flags = (int)flags;
    if (scenario == MOUNT_FAIL) { errno = EINVAL; return -1; }
    return 0;
}
int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list ap; int n = indexfd(fd); assert(n >= 0 && n < 8); va_start(ap, request);
    struct slot *s = &slots[n];
    if (request == LOOP_GET_STATUS64) {
        struct loop_info64 *arg = va_arg(ap, struct loop_info64 *); va_end(ap);
        ++get_calls;
        if (scenario == GET_EIO) { errno = EIO; return -1; }
        if (!s->busy) { errno = ENXIO; return -1; }
        memcpy(arg, &s->info, sizeof(s->info)); return 0;
    }
    if (request == LOOP_SET_FD) {
        int backing = va_arg(ap, int); va_end(ap);
        ++set_fd_calls; assert(backing == 100);
        if (scenario == RACE && n == 0) { errno = EBUSY; return -1; }
        s->busy = s->attached = 1;
        s->info.lo_number = n;
        s->info.lo_flags = 0x200003U;
        s->backing_readonly = (__wrap_fcntl(backing, F_GETFL) & O_ACCMODE) == O_RDONLY;
        s->info.lo_device = (0x22 & 0xff) | ((unsigned long long)0x11 << 8);
        s->info.lo_inode = 0x1234;
        return 0;
    }
    if (request == LOOP_SET_STATUS64) {
        struct loop_info64 *requested = va_arg(ap, struct loop_info64 *); va_end(ap);
        ++set_status_calls;
        if (scenario == STATUS_FAIL) { errno = EINVAL; return -1; }
        assert(requested->lo_flags == 0 && !requested->lo_offset &&
               !requested->lo_sizelimit && !requested->lo_encrypt_type);
        assert(!memcmp(requested->lo_file_name, OWNER, sizeof(OWNER)));
        /* The pinned old loop driver ignores status flags for readonly: it
         * derives readonly from SET_FD's O_RDONLY backing descriptor. */
        memcpy(s->info.lo_file_name, requested->lo_file_name, sizeof(s->info.lo_file_name));
        s->info.lo_offset = requested->lo_offset;
        s->info.lo_sizelimit = requested->lo_sizelimit;
        s->info.lo_encrypt_type = requested->lo_encrypt_type;
        s->info.lo_flags = s->backing_readonly ? 0x200007U : 0;
        if (scenario == BAD_INFO) { s->info.lo_inode++; }
        return 0;
    }
    if (request == LOOP_CLR_FD) {
        assert(va_arg(ap, int) == 0); va_end(ap);
        ++clear_calls;
        if (force_clear_fail || scenario == CLEAR_FAIL) { errno = EBUSY; return -1; }
        if (scenario == POSTCLEAR_BOUND) return 0;
        s->busy = s->attached = 0; memset(&s->info, 0, sizeof(s->info)); return 0;
    }
    va_end(ap);
    assert(!"unexpected ioctl"); return -1;
}

static void expect_mount(int expected, int err)
{
    errno = 0; int actual = gkd_app_loop_mount(100, "/mnt/a", OWNER);
    assert(actual == expected);
    if (err) assert(errno == err);
}
static void run_case(const char *name)
{
    init();
    fprintf(stderr, "loop %s...\n", name);
    if (!strcmp(name, "success-cleanup")) {
        expect_mount(0, 0); assert(mount_calls == 1 && clear_calls == 0);
        assert(last_mount_flags == (MS_RDONLY | MS_NOSUID | MS_NODEV));
        assert(gkd_app_loop_cleanup(OWNER) == 0 && clear_calls == 1 && !slots[0].busy);
    } else if (!strcmp(name, "all-busy")) {
        for (int i = 0; i < 8; ++i) { slots[i].busy = 1; info_for(&slots[i].info, OTHER); }
        expect_mount(-1, EBUSY); assert(set_fd_calls == 0);
    } else if (!strcmp(name, "absent")) { for (int i=0;i<8;i++) slots[i].exists=0; expect_mount(-1, EBUSY); }
    else if (!strcmp(name, "race-ebusy")) { scenario=RACE; expect_mount(0, 0); assert(set_fd_calls==2 && last_mount_index==1 && clear_calls==0); }
    else if (!strcmp(name, "get-eio")) { scenario=GET_EIO; expect_mount(-1, EIO); assert(set_fd_calls==0); }
    else if (!strcmp(name, "bad-source-fd")) { scenario=BAD_IMAGE; expect_mount(-1, EINVAL); assert(set_fd_calls==0); }
    else if (!strcmp(name, "nonregular-source")) { scenario=NONREG_IMAGE; expect_mount(-1, EINVAL); assert(set_fd_calls==0); }
    else if (!strcmp(name, "loop-fstat-eio")) { scenario=FSTAT_EIO; expect_mount(-1, EIO); assert(get_calls==0 && clear_calls==0 && close_calls==1); }
    else if (!strcmp(name, "loop-nonblock")) { scenario=NONBLOCK; expect_mount(-1, ENOTBLK); assert(get_calls==0 && clear_calls==0 && close_calls==1); }
    else if (!strcmp(name, "loop-wrong-major")) { scenario=WRONG_MAJOR; expect_mount(-1, ENOTBLK); assert(get_calls==0 && clear_calls==0 && close_calls==1); }
    else if (!strcmp(name, "loop-symlink")) { scenario=SYMLINK; expect_mount(-1, ELOOP); assert(get_calls==0 && clear_calls==0 && close_calls==0); }
    else if (!strcmp(name, "set-status-failure")) { scenario=STATUS_FAIL; expect_mount(-1, EINVAL); assert(clear_calls==1); }
    else if (!strcmp(name, "wrong-info")) { scenario=BAD_INFO; expect_mount(-1, EBADMSG); assert(clear_calls==1); }
    else if (!strcmp(name, "mount-failure")) { scenario=MOUNT_FAIL; expect_mount(-1, EINVAL); assert(clear_calls==1); }
    else if (!strcmp(name, "clear-ebusy")) { scenario=MOUNT_FAIL; force_clear_fail=1; expect_mount(-1, EBUSY); assert(clear_calls==1); }
    else if (!strcmp(name, "postclear-bound")) { scenario=POSTCLEAR_BOUND; expect_mount(0, 0); assert(clear_calls==0); errno=0; assert(gkd_app_loop_cleanup(OWNER)==-1 && errno==EBUSY && clear_calls==1); }
    else if (!strcmp(name, "foreign-preserved")) { slots[0].busy=1; info_for(&slots[0].info, OTHER); assert(gkd_app_loop_cleanup(OWNER)==0 && clear_calls==0 && slots[0].busy); }
    else if (!strcmp(name, "old-tag-preserved")) { slots[0].busy=1; info_for(&slots[0].info, "gkd-app-old"); assert(gkd_app_loop_cleanup(OWNER)==0 && clear_calls==0); }
    else if (!strcmp(name, "cleanup-invalid-owned")) { scenario=CLEANUP_BAD; slots[0].busy=1; info_for(&slots[0].info, OWNER); slots[0].info.lo_flags=0; errno=0; assert(gkd_app_loop_cleanup(OWNER)==-1 && errno==EBADMSG && clear_calls==0); }
    else assert(!"unknown case");
    printf("loop %s PASS\n", name);
}
static void lease_cases(void)
{
    struct gkd_app_loop lease=GKD_APP_LOOP_INIT;
    init(); slots[0].busy=1;info_for(&slots[0].info,OWNER);
    assert(!gkd_app_loop_mount_owned(100,"/mnt/a",OWNER,&lease));
    assert(lease.fd==11 && !lease.unlabelled && slots[0].busy && slots[1].busy);
    assert(gkd_app_loop_owned_count(OWNER)==2);
    assert(!gkd_app_loop_release(&lease)&&lease.fd==-1&&slots[0].busy&&!slots[1].busy);
    assert(gkd_app_loop_owned_count(OWNER)==1);
    init();lease=(struct gkd_app_loop)GKD_APP_LOOP_INIT;
    scenario=STATUS_FAIL;force_clear_fail=1;
    assert(gkd_app_loop_mount_owned(100,"/mnt/a",OWNER,&lease)==-1&&lease.fd==10&&lease.unlabelled);
    assert(slots[0].busy&&close_calls==0);
    force_clear_fail=0;
    assert(!gkd_app_loop_release(&lease)&&lease.fd==-1&&!slots[0].busy);
    init();lease=(struct gkd_app_loop)GKD_APP_LOOP_INIT;
    assert(!gkd_app_loop_mount_owned(100,"/mnt/a",OWNER,&lease));
    slots[0].info.lo_inode++;
    assert(gkd_app_loop_release(&lease)==-1&&errno==EBADMSG&&lease.fd==10&&clear_calls==0);
    puts("GKD_LOOP_LEASE=PASS exact-handle frontend-preserved unlabelled-failure-retained identity-refusal");
}
int main(void)
{
    lease_cases();
    const char *cases[] = {"success-cleanup", "all-busy", "absent", "race-ebusy", "get-eio", "bad-source-fd", "nonregular-source", "loop-fstat-eio", "loop-nonblock", "loop-wrong-major", "loop-symlink", "set-status-failure", "wrong-info", "mount-failure", "clear-ebusy", "postclear-bound", "foreign-preserved", "old-tag-preserved", "cleanup-invalid-owned"};
    for (size_t i=0; i<sizeof(cases)/sizeof(cases[0]); ++i) run_case(cases[i]);
    puts("GKD_APP_LOOP=PASS cases=19 native-fault-injection=1"); return 0;
}
