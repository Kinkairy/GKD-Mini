/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-diagnostics.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/magic.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

static void discard(struct gkd_app_diagnostics *d, size_t bytes)
{
    if (UINT64_MAX - d->discarded < bytes) d->discarded = UINT64_MAX;
    else d->discarded += bytes;
}

int gkd_app_diagnostics_open(struct gkd_app_diagnostics *d, const char *directory)
{
    struct stat st;
    struct statfs fs;
    int dir = -1, pair[2] = {-1, -1}, file = -1, saved;
    if (!d || !directory || d->reader >= 0 || d->writer >= 0 || d->file >= 0) {
        errno = EINVAL; return -1;
    }
    dir = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (dir < 0) return -1;
    if (fstat(dir, &st) || fstatfs(dir, &fs)) goto fail;
    if (st.st_uid != geteuid() || (st.st_mode & 0777) != 0700 ||
        (fs.f_type != (long)TMPFS_MAGIC && fs.f_type != (long)RAMFS_MAGIC)) {
        errno = EPERM; goto fail;
    }
    if (pipe2(pair, O_CLOEXEC | O_NONBLOCK)) goto fail;
    /* Final drain is bounded by this maximum, not an assumed default size. */
    int capacity = fcntl(pair[0], F_GETPIPE_SZ);
    if (capacity <= 0 || capacity > 1048576) { errno = EOVERFLOW; goto fail; }
    file = openat(dir, "child-output.log",
                  O_WRONLY | O_APPEND | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (file < 0) goto fail;
    if (fchmod(file, 0600)) {
        saved = errno;
        close(file);
        (void)unlinkat(dir, "child-output.log", 0);
        errno = saved;
        goto fail;
    }
    close(dir);
    *d = (struct gkd_app_diagnostics)GKD_APP_DIAGNOSTICS_INIT;
    d->reader = pair[0]; d->writer = pair[1]; d->file = file;
    return 0;
fail:
    saved = errno;
    if (dir >= 0) close(dir);
    if (pair[0] >= 0) close(pair[0]);
    if (pair[1] >= 0) close(pair[1]);
    errno = saved;
    return -1;
}

void gkd_app_diagnostics_pump(struct gkd_app_diagnostics *d)
{
    int saved = errno;
    if (!d || d->reader < 0 || d->eof || d->read_errno) return;
    for (unsigned i = 0; i < 4; ++i) {
        unsigned char buffer[4096];
        ssize_t n = read(d->reader, buffer, sizeof(buffer));
        if (!n) { d->eof = 1; break; }
        if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                d->read_errno = errno;
            break;
        }
        size_t keep = 0;
        if (!d->capture_errno && d->retained < GKD_APP_DIAGNOSTICS_LIMIT) {
            keep = GKD_APP_DIAGNOSTICS_LIMIT - (size_t)d->retained;
            if (keep > (size_t)n) keep = (size_t)n;
            ssize_t written = write(d->file, buffer, keep);
            if (written < 0) { d->capture_errno = errno; keep = 0; }
            else {
                if ((size_t)written != keep) d->capture_errno = EIO;
                keep = (size_t)written;
                d->retained += keep;
            }
        }
        discard(d, (size_t)n - keep);
    }
    errno = saved;
}

void gkd_app_diagnostics_finish(struct gkd_app_diagnostics *d)
{
    if (!d) return;
    for (unsigned i = 0; i < 65 && !d->eof && !d->read_errno; ++i)
        gkd_app_diagnostics_pump(d);
}

void gkd_app_diagnostics_close(struct gkd_app_diagnostics *d)
{
    if (!d) return;
    if (d->reader >= 0) close(d->reader);
    if (d->writer >= 0) close(d->writer);
    if (d->file >= 0) close(d->file);
    d->reader = d->writer = d->file = -1;
}
