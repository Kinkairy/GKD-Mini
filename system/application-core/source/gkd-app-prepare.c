/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-prepare.h"
#include "gkd-app-output.h"
#include "gkd-app-release.h"
#include "gkd-app-transfer.h"
#include "gkd-app-error-receiver.h"
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
/* Pinned Linux 6.1 include/uapi/linux/nsfs.h. Older target libc headers omit it. */
#define GKD_NS_GET_NSTYPE _IO(0xb7, 0x3)

static int milliseconds(uint64_t *value)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return -1;
    if (t.tv_sec < 0 || (uint64_t)t.tv_sec > UINT64_MAX / 1000U) {
        errno = EOVERFLOW; return -1;
    }
    *value = (uint64_t)t.tv_sec * 1000U + (uint64_t)t.tv_nsec / 1000000U;
    return 0;
}
static int time_left(uint64_t deadline)
{
    uint64_t now;
    if (milliseconds(&now)) return -1;
    if (now >= deadline) { errno = ETIMEDOUT; return -1; }
    return (int)(deadline - now); /* timeout validated <= INT_MAX. */
}
static int wait_channel(int fd, short events, uint64_t deadline)
{
    struct pollfd p = {fd, events, 0};
    int left = time_left(deadline);
    if (left < 0) return -1;
    int result = poll(&p, 1, left);
    if (result < 0 && errno != EINTR) return -1;
    if (!result) { errno = ETIMEDOUT; return -1; }
    if (p.revents & (POLLNVAL | POLLERR)) { errno = EIO; return -1; }
    return 0; /* Shared parser/send handles EOF/HUP and the next syscall error. */
}
static int namespace_separate(int fd, int type, const char *current)
{
    struct stat original, worker;
    if (ioctl(fd, GKD_NS_GET_NSTYPE) != type) { errno = EINVAL; return -1; }
    if (fstat(fd, &original) || stat(current, &worker)) return -1;
    if (original.st_dev == worker.st_dev && original.st_ino == worker.st_ino) {
        errno = EPERM; return -1;
    }
    return 0;
}
static int empty_inittab(void)
{
    struct stat s;
    struct statfs filesystem;
    int directory = open("/etc", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (directory < 0) return -1;
    int fd = openat(directory, "inittab", O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    int saved = errno;
    close(directory);
    if (fd < 0) { errno = saved; return -1; }
    int result = fstat(fd, &s);
    /* Pinned uClibc fstatvfs guesses flags from the first same-device mount.
     * A shared RW guard and this RO bind have the same st_dev. Linux 6.1
     * fstatfs returns flags for this exact fd mount; never infer from order. */
    if (!result) result = fstatfs(fd, &filesystem);
    saved = errno;
    close(fd);
    if (result < 0) { errno = saved; return -1; }
    if (!S_ISREG(s.st_mode) || s.st_size || s.st_uid != geteuid() ||
        (s.st_mode & 0022) || !(filesystem.f_flags & ST_RDONLY)) {
        errno = EPERM; return -1;
    }
    return 0;
}
static int preflight(const struct gkd_app_prepare_request *r, const int fds[9])
{
    struct stat image, terminal;
    unsigned char signature[4];
    unsigned nonzero = 0;
    if (getpid() != 1 || geteuid() != 0) { errno = EPERM; return -1; }
    if (!r->app.launch_token || !r->timeout_ms || r->timeout_ms > INT_MAX ||
        r->controller_mapped_pid < 0) { errno = EINVAL; return -1; }
    for (unsigned i = 0; i < 16; ++i) nonzero |= r->app.launch_token[i];
    if (!nonzero) { errno = EINVAL; return -1; }
    for (unsigned i = 0; i < 9; ++i) {
        if (fds[i] < 3) { errno = EINVAL; return -1; }
        int flags = fcntl(fds[i], F_GETFD);
        if (flags < 0) return -1;
        if (!(flags & FD_CLOEXEC)) { errno = EINVAL; return -1; }
        for (unsigned j = 0; j < i; ++j)
            if (fds[j] == fds[i]) { errno = EINVAL; return -1; }
    }
    if (namespace_separate(r->controller_pidns_fd, CLONE_NEWPID, "/proc/self/ns/pid") ||
        namespace_separate(r->controller_mntns_fd, CLONE_NEWNS, "/proc/self/ns/mnt") ||
        empty_inittab()) return -1;
    if (fstat(r->init_executable_fd, &image) || fstat(r->app.terminal_fd, &terminal)) return -1;
    if (!S_ISREG(image.st_mode) || !(image.st_mode & 0111) ||
        (image.st_mode & (S_ISUID | S_ISGID)) || !S_ISCHR(terminal.st_mode)) {
        errno = EINVAL; return -1;
    }
    if (pread(r->init_executable_fd, signature, 4, 0) != 4 ||
        memcmp(signature, "\177ELF", 4)) { errno = ENOEXEC; return -1; }
    if (gkd_app_output_validate(r->app.output_fd) || gkd_app_seqpacket_endpoint(r->error_fd, 0) ||
        gkd_app_seqpacket_endpoint(r->release_fd, 1)) return -1;
    return 0; /* App request validation belongs to the existing exec leaf. */
}
static int reset_signals(void)
{
    struct sigaction action;
    sigset_t empty;
    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;
    if (sigemptyset(&action.sa_mask) || sigemptyset(&empty)) return -1;
    for (int s = 1; s < NSIG; ++s) {
        if (s == SIGKILL || s == SIGSTOP) continue;
        if (sigaction(s, &action, NULL) < 0 && errno != EINVAL) return -1;
        /* glibc reserves internal real-time signals; EINVAL cannot be set by
         * ordinary sigaction callers either. This worker must be single-threaded. */
    }
    return sigprocmask(SIG_SETMASK, &empty, NULL);
}
static int contain(const int fds[9])
{
    int staged[7] = {-1,-1,-1,-1,-1,-1,-1}, saved;
    for (unsigned i = 0; i < 7; ++i) {
        staged[i] = fcntl(fds[i], F_DUPFD_CLOEXEC, 16);
        if (staged[i] < 0) goto fail;
    }
    if (dup3(staged[2], 0, 0) < 0 || dup3(staged[3], 1, 0) < 0 ||
        dup3(staged[3], 2, 0) < 0) goto fail;
    for (int i = 0; i < 7; ++i)
        if (dup3(staged[i], 3 + i, O_CLOEXEC) < 0) goto fail;
    /* 3 init, 4 app, 5 terminal, 6 output, 7 ready, 8 error, 9 release. */
    if (syscall(SYS_close_range, 10U, ~0U, 0U) < 0) goto fail;
    return 0;
fail:
    saved = errno;
    for (unsigned i = 0; i < 7; ++i) if (staged[i] >= 0) close(staged[i]);
    errno = saved;
    return -1;
}
static void child(const struct gkd_app_prepare_request *r, uint64_t deadline)
{
    struct gkd_app_release_rx rx = GKD_APP_RELEASE_RX_INIT;
    struct gkd_app_exec_request app = r->app;
    uint64_t now;
    close(3); /* Never retain init's executable handle in the waiting app. */
    if (milliseconds(&now)) {
        int saved = errno; gkd_app_output_stage(6, "CLOCK", saved, 0, -1); _exit(125);
    }
    if (now >= deadline) {
        gkd_app_output_stage(6, "RELEASE_INIT", ETIMEDOUT, now, -1); _exit(125);
    }
    if (gkd_app_release_rx_init(&rx, 9, r->controller_mapped_pid,
            r->controller_mapped_uid, r->app.launch_token, now, deadline - now)) {
        int saved = errno; gkd_app_output_stage(6, "RELEASE_INIT", saved, now, -1); _exit(125);
    }
    for (;;) {
        if (milliseconds(&now)) { int saved = errno; gkd_app_output_stage(6, "CLOCK", saved, 0, rx.state); _exit(125); }
        enum gkd_app_release_state state = gkd_app_release_poll(&rx, now);
        if (state == GKD_RELEASE_GRANTED) break;
        if (state != GKD_RELEASE_WAITING) {
            gkd_app_output_stage(6, "RELEASE_RECEIVE", 0, now, state); _exit(125);
        }
        if (wait_channel(9, POLLIN, deadline)) {
            int saved = errno; gkd_app_output_stage(6, "RELEASE_WAIT", saved, now, state); _exit(125);
        }
    }
    gkd_app_output_stage(6, "RELEASE_GRANTED", 0, now, GKD_RELEASE_GRANTED);
    close(9);
    app.preparer_pid = 1; app.executable_fd = 4; app.terminal_fd = 5; app.output_fd = 6; app.ready_fd = 7;
    gkd_app_output_stage(6, "EXEC_ATTEMPT", 0, now, GKD_RELEASE_GRANTED);
    (void)gkd_app_exec_replace_report(&app, 8);
    _exit(111);
}
int gkd_app_prepare_replace(const struct gkd_app_prepare_request *r)
{
    uint64_t now, deadline;
    if (!r) { errno = EINVAL; return -1; }
    int fds[9] = {r->init_executable_fd, r->app.executable_fd, r->app.terminal_fd,
        r->app.output_fd, r->app.ready_fd, r->error_fd, r->release_fd, r->controller_pidns_fd, r->controller_mntns_fd};
    if (preflight(r, fds) || milliseconds(&now)) return -1;
    if (UINT64_MAX - now < r->timeout_ms) { errno = EOVERFLOW; return -1; }
    deadline = now + r->timeout_ms;
    if (contain(fds) || reset_signals()) return -1;
    pid_t app = fork();
    if (app < 0) return -1;
    if (!app) child(r, deadline);
    int pidfd = (int)syscall(SYS_pidfd_open, app, 0U);
    if (pidfd < 0) return -1;
    struct gkd_app_transfer_tx tx = GKD_APP_TRANSFER_TX_INIT;
    for (;;) {
        if (time_left(deadline) < 0) {
            int saved = errno; close(pidfd); errno = saved; return -1;
        }
        if (!gkd_app_transfer_send(&tx, 8, pidfd, app, r->app.launch_token)) break;
        if ((errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) ||
            wait_channel(8, POLLOUT, deadline)) {
            int saved = errno; close(pidfd); errno = saved; return -1;
        }
    }
    close(pidfd);
    close(4); close(5); close(6); close(7); close(8); close(9);
    char *argv[] = {"init", NULL};
    char *environment[] = {"HOME=/", "PATH=/bin", "TERM=linux", NULL};
    (void)syscall(SYS_execveat, 3, "", argv, environment, AT_EMPTY_PATH);
    return -1; /* Caller must immediately exit this namespace worker. */
}
