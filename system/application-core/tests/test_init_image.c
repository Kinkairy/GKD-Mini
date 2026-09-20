/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-init.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static int descriptors(void)
{
    DIR *dir = opendir("/proc/self/fd");
    struct dirent *entry;
    int count = 0;
    assert(dir);
    while ((entry = readdir(dir))) if (entry->d_name[0] != '.') ++count;
    assert(!closedir(dir));
    return count;
}

static void stop_child(pid_t child)
{
    int status;
    assert(kill(child, SIGKILL) == 0);
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
}

static void run_case(const char *name)
{
    int count = descriptors(), pipefd[2], extra = -1, stopped = 0, flags;
    int pidfd, image, rc, saved;
    const char *path = !strcmp(name, "deleted-image") ? "/test/init-image-unlinked" :
                                                       "/test/init-image-fixture";
    assert(pipe2(pipefd, O_CLOEXEC) == 0);
    image = open(path, O_PATH | O_CLOEXEC);
    assert(image >= 3);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        char value;
        close(pipefd[1]);
        if (read(pipefd[0], &value, 1) != 1) _exit(120);
        execl(path, path, (char *)NULL);
        _exit(121);
    }
    close(pipefd[0]);
    pidfd = (int)syscall(SYS_pidfd_open, child, 0);
    assert(pidfd >= 3);
    assert(gkd_app_init_image(pidfd, child, image) == 0);

    if (!strcmp(name, "exec-image") || !strcmp(name, "deleted-image") ||
        !strcmp(name, "same-bytes-other-inode") || !strcmp(name, "exit-after-match")) {
        assert(write(pipefd[1], "x", 1) == 1);
        for (int i = 0; i < 200; ++i) {
            rc = gkd_app_init_image(pidfd, child, image);
            assert(rc >= 0);
            if (rc == 1) break;
            usleep(10000);
        }
        assert(rc == 1);
        if (!strcmp(name, "deleted-image")) {
            assert(!unlink(path));
            assert(gkd_app_init_image(pidfd, child, image) == 1);
        }
        if (!strcmp(name, "same-bytes-other-inode")) {
            extra = open("/test/init-image-copy", O_PATH | O_CLOEXEC);
            assert(extra >= 3);
            assert(gkd_app_init_image(pidfd, child, extra) == 0);
        }
        if (!strcmp(name, "exit-after-match")) {
            stop_child(child); stopped = 1;
            assert(gkd_app_init_image(pidfd, child, image) == -1);
            assert(errno == ESRCH);
        }
    } else if (!strcmp(name, "preexec")) {
        for (int i = 0; i < 20; ++i) assert(gkd_app_init_image(pidfd, child, image) == 0);
    } else if (!strcmp(name, "dead-process")) {
        stop_child(child); stopped = 1;
        assert(gkd_app_init_image(pidfd, child, image) == -1 && errno == ESRCH);
    } else {
        int tested_pidfd = pidfd, tested_image = image;
        pid_t tested_pid = child;
        int expected_errno = EINVAL;
        if (!strcmp(name, "wrong-pid")) tested_pid = getpid();
        else if (!strcmp(name, "wrong-pidfd")) {
            extra = (int)syscall(SYS_pidfd_open, getpid(), 0);
            assert(extra >= 3); tested_pidfd = extra;
        } else if (!strcmp(name, "non-pidfd")) {
            tested_pidfd = pipefd[1]; expected_errno = 0;
        } else if (!strcmp(name, "closed-image")) {
            assert(!close(image)); tested_image = image; image = -1; expected_errno = EBADF;
        } else if (!strcmp(name, "non-executable")) {
            extra = open("/test/plain", O_PATH | O_CLOEXEC); assert(extra >= 3);
            tested_image = extra;
        } else if (!strcmp(name, "directory")) {
            extra = open("/test", O_PATH | O_CLOEXEC); assert(extra >= 3); tested_image = extra;
        } else if (!strcmp(name, "symlink-handle")) {
            extra = open("/test/init-image-link", O_PATH | O_NOFOLLOW | O_CLOEXEC);
            assert(extra >= 3); tested_image = extra;
        } else if (!strcmp(name, "image-no-cloexec")) {
            flags = fcntl(image, F_GETFD); assert(flags >= 0);
            assert(!fcntl(image, F_SETFD, flags & ~FD_CLOEXEC));
        } else if (!strcmp(name, "pidfd-no-cloexec")) {
            flags = fcntl(pidfd, F_GETFD); assert(flags >= 0);
            assert(!fcntl(pidfd, F_SETFD, flags & ~FD_CLOEXEC));
        } else if (!strcmp(name, "aliased")) tested_image = pidfd;
        else if (!strcmp(name, "invalid-pid")) tested_pid = 0;
        else if (!strcmp(name, "low-fd")) tested_image = 0;
        else assert(!"unknown case");
        errno = 0;
        rc = gkd_app_init_image(tested_pidfd, tested_pid, tested_image); saved = errno;
        assert(rc == -1 && (!expected_errno || saved == expected_errno));
        /* Error paths borrow rather than close the caller's surviving handles. */
        assert(fcntl(pidfd, F_GETFD) >= 0);
        if (image >= 0) assert(fcntl(image, F_GETFD) >= 0);
        if (extra >= 0) assert(fcntl(extra, F_GETFD) >= 0);
    }
    if (!stopped) stop_child(child);
    assert(!close(pidfd));
    if (image >= 0) assert(!close(image));
    if (extra >= 0) assert(!close(extra));
    assert(!close(pipefd[1]));
    assert(descriptors() == count);
    printf("init-image %s PASS\n", name);
}

int main(void)
{
    const char *cases[] = {"preexec", "exec-image", "deleted-image", "same-bytes-other-inode",
        "exit-after-match", "dead-process", "wrong-pid", "wrong-pidfd", "non-pidfd",
        "closed-image", "non-executable", "directory", "symlink-handle", "image-no-cloexec",
        "pidfd-no-cloexec", "aliased", "invalid-pid", "low-fd"};
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) run_case(cases[i]);
    puts("GKD_APP_INIT_IMAGE=PASS cases=18 descriptor-leaks=0");
    return 0;
}
