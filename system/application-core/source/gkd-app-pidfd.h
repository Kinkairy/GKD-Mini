/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_PIDFD_PRIVATE_H
#define GKD_APP_PIDFD_PRIVATE_H
/* Caller includes this with _GNU_SOURCE enabled and trusted matching procfs. */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

/* 1 alive, 0 exited, -1 observation error. No numeric-PID probing after init. */
static inline int gkd_app_pidfd_alive(int fd)
{
    struct pollfd item = {fd, POLLIN, 0};
    int result = poll(&item, 1, 0);
    if (result < 0) return -1;
    if (item.revents & (POLLERR | POLLNVAL)) { errno = EBADF; return -1; }
    return !(item.revents & (POLLIN | POLLHUP));
}

static inline int gkd_app_pidfd_pid(int fd, pid_t *mapped)
{
    char path[64], contents[1024], extra, *line, *end;
    int info, saved, flags;
    ssize_t length, tail;
    unsigned int pid = 0;
    flags = fcntl(fd, F_GETFD);
    if (flags < 0) return -1;
    if (!(flags & FD_CLOEXEC)) { errno = EINVAL; return -1; }
    /* Signal zero validates the kernel file type/permission; delivers nothing. */
    if (syscall(SYS_pidfd_send_signal, fd, 0, NULL, 0U) < 0) return -1;
    (void)snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", fd);
    info = open(path, O_RDONLY | O_CLOEXEC);
    if (info < 0) return -1;
    length = read(info, contents, sizeof(contents) - 1);
    saved = errno;
    tail = length >= 0 ? read(info, &extra, 1) : -1;
    if (tail < 0 && length >= 0) saved = errno;
    (void)close(info);
    if (length < 0 || tail < 0) { errno = saved; return -1; }
    if (tail != 0) { errno = EOVERFLOW; return -1; }
    contents[length] = 0;
    line = !strncmp(contents, "Pid:\t", 5) ? contents : strstr(contents, "\nPid:\t");
    if (!line) { errno = EPROTO; return -1; }
    if (*line == '\n') ++line;
    /* The kernel field is bounded unsigned decimal, not general libc numeric
     * syntax. Avoid newer native libc's C23 strtol ABI in the host probe. */
    for (end = line + 5; *end >= '0' && *end <= '9'; ++end) {
        unsigned int digit = (unsigned int)(*end - '0');
        if (pid > ((unsigned int)INT_MAX - digit) / 10U) { errno = EINVAL; return -1; }
        pid = pid * 10U + digit;
    }
    if (end == line + 5 || *end != '\n' || !pid ||
        strstr(end, "\nPid:\t")) { errno = EINVAL; return -1; }
    int live = gkd_app_pidfd_alive(fd);
    if (live < 0) return -1;
    if (!live) { errno = ESRCH; return -1; }
    *mapped = (pid_t)pid;
    return 0;
}

static inline int gkd_app_pidfd_match(int fd, pid_t expected)
{
    pid_t mapped;
    if (gkd_app_pidfd_pid(fd, &mapped) < 0) return -1;
    if (mapped != expected) { errno = EINVAL; return -1; }
    return 0;
}

#endif
