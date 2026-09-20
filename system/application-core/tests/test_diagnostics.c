/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-diagnostics.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int partial_fd = -1;
ssize_t __real_write(int, const void *, size_t);
ssize_t __wrap_write(int fd, const void *p, size_t n)
{
    if (fd == partial_fd && n > 2) n = 2;
    return __real_write(fd, p, n);
}
static char directory[] = "/dev/shm/gkd-diag-test-XXXXXX";
static char path[256];
static struct gkd_app_diagnostics fresh(void)
{
    struct gkd_app_diagnostics d = GKD_APP_DIAGNOSTICS_INIT;
    assert(!gkd_app_diagnostics_open(&d, directory));
    struct stat st;
    assert(!stat(path, &st) && (st.st_mode & 0777) == 0600);
    assert((fcntl(d.writer, F_GETFL) & (O_ACCMODE | O_NONBLOCK)) == (O_WRONLY | O_NONBLOCK));
    assert(fcntl(d.reader, F_GETFD) & FD_CLOEXEC);
    assert(fcntl(d.writer, F_GETFD) & FD_CLOEXEC);
    assert(fcntl(d.file, F_GETFD) & FD_CLOEXEC);
    return d;
}
static void clear(struct gkd_app_diagnostics *d)
{
    gkd_app_diagnostics_close(d);
    assert(!unlink(path));
}
int main(void)
{
    assert(mkdtemp(directory));
    snprintf(path, sizeof(path), "%s/child-output.log", directory);
    struct gkd_app_diagnostics d = GKD_APP_DIAGNOSTICS_INIT;
    char disk_directory[] = "/test/gkd-diag-disk-XXXXXX";
    assert(mkdtemp(disk_directory));
    assert(gkd_app_diagnostics_open(&d, disk_directory) == -1 && errno == EPERM);
    assert(!rmdir(disk_directory));
    assert(!chmod(directory, 0755));
    assert(gkd_app_diagnostics_open(&d, directory) == -1 && errno == EPERM);
    assert(!chmod(directory, 0700));
    assert(!symlink("/missing-diagnostic-target", path));
    assert(gkd_app_diagnostics_open(&d, directory) == -1);
    assert(!unlink(path));
    mode_t previous_mask = umask(0777);
    d = fresh();
    umask(previous_mask);
    struct gkd_app_diagnostics other = GKD_APP_DIAGNOSTICS_INIT;
    assert(gkd_app_diagnostics_open(&other, directory) == -1 && errno == EEXIST);
    assert(write(d.writer, "stdout\nstderr\n", 14) == 14);
    close(d.writer); d.writer = -1;
    errno = EDOM;
    gkd_app_diagnostics_finish(&d);
    assert(errno == EDOM && d.eof && d.retained == 14 && !d.discarded);
    int fd = open(path, O_RDONLY); char small[15] = {0};
    assert(fd >= 0 && read(fd, small, 14) == 14 && !strcmp(small, "stdout\nstderr\n")); close(fd);
    clear(&d);

    d = fresh();
    char block[4096]; memset(block, 'x', sizeof(block));
    for (unsigned i = 0; i < 80; ++i) {
        assert(write(d.writer, block, sizeof(block)) == sizeof(block));
        gkd_app_diagnostics_pump(&d);
    }
    close(d.writer); d.writer = -1; gkd_app_diagnostics_finish(&d);
    struct stat st; assert(!stat(path, &st));
    assert(st.st_size == 65536 && d.retained == 65536 && d.discarded == 262144 && d.eof);
    clear(&d);

    d = fresh(); size_t queued = 0;
    for (;;) {
        ssize_t n = write(d.writer, block, sizeof(block));
        if (n < 0) { assert(errno == EAGAIN); break; }
        queued += n; assert(queued <= 1048576);
    }
    /* An open writer with no more bytes must not make finish wait for EOF. */
    gkd_app_diagnostics_finish(&d); assert(!d.eof);
    assert(d.retained + d.discarded == queued);
    close(d.writer); d.writer = -1; gkd_app_diagnostics_finish(&d); assert(d.eof);
    clear(&d);

    d = fresh(); partial_fd = d.file;
    assert(write(d.writer, "abcdef", 6) == 6); gkd_app_diagnostics_pump(&d);
    assert(d.capture_errno == EIO && d.retained == 2 && d.discarded == 4);
    partial_fd = -1;
    assert(write(d.writer, "more", 4) == 4); gkd_app_diagnostics_pump(&d);
    assert(d.retained == 2 && d.discarded == 8);
    clear(&d);

    d = fresh(); close(d.file); d.file = -1;
    assert(write(d.writer, "error", 5) == 5); gkd_app_diagnostics_pump(&d);
    assert(d.capture_errno == EBADF && d.discarded == 5);
    clear(&d);
    d = fresh(); close(d.reader);
    gkd_app_diagnostics_pump(&d);
    assert(d.read_errno == EBADF);
    d.reader = -1;
    clear(&d);
    assert(!rmdir(directory));
    puts("GKD_APP_DIAGNOSTICS=PASS exclusive permissions routing eof cap flood eagain partial-write capture-error");
    return 0;
}
