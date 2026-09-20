/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_OUTPUT_H
#define GKD_APP_OUTPUT_H
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>

static inline int gkd_app_output_validate(int fd)
{
    struct stat st;
    int status, flags;
    if (fd < 3) { errno = EINVAL; return -1; }
    status = fcntl(fd, F_GETFD);
    flags = fcntl(fd, F_GETFL);
    if (status < 0 || flags < 0 || fstat(fd, &st)) return -1;
    if (!(status & FD_CLOEXEC) || !(flags & O_NONBLOCK) ||
        (flags & O_ACCMODE) != O_WRONLY || !S_ISFIFO(st.st_mode)) {
        errno = EINVAL; return -1;
    }
    return 0;
}

/* Best-effort diagnostic text, never an authenticated lifecycle receipt.
 * A zero monotonic value means unavailable/not measured at this call site.
 * Pipe-full loss cannot be counted by the controller. */
static inline void gkd_app_output_stage(int fd, const char *phase, int error,
                                        unsigned long long monotonic_ms, int release_state)
{
    char line[192];
    int saved = errno;
    int length = snprintf(line, sizeof(line), "GKD_APP phase=%s errno=%d mono_ms=%llu release_state=%d\n",
                          phase, error, monotonic_ms, release_state);
    if (length > 0 && (size_t)length < sizeof(line)) {
        ssize_t written = write(fd, line, (size_t)length);
        (void)written;
    }
    errno = saved;
}
#endif
