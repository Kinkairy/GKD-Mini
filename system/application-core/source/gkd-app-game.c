/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Proven behavior identities:
 * RC2.85 generated power source 3e7904874c24395504e9f090c27008f07741f4f34120fcc6e21b749a0710e08e,
 * power_daemon 5b9f389ce4f4b4aa99a001058c374efe5c2a71b4c93be2a4dd90ed65ea3a5b17.
 * Current accepted OPK 5017c77c4cd10fc4daf6c26cfe434f91bf53bb76bc33ce4f22bd3a93a626833b
 * keeps the registered setsid launcher; SimpleMenu ELF is
 * e4373a1d0ebe4e4474b5b9aa4a753425eb3ec106e05c38b870a5e726608a17e8.
 */
#define _GNU_SOURCE
#include "gkd-app-game-control.h"
#include "gkd-app-namespace.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define ACTIVE_GAME_FILE "/var/run/gkd-mini/active-game"
#define PROC_ROOT "/proc"

#ifdef GKD_APP_GAME_NO_MAIN
#include "gkd-app-namespace.c"
#endif

struct game_target { pid_t pid, pgid; unsigned long long starttime; };

static int fail(const char *stage)
{
    int saved = errno ? errno : EIO;
    fprintf(stderr, "GKD_APP_GAME=FAILED stage=%s errno=%d\n", stage, saved);
    errno = saved; return -1;
}
static int parse_pid(const char *text, pid_t *result)
{
    unsigned long value = 0;
    if (!text || !*text) return 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p < '0' || *p > '9' ||
            value > ((unsigned long)INT32_MAX - (unsigned long)(*p - '0')) / 10UL) return 0;
        value = value * 10UL + (unsigned long)(*p - '0');
    }
    if (value <= 1 || value > INT32_MAX) return 0;
    *result = (pid_t)value; return 1;
}
static int process_name(pid_t pid, char *name, size_t size)
{
    char path[64]; snprintf(path, sizeof(path), PROC_ROOT "/%ld/comm", (long)pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return 0;
    ssize_t length = read(fd, name, size - 1);
    close(fd);
    if (length <= 0) return 0;
    name[length] = 0; return 1;
}
static int protected_name(const char *name)
{
    char lowered[96]; size_t length = strlen(name);
    while (length && isspace((unsigned char)name[length - 1])) --length;
    if (length >= sizeof(lowered)) return 1;
    for (size_t i = 0; i < length; ++i) lowered[i] = (char)tolower((unsigned char)name[i]);
    lowered[length] = 0;
    return strstr(lowered, "simplemenu") || strstr(lowered, "gmenu2x") ||
           strstr(lowered, "power_daemon") || strstr(lowered, "gkd_hardware") || !strcmp(lowered, "init");
}
static int exact_unsigned(const char *text, unsigned long long maximum, unsigned long long *result)
{
    unsigned long long value = 0; if (!*text) return 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p < '0' || *p > '9' || value > (maximum - (unsigned long long)(*p - '0')) / 10ULL) return 0;
        value = value * 10ULL + (unsigned long long)(*p - '0');
    }
    if (!value) return 0;
    *result = value; return 1;
}
static int registered_game(struct game_target *target, int *idle)
{
    char data[384], extra, *lines[4] = {0}, *save = NULL; struct stat st; ssize_t length, tail;
    *idle = 0; int fd = open(ACTIVE_GAME_FILE, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) { if (errno == ENOENT) { *idle = 1; return 0; } return -1; }
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != 0 || (st.st_mode & 0022)) {
        int saved = errno ? errno : EPERM; close(fd); errno = saved; return -1;
    }
    length = read(fd, data, sizeof(data) - 1); int saved = errno;
    tail = length >= 0 ? read(fd, &extra, 1) : -1;
    if (tail < 0 && length >= 0) saved = errno;
    close(fd);
    if (length <= 0 || tail < 0) { errno = length == 0 ? EPROTO : saved; return -1; }
    if (tail || data[length - 1] != '\n') { errno = EPROTO; return -1; }
    data[length - 1] = 0;
    if (data[0] == '\n' || (length > 1 && data[length - 2] == '\n') ||
        strstr(data, "\n\n")) { errno = EPROTO; return -1; }
    unsigned count = 0;
    for (char *line = strtok_r(data, "\n", &save); line && count < 4; line = strtok_r(NULL, "\n", &save))
        lines[count++] = line;
    if (count != 3 || strncmp(lines[0], "pid=", 4) || strncmp(lines[1], "pgid=", 5) ||
        strncmp(lines[2], "starttime=", 10)) { errno = EPROTO; return -1; }
    unsigned long long pid, pgid, starttime;
    if (!exact_unsigned(lines[0] + 4, INT32_MAX, &pid) || !exact_unsigned(lines[1] + 5, INT32_MAX, &pgid) ||
        !exact_unsigned(lines[2] + 10, ~0ULL, &starttime) || pid <= 1 || pgid <= 1) {
        errno = EPROTO; return -1;
    }
    struct gkd_app_process_identity identity; char name[96];
    if (gkd_app_process_identity_read((pid_t)pid, &identity) || identity.starttime != starttime ||
        identity.pgid != (pid_t)pgid || identity.sid != (pid_t)pgid || pid != pgid ||
        (pid_t)pgid == getpgrp() || !process_name((pid_t)pid, name, sizeof(name)) || protected_name(name)) {
        errno = ESRCH; return -1;
    }
    target->pid = (pid_t)pid; target->pgid = (pid_t)pgid; target->starttime = starttime; return 0;
}
int gkd_app_game_self(unsigned long long *start)
{
    struct gkd_app_process_identity identity;
    if (!start || gkd_app_process_identity_read(getpid(), &identity)) return -1;
    *start = identity.starttime; return 0;
}
static int run_inside(int do_exit, unsigned long long init_starttime)
{
    struct gkd_app_process_identity init_identity;
    if (gkd_app_process_identity_read(1, &init_identity) || init_identity.starttime != init_starttime) {
        errno = ESRCH; return fail("init-revalidate");
    }
    struct game_target target; int idle;
    if (registered_game(&target, &idle)) return fail("registry");
    if (idle) {
        if(do_exit==3){struct gkd_game_orientation s=GKD_GAME_ORIENTATION_INIT;gkd_game_orientation_print(&s,0);return ferror(stdout)?fail("stdout"):0;}
        if (do_exit) { errno = ENOENT; return fail("idle"); }
        if (puts("IDLE") == EOF) return fail("stdout");
        return 0;
    }
    if (!do_exit) { if (puts("ACTIVE") == EOF) return fail("stdout"); return 0; }
    struct gkd_app_process_identity identity; char name[96];
    if (gkd_app_process_identity_read(target.pid, &identity) || identity.starttime != target.starttime ||
        identity.pgid != target.pgid || identity.sid != target.pgid || target.pid != target.pgid ||
        !process_name(target.pid, name, sizeof(name)) || protected_name(name)) {
        errno = ESRCH; return fail("exit-revalidate");
    }
    int game_fd = (int)syscall(SYS_pidfd_open, target.pid, 0);
    if (game_fd < 0) return fail("native-game-pin");
    if (gkd_app_process_identity_read(target.pid, &identity) || identity.starttime != target.starttime) {
        close(game_fd); errno = ESRCH; return fail("native-game-race");
    }
    struct gkd_game_orientation orientation=GKD_GAME_ORIENTATION_INIT;
    int request_result = do_exit==3?gkd_app_game_request_orientation(target.pid,target.starttime,game_fd,&orientation):gkd_app_game_request_operation(target.pid, target.starttime, game_fd,
        do_exit==2?GKD_GAME_MENU:GKD_GAME_EXIT);
    int request_error = errno; close(game_fd); errno = request_error;
    if (request_result) return fail("native-exit-request");
    if(do_exit==3){gkd_game_orientation_print(&orientation,1);return ferror(stdout)?fail("stdout"):0;}
    if (puts(do_exit==2?"MENU_DELIVERED":"EXITED") == EOF) return fail("stdout");
    return 0;
}
int gkd_app_game_main(int argc, char **argv)
{
    pid_t host, init; int do_exit;
    if (geteuid() || argc != 4 || (strcmp(argv[1], "check") && strcmp(argv[1], "exit") && strcmp(argv[1], "menu") && strcmp(argv[1], "orientation")) ||
        !parse_pid(argv[2], &host) || !parse_pid(argv[3], &init) || host == init) {
        errno = EINVAL; fail("arguments"); return 64;
    }
    do_exit = !strcmp(argv[1],"orientation")?3:!strcmp(argv[1], "menu")?2:!strcmp(argv[1], "exit"); struct gkd_app_namespace_pin target;
    gkd_app_namespace_pin_init(&target);
    if (gkd_app_namespace_pin_open(&target, host, init)) { fail("namespace"); return 65; }
    if (setns(target.pidns_fd, CLONE_NEWPID) || setns(target.mntns_fd, CLONE_NEWNS) ||
        fchdir(target.root_fd) || chroot(".") || chdir("/")) {
        fail("enter"); gkd_app_namespace_pin_close(&target); return 66;
    }
    if (gkd_app_namespace_pin_live(&target)) {
        fail("namespace-race"); gkd_app_namespace_pin_close(&target); return 65;
    }
    pid_t child = fork();
    if (child < 0) { fail("fork"); gkd_app_namespace_pin_close(&target); return 66; }
    if (!child) {
        int result = run_inside(do_exit, target.init_starttime);
        fflush(stdout); fflush(stderr); _exit(result ? 1 : 0);
    }
    int status; pid_t got;
    do got = waitpid(child, &status, 0); while (got < 0 && errno == EINTR);
    gkd_app_namespace_pin_close(&target);
    if (got != child || !WIFEXITED(status)) { fail("reap"); return 67; }
    return WEXITSTATUS(status);
}
#ifndef GKD_APP_GAME_NO_MAIN
int main(int argc, char **argv) { return gkd_app_game_main(argc, argv); }
#endif
