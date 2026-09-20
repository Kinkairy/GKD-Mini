/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-namespace.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef GKD_APP_NAMESPACE_PROC_ROOT
#define GKD_APP_NAMESPACE_PROC_ROOT "/proc"
#endif

static int process_identity_path(const char *path,
                                 struct gkd_app_process_identity *identity)
{
    char line[2048], *cursor, *token, *save = NULL, extra;
    int fd, field = 3; ssize_t length, tail;
    memset(identity, 0, sizeof(*identity));
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    do length = read(fd, line, sizeof(line) - 1); while (length < 0 && errno == EINTR);
    tail = length >= 0 ? read(fd, &extra, 1) : -1;
    int saved = errno; close(fd);
    if (length <= 0 || tail < 0) { errno = length == 0 ? EPROTO : saved; return -1; }
    if (tail != 0) { errno = EOVERFLOW; return -1; }
    line[length] = 0; cursor = strrchr(line, ')');
    if (!cursor || cursor[1] != ' ') { errno = EPROTO; return -1; }
    cursor += 2;
    for (token = strtok_r(cursor, " ", &save); token;
         token = strtok_r(NULL, " ", &save), ++field) {
        char *end = NULL;
        if (field == 4 || field == 5 || field == 6) {
            errno = 0; long value = strtol(token, &end, 10);
            if (errno || end == token || (*end && *end != '\n') ||
                value < 0 || value > INT32_MAX) {
                errno = EPROTO; return -1;
            }
            if (field == 4) identity->ppid = (pid_t)value;
            else if (field == 5) identity->pgid = (pid_t)value;
            else identity->sid = (pid_t)value;
        } else if (field == 22) {
            errno = 0; identity->starttime = strtoull(token, &end, 10);
            if (errno || end == token || (*end && *end != '\n') ||
                !identity->starttime) {
                errno = EPROTO; return -1;
            }
            return 0;
        }
    }
    errno = EPROTO; return -1;
}

int gkd_app_process_identity_read(pid_t pid,
                                  struct gkd_app_process_identity *identity)
{
    char path[64];
    if (!identity || pid <= 0) {
        errno = EINVAL; return -1;
    }
    if (snprintf(path, sizeof(path), GKD_APP_NAMESPACE_PROC_ROOT "/%ld/stat",
                 (long)pid) >= (int)sizeof(path)) {
        errno = EOVERFLOW; return -1;
    }
    return process_identity_path(path, identity);
}

static int pidfd_matches(int fd, pid_t expected)
{
    char path[64], data[512], extra, *line, *end; ssize_t length, tail;
    if (syscall(SYS_pidfd_send_signal, fd, 0, NULL, 0U)) return -1;
    snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", fd);
    int info = open(path, O_RDONLY | O_CLOEXEC);
    if (info < 0) return -1;
    length = read(info, data, sizeof(data) - 1); int saved = errno;
    tail = length >= 0 ? read(info, &extra, 1) : -1;
    if (tail < 0 && length >= 0) saved = errno;
    close(info);
    if (length < 0 || tail < 0) { errno = saved; return -1; }
    if (tail || !length) { errno = EPROTO; return -1; }
    data[length] = 0;
    line = !strncmp(data, "Pid:\t", 5) ? data : strstr(data, "\nPid:\t");
    if (!line) { errno = EPROTO; return -1; }
    if (*line == '\n') ++line;
    errno = 0; long value = strtol(line + 5, &end, 10);
    if (errno || end == line + 5 || *end != '\n' || value != (long)expected) {
        errno = ESRCH; return -1;
    }
    struct pollfd item = {fd, POLLIN | POLLHUP, 0};
    if (poll(&item, 1, 0) < 0) return -1;
    if (item.revents) { errno = ESRCH; return -1; }
    return 0;
}

static int pidfd_alive(int fd)
{
    struct pollfd item = {fd, POLLIN | POLLHUP, 0};
    if (fd < 0 || syscall(SYS_pidfd_send_signal, fd, 0, NULL, 0U)) return -1;
    if (poll(&item, 1, 0) < 0) return -1;
    if (item.revents) { errno = ESRCH; return -1; }
    return 0;
}

static int last_nspid_is_one(pid_t pid)
{
    char path[64], data[4096], extra, *line, *end; ssize_t length, tail;
    snprintf(path, sizeof(path), GKD_APP_NAMESPACE_PROC_ROOT "/%ld/status", (long)pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return 0;
    length = read(fd, data, sizeof(data) - 1); int saved = errno;
    tail = length >= 0 ? read(fd, &extra, 1) : -1;
    if (tail < 0 && length >= 0) saved = errno;
    close(fd);
    if (length <= 0 || tail < 0) { errno = saved; return 0; }
    if (tail) { errno = EOVERFLOW; return 0; }
    data[length] = 0;
    line = !strncmp(data, "NSpid:\t", 7) ? data : strstr(data, "\nNSpid:\t");
    if (!line) { errno = EPROTO; return 0; }
    if (*line == '\n') ++line;
    line += 7;
    long last = -1;
    while (*line && *line != '\n') {
        while (*line == ' ' || *line == '\t') ++line;
        if (*line == '\n' || !*line) break;
        errno = 0; long value = strtol(line, &end, 10);
        if (errno || end == line || value < 1 || value > INT32_MAX) {
            errno = EPROTO; return 0;
        }
        last = value; line = end;
    }
    if (last != 1) { errno = EINVAL; return 0; }
    return 1;
}

static int different_object(int a, int b)
{
    struct stat left, right;
    if (fstat(a, &left) || fstat(b, &right)) return -1;
    if (left.st_dev == right.st_dev && left.st_ino == right.st_ino) {
        errno = EINVAL; return -1;
    }
    return 0;
}

static int same_object(int a, int b)
{
    struct stat left, right;
    if (fstat(a, &left) || fstat(b, &right)) return -1;
    if (left.st_dev != right.st_dev || left.st_ino != right.st_ino) {
        errno = EINVAL; return -1;
    }
    return 0;
}

void gkd_app_namespace_pin_init(struct gkd_app_namespace_pin *pin)
{
    if (pin) *pin = (struct gkd_app_namespace_pin)GKD_APP_NAMESPACE_PIN_INIT;
}

void gkd_app_namespace_pin_close(struct gkd_app_namespace_pin *pin)
{
    if (!pin) return;
    int *fds[] = {&pin->host_pidfd, &pin->init_pidfd, &pin->pidns_fd,
                  &pin->mntns_fd, &pin->root_fd};
    for (unsigned i = 0; i < sizeof(fds) / sizeof(fds[0]); ++i)
        if (*fds[i] >= 0) { close(*fds[i]); *fds[i] = -1; }
    pin->host = -1; pin->init = -1; pin->init_starttime = 0;
}

int gkd_app_namespace_pin_live(const struct gkd_app_namespace_pin *pin)
{
    if (!pin || pin->host <= 1 || pin->init <= 1 || pin->host == pin->init ||
        pin->host_pidfd < 0 || pin->init_pidfd < 0 || pin->pidns_fd < 0 ||
        pin->mntns_fd < 0 || pin->root_fd < 0) {
        errno = EINVAL; return -1;
    }
    return pidfd_alive(pin->host_pidfd) || pidfd_alive(pin->init_pidfd) ? -1 : 0;
}

int gkd_app_namespace_pin_open(struct gkd_app_namespace_pin *pin,
                               pid_t host, pid_t init)
{
    char path[96];
    int self_pidns = -1, self_root = -1, host_pidns = -1;
    int host_mntns = -1, host_root = -1;
    struct gkd_app_process_identity init_identity;
    if (!pin || host <= 1 || init <= 1 || host == init) { errno = EINVAL; return -1; }
    gkd_app_namespace_pin_init(pin);
    pin->host = host; pin->init = init;
    pin->host_pidfd = (int)syscall(SYS_pidfd_open, host, 0U);
    pin->init_pidfd = (int)syscall(SYS_pidfd_open, init, 0U);
    if (pin->host_pidfd < 0 || pin->init_pidfd < 0) goto error;
    if (pidfd_matches(pin->host_pidfd, host) || pidfd_matches(pin->init_pidfd, init)) goto error;
    if (gkd_app_process_identity_read(init, &init_identity) ||
        init_identity.ppid != host) { errno = ESRCH; goto error; }
    pin->init_starttime = init_identity.starttime;
    if (!last_nspid_is_one(init)) goto error;
    snprintf(path, sizeof(path), GKD_APP_NAMESPACE_PROC_ROOT "/%ld/ns/pid", (long)init);
    pin->pidns_fd = open(path, O_RDONLY | O_CLOEXEC);
    snprintf(path, sizeof(path), GKD_APP_NAMESPACE_PROC_ROOT "/%ld/ns/mnt", (long)init);
    pin->mntns_fd = open(path, O_RDONLY | O_CLOEXEC);
    snprintf(path, sizeof(path), GKD_APP_NAMESPACE_PROC_ROOT "/%ld/root", (long)init);
    pin->root_fd = open(path, O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (pin->pidns_fd < 0 || pin->mntns_fd < 0 || pin->root_fd < 0) goto error;
    self_pidns = open("/proc/self/ns/pid", O_RDONLY | O_CLOEXEC);
    self_root = open("/", O_PATH | O_DIRECTORY | O_CLOEXEC);
    snprintf(path, sizeof(path), GKD_APP_NAMESPACE_PROC_ROOT "/%ld/ns/pid", (long)host);
    host_pidns = open(path, O_RDONLY | O_CLOEXEC);
    snprintf(path, sizeof(path), GKD_APP_NAMESPACE_PROC_ROOT "/%ld/ns/mnt", (long)host);
    host_mntns = open(path, O_RDONLY | O_CLOEXEC);
    snprintf(path, sizeof(path), GKD_APP_NAMESPACE_PROC_ROOT "/%ld/root", (long)host);
    host_root = open(path, O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (self_pidns < 0 || self_root < 0 || host_pidns < 0 || host_mntns < 0 ||
        host_root < 0 || same_object(self_pidns, host_pidns) ||
        different_object(pin->pidns_fd, host_pidns) ||
        different_object(pin->mntns_fd, host_mntns) ||
        same_object(self_root, host_root) || different_object(pin->root_fd, self_root))
        goto error;
    close(self_pidns); self_pidns = -1;
    close(self_root); self_root = -1;
    close(host_pidns); host_pidns = -1;
    close(host_mntns); host_mntns = -1;
    close(host_root); host_root = -1;
    if (pidfd_matches(pin->host_pidfd, host) || pidfd_matches(pin->init_pidfd, init) ||
        gkd_app_process_identity_read(init, &init_identity) ||
        init_identity.ppid != host || init_identity.starttime != pin->init_starttime) {
        errno = ESRCH; goto error;
    }
    return 0;
error: {
    int saved = errno ? errno : EINVAL;
    if (self_pidns >= 0) close(self_pidns);
    if (self_root >= 0) close(self_root);
    if (host_pidns >= 0) close(host_pidns);
    if (host_mntns >= 0) close(host_mntns);
    if (host_root >= 0) close(host_root);
    gkd_app_namespace_pin_close(pin); errno = saved; return -1;
}}
