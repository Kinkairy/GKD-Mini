/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int statefd;

static void flush_and_exit(int ignored)
{
    int fd;
    ssize_t written;
    (void)ignored;
    fd = openat(statefd, "flush", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) _exit(126);
    written = write(fd, "clean=1\n", 8U);
    if (written != 8 || fsync(fd) < 0 || close(fd) < 0 || fsync(statefd) < 0) _exit(126);
    _exit(0);
}

static int record(int readyfd)
{
    char path[64], line[160];
    int fd, n, parentfd;
    struct stat state, ownns, parentns;

    if (fstat(statefd, &state) || !S_ISDIR(state.st_mode)) return -1;
    for (int i = 5; i < 1024; ++i)
        if (fcntl(i, F_GETFD) >= 0 || errno != EBADF) return -1;
    if (fcntl(2048, F_GETFD) >= 0 || errno != EBADF) return -1;
    fd = open("/proc/self/ns/mnt", O_RDONLY | O_CLOEXEC);
    if (fd < 0 || fstat(fd, &ownns) < 0) return -1;
    (void)close(fd);
    n = snprintf(path, sizeof(path), "/proc/%ld/ns/mnt", (long)getppid());
    if (n <= 0 || (size_t)n >= sizeof(path)) return -1;
    parentfd = open(path, O_RDONLY | O_CLOEXEC);
    if (parentfd < 0 || fstat(parentfd, &parentns) < 0) return -1;
    (void)close(parentfd);
    fd = openat(statefd, "identity", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    n = snprintf(line, sizeof(line), "pid=%ld parent=%ld mount=%llu parent_mount=%llu\n", (long)getpid(),
        (long)getppid(), (unsigned long long)ownns.st_ino, (unsigned long long)parentns.st_ino);
    if (n <= 0 || (size_t)n >= sizeof(line) || write(fd, line, (size_t)n) != n) { (void)close(fd); return -1; }
    (void)close(fd);
    return readyfd >= 0 ? 0 : -1;
}

int main(int argc, char **argv)
{
    const char *mode;
    int readyfd;

    if (argc != 4 || strcmp(argv[1], "--managed")) return 125;
    statefd = atoi(argv[2]);
    readyfd = atoi(argv[3]);
    if (statefd != 3 || readyfd != 4 || record(readyfd)) return 125;
    mode = getenv("GKD_CONTROLS_MODE");
    if (!mode) mode = "ready";
    if (!strcmp(mode, "eof")) return 0;
    if (!strcmp(mode, "bad")) return write(readyfd, "X", 1U) == 1 ? 0 : 125;
    if (!strcmp(mode, "timeout")) { sleep(6); return 0; }
    if (!strcmp(mode, "hung")) (void)signal(SIGTERM, SIG_IGN);
    else if (signal(SIGTERM, flush_and_exit) == SIG_ERR) return 125;
    if (write(readyfd, "R", 1U) != 1) return 125;
    if (!strcmp(mode, "early")) return 0;
    for (;;) pause();
}
