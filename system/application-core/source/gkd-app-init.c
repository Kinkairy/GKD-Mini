/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-init.h"
#include "gkd-app-pidfd.h"
#include <sys/stat.h>

int gkd_app_init_image(int pidfd, pid_t pid, int executable_fd)
{
    struct stat expected, observed;
    char path[64];
    int flags, directory, image, result, saved;
    if (pid <= 0 || pidfd < 3 || executable_fd < 3 || pidfd == executable_fd) {
        errno = EINVAL; return -1;
    }
    flags = fcntl(executable_fd, F_GETFD);
    if (flags < 0 || fstat(executable_fd, &expected)) return -1;
    if (!(flags & FD_CLOEXEC) || !S_ISREG(expected.st_mode) || !(expected.st_mode & 0111)) {
        errno = EINVAL; return -1;
    }
    if (gkd_app_pidfd_match(pidfd, pid)) return -1;
    (void)snprintf(path, sizeof(path), "/proc/%ld", (long)pid);
    directory = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0) return -1;
    /* Open the kernel magic link, not its display string (which may say deleted).
     * The proc directory pins this lookup to the original proc inode. */
    image = openat(directory, "exe", O_PATH | O_CLOEXEC);
    saved = errno;
    (void)close(directory);
    if (image < 0) { errno = saved; return -1; }
    result = fstat(image, &observed);
    saved = errno;
    (void)close(image);
    if (result < 0) { errno = saved; return -1; }
    /* Never accept an exited/recycled process from a stale executable snapshot. */
    if (gkd_app_pidfd_match(pidfd, pid)) return -1;
    return expected.st_dev == observed.st_dev && expected.st_ino == observed.st_ino;
}
