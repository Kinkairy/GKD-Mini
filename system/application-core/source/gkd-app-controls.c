/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-controls.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define GKD_CONTROLS_READY_MS UINT64_C(5000)
#define GKD_CONTROLS_STOP_MS UINT64_C(1000)
#ifndef GKD_APP_CONTROLS_LAUNCHER
#define GKD_APP_CONTROLS_LAUNCHER "/usr/sbin/gkd-controls-start"
#endif

static int failed(struct gkd_app_controls *controls, int error)
{
	errno = error;
	controls->state = GKD_APP_CONTROLS_FAILED;
	return -1;
}

static int live(int pidfd)
{
	struct pollfd poller = {pidfd, POLLIN | POLLHUP | POLLERR, 0};
	if (pidfd < 0) { errno = EBADF; return 0; }
	return poll(&poller, 1, 0) == 0;
}

static int note_time(struct gkd_app_controls *controls, uint64_t now_ms)
{
	if (now_ms == 0U || (controls->last_now_ms && now_ms < controls->last_now_ms)) {
		errno = EINVAL;
		return -1;
	}
	controls->last_now_ms = now_ms;
	return 0;
}

static int collect(struct gkd_app_controls *controls)
{
	int status;
	pid_t result;
	if (controls->pid <= 0) return 0;
	result = waitpid(controls->pid, &status, WNOHANG);
	if (result == 0) return 0;
	if (result != controls->pid) {
        if (result < 0 && errno == ECHILD) controls->pid = -1;
        return -1;
    }
	controls->pid = -1;
	controls->exit_status = status;
	return 1;
}

static int secure_directory(int fd)
{
	struct stat state;
	if (fstat(fd, &state) < 0) return -1;
	if (!S_ISDIR(state.st_mode) || state.st_uid != 0 || (state.st_mode & 0022)) {
		errno = EPERM;
		return -1;
	}
	return 0;
}

static int walk_directory(int parent, const char *name, int create, dev_t device)
{
    struct stat st;
    int fd;
    if (create && mkdirat(parent, name, 0700) < 0 && errno != EEXIST) return -1;
    fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    if (secure_directory(fd) < 0 || fstat(fd, &st) < 0) {
        int saved = errno; (void)close(fd); errno = saved; return -1;
    }
    if (st.st_dev != device) { (void)close(fd); errno = EXDEV; return -1; }
    return fd;
}
static int open_state_directory(int init_pidfd, pid_t init_pid)
{
    char root[96]; struct stat block, data_stat;
    dev_t expected;
    int data=-1, local=-1, home=-1, state=-1;
    if (init_pid <= 1 || !live(init_pidfd)) { errno = ESRCH; return -1; }
    if (snprintf(root, sizeof(root), "/proc/%ld/root/media/data", (long)init_pid) >= (int)sizeof(root)) {
        errno = EOVERFLOW; return -1;
    }
#ifdef GKD_APP_CONTROLS_TEST_EXPECTED_PATH
    if (stat(GKD_APP_CONTROLS_TEST_EXPECTED_PATH, &block) < 0) return -1;
    expected = block.st_dev;
#else
    if (stat("/dev/mmcblk0p2", &block) < 0) return -1;
    if (!S_ISBLK(block.st_mode)) { errno = ENODEV; return -1; }
    expected = block.st_rdev;
#endif
    data = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (data < 0 || secure_directory(data) < 0 || fstat(data, &data_stat) < 0) goto fail;
    if (data_stat.st_dev != expected) { errno = EXDEV; goto fail; }
    local = walk_directory(data, "local", 0, expected);
    if (local < 0) goto fail;
    home = walk_directory(local, "home", 0, expected);
    if (home < 0) goto fail;
    state = walk_directory(home, ".gkdmini", 1, expected);
    if (state < 0) goto fail;
    if (!live(init_pidfd)) { errno = ESRCH; goto fail; }
    (void)close(home); (void)close(local); (void)close(data);
    return state;
fail:
    {
        int saved = errno;
        if (state >= 0) (void)close(state);
        if (home >= 0) (void)close(home);
        if (local >= 0) (void)close(local);
        if (data >= 0) (void)close(data);
        errno = saved;
    }
    return -1;
}

static void child_exec(int statefd, int readyfd, pid_t parent)
{
	char state_arg[16], ready_arg[16];
	int state_copy, ready_copy;
	if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0 || getppid() != parent) _exit(125);
	state_copy = fcntl(statefd, F_DUPFD_CLOEXEC, 5);
	ready_copy = fcntl(readyfd, F_DUPFD_CLOEXEC, 5);
	if (state_copy < 0 || ready_copy < 0 || dup2(state_copy, 3) < 0 ||
	    dup2(ready_copy, 4) < 0) _exit(125);
	statefd = 3;
	readyfd = 4;
	if (fcntl(statefd, F_SETFD, 0) < 0 || fcntl(readyfd, F_SETFD, 0) < 0 ||
	    snprintf(state_arg, sizeof(state_arg), "%d", statefd) <= 0 ||
	    snprintf(ready_arg, sizeof(ready_arg), "%d", readyfd) <= 0)
		_exit(125);
	if (syscall(SYS_close_range, 5U, ~0U, 0U) < 0) _exit(125);
	{ char *const argv[] = {GKD_APP_CONTROLS_LAUNCHER, "--managed", state_arg, ready_arg, NULL};
	  execv(argv[0], argv); }
	_exit(125);
}

int gkd_app_controls_start(struct gkd_app_controls *controls, int init_pidfd, pid_t init_pid,
	uint64_t now_ms)
{
	int ready[2];
	pid_t pid, parent;
	if (!controls || controls->state != GKD_APP_CONTROLS_IDLE || note_time(controls, now_ms) < 0 ||
	    now_ms > UINT64_MAX - GKD_CONTROLS_READY_MS || !live(init_pidfd)) { errno = EINVAL; return -1; }
	controls->statefd = open_state_directory(init_pidfd, init_pid);
	if (controls->statefd < 0) return failed(controls, errno);
	if (!live(init_pidfd)) { (void)close(controls->statefd); controls->statefd = -1; return failed(controls, ESRCH); }
	if (pipe2(ready, O_CLOEXEC | O_NONBLOCK) < 0) { int saved = errno; (void)close(controls->statefd); controls->statefd = -1; return failed(controls, saved); }
	parent = getpid();
	pid = fork();
	if (pid < 0) { int saved = errno; (void)close(ready[0]); (void)close(ready[1]); (void)close(controls->statefd); controls->statefd = -1; return failed(controls, saved); }
	if (pid == 0) { (void)close(ready[0]); child_exec(controls->statefd, ready[1], parent); }
	(void)close(ready[1]);
	controls->pid = pid;
	controls->readyfd = ready[0];
	controls->pidfd = (int)syscall(SYS_pidfd_open, pid, 0);
	if (controls->pidfd < 0) {
        int saved = errno;
        /* Keep the unreaped direct child owned. The same bounded stop path can
         * signal its PID safely even when pidfd allocation failed. */
        (void)close(controls->statefd); controls->statefd = -1;
        return failed(controls, saved);
    }
	(void)close(controls->statefd); controls->statefd = -1;
	controls->deadline_ms = now_ms + GKD_CONTROLS_READY_MS;
	controls->state = GKD_APP_CONTROLS_WAITING;
	return 0;
}

enum gkd_app_controls_state gkd_app_controls_poll(struct gkd_app_controls *controls, uint64_t now_ms)
{
	char ready;
	ssize_t count;
	int observed;
	if (!controls || controls->state == GKD_APP_CONTROLS_REAPED || controls->state == GKD_APP_CONTROLS_FAILED)
		return controls ? controls->state : GKD_APP_CONTROLS_FAILED;
	if (note_time(controls, now_ms) < 0) return failed(controls, errno), controls->state;
	observed = collect(controls);
	if (observed != 0) return failed(controls, observed > 0 ? ECHILD : errno), controls->state;
	if (controls->state == GKD_APP_CONTROLS_READY) return controls->state;
	if (controls->state != GKD_APP_CONTROLS_WAITING) return controls->state;
	count = read(controls->readyfd, &ready, 1U);
	if (count == 1 && ready == 'R') {
		if (now_ms >= controls->deadline_ms)
			return failed(controls, ETIMEDOUT), controls->state;
		observed = collect(controls);
		if (!live(controls->pidfd) || observed != 0)
			return failed(controls, ECHILD), controls->state;
		(void)close(controls->readyfd); controls->readyfd = -1;
		controls->state = GKD_APP_CONTROLS_READY; return controls->state;
	}
	if (count == 1 || count == 0 || (count < 0 && errno != EAGAIN && errno != EINTR) || now_ms >= controls->deadline_ms)
		return failed(controls, count == 1 ? EPROTO : count == 0 ? EPIPE : now_ms >= controls->deadline_ms ? ETIMEDOUT : errno), controls->state;
	return controls->state;
}

enum gkd_app_controls_state gkd_app_controls_stop(struct gkd_app_controls *controls, uint64_t now_ms)
{
	if (!controls) return GKD_APP_CONTROLS_FAILED;
    if (controls->pid <= 0) return controls->state;
	if (note_time(controls, now_ms) < 0) return failed(controls, errno), controls->state;
	{
		int observed = collect(controls);
		if (observed != 0) { controls->state = observed > 0 ? GKD_APP_CONTROLS_REAPED : GKD_APP_CONTROLS_FAILED; return controls->state; }
	}
	if (controls->state != GKD_APP_CONTROLS_STOPPING) {
		if (now_ms > UINT64_MAX - GKD_CONTROLS_STOP_MS) return failed(controls, EOVERFLOW), controls->state;
		if (kill(controls->pid, SIGTERM) < 0 && errno != ESRCH) return failed(controls, errno), controls->state;
		controls->stop_deadline_ms = now_ms + GKD_CONTROLS_STOP_MS;
		controls->state = GKD_APP_CONTROLS_STOPPING;
		return controls->state;
	}
	if (now_ms >= controls->stop_deadline_ms && controls->kill_deadline_ms == 0U) {
		if (now_ms > UINT64_MAX - GKD_CONTROLS_STOP_MS) return failed(controls, EOVERFLOW), controls->state;
		if (kill(controls->pid, SIGKILL) < 0 && errno != ESRCH) return failed(controls, errno), controls->state;
		controls->kill_deadline_ms = now_ms + GKD_CONTROLS_STOP_MS;
	} else if (controls->kill_deadline_ms && now_ms >= controls->kill_deadline_ms) {
		return failed(controls, ETIMEDOUT), controls->state;
	}
	return controls->state;
}

void gkd_app_controls_close(struct gkd_app_controls *controls)
{
	if (!controls) return;
    /* Never erase ownership of a live or unconfirmed child. */
    if (controls->pid > 0) { errno = EBUSY; return; }
	if (controls->readyfd >= 0) (void)close(controls->readyfd);
	if (controls->statefd >= 0) (void)close(controls->statefd);
	if (controls->pidfd >= 0) (void)close(controls->pidfd);
	*controls = (struct gkd_app_controls)GKD_APP_CONTROLS_INIT;
}
