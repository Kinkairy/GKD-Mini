/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-idle.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static char fixture_root[PATH_MAX];
static int mock_open_error;
static int mock_live_error;

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

int gkd_app_namespace_pin_open(struct gkd_app_namespace_pin *pin,
                               pid_t host, pid_t init)
{
    if (mock_open_error) { errno = ESRCH; return -1; }
    gkd_app_namespace_pin_init(pin);
    pin->host = host; pin->init = init; pin->init_starttime = 1;
    pin->host_pidfd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    pin->init_pidfd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    pin->pidns_fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    pin->mntns_fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    pin->root_fd = open(fixture_root, O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (pin->host_pidfd < 0 || pin->init_pidfd < 0 || pin->pidns_fd < 0 ||
        pin->mntns_fd < 0 || pin->root_fd < 0) {
        int saved = errno; gkd_app_namespace_pin_close(pin); errno = saved; return -1;
    }
    return 0;
}

int gkd_app_namespace_pin_live(const struct gkd_app_namespace_pin *pin)
{
    if (mock_live_error) { errno = ESRCH; return -1; }
    if (!pin || pin->host_pidfd < 0 || pin->init_pidfd < 0 || pin->root_fd < 0) {
        errno = EINVAL; return -1;
    }
    return 0;
}

static void path(char *result, size_t size, const char *suffix)
{
    assert(snprintf(result, size, "%s/%s", fixture_root, suffix) < (int)size);
}

static void make_root(void)
{
    char pattern[] = "/tmp/gkd-idle-fixture-XXXXXX";
    char *made = mkdtemp(pattern);
    assert(made && strlen(made) < sizeof(fixture_root));
    strcpy(fixture_root, made);
}

static void make_dir(const char *suffix, mode_t mode)
{
    char item[PATH_MAX]; path(item, sizeof(item), suffix);
    assert(!mkdir(item, mode));
}

static void make_tree(void)
{
    make_root();
    make_dir("var", 0755);
    make_dir("var/run", 0755);
}

static void lock_path(char *result, size_t size)
{
    path(result, size, "var/run/gkd-mini/launcher.lock");
}

static void assert_released(const struct gkd_app_idle_lease *lease)
{
    assert(lease->launcher_lock_fd == -1 && lease->game_dir_fd == -1);
    assert(lease->pin.host_pidfd == -1 && lease->pin.init_pidfd == -1 &&
           lease->pin.pidns_fd == -1 && lease->pin.mntns_fd == -1 &&
           lease->pin.root_fd == -1);
}

static void basic_and_busy(void)
{
    make_tree();
    struct gkd_app_idle_lease owner = GKD_APP_IDLE_LEASE_INIT;
    assert(gkd_app_idle_lease_acquire(&owner, 10, 11) == GKD_APP_IDLE_ACQUIRED);
    assert(!gkd_app_idle_lease_live(&owner));
    char lock[PATH_MAX]; lock_path(lock, sizeof(lock));
    struct stat st;
    assert(!stat(lock, &st) && S_ISREG(st.st_mode) && st.st_uid == 0 &&
           st.st_gid == 0 && st.st_nlink == 1 && (st.st_mode & 0777) == 0600);

    int competing = open(lock, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    assert(competing >= 0);
    errno = 0;
    assert(flock(competing, LOCK_EX | LOCK_NB) &&
           (errno == EWOULDBLOCK || errno == EAGAIN));
    close(competing);

    pid_t children[8];
    for (unsigned i = 0; i < sizeof(children) / sizeof(children[0]); ++i) {
        children[i] = fork(); assert(children[i] >= 0);
        if (!children[i]) {
            struct gkd_app_idle_lease contender = GKD_APP_IDLE_LEASE_INIT;
            enum gkd_app_idle_result result =
                gkd_app_idle_lease_acquire(&contender, 10, 11);
            gkd_app_idle_lease_release(&contender);
            _exit(result == GKD_APP_IDLE_BUSY ? 0 : 1);
        }
    }
    for (unsigned i = 0; i < sizeof(children) / sizeof(children[0]); ++i) {
        int status;
        assert(waitpid(children[i], &status, 0) == children[i]);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    gkd_app_idle_lease_release(&owner); assert_released(&owner);
    struct gkd_app_idle_lease next = GKD_APP_IDLE_LEASE_INIT;
    assert(gkd_app_idle_lease_acquire(&next, 10, 11) == GKD_APP_IDLE_ACQUIRED);
    gkd_app_idle_lease_release(&next);
}

static void identity_failures(void)
{
    make_tree();
    struct gkd_app_idle_lease lease = GKD_APP_IDLE_LEASE_INIT;
    mock_open_error = 1;
    assert(gkd_app_idle_lease_acquire(&lease, 10, 11) == GKD_APP_IDLE_ERROR);
    assert_released(&lease);
    mock_open_error = 0; mock_live_error = 1;
    assert(gkd_app_idle_lease_acquire(&lease, 10, 11) == GKD_APP_IDLE_ERROR);
    assert_released(&lease);
    mock_live_error = 0;
    assert(gkd_app_idle_lease_acquire(&lease, 10, 11) == GKD_APP_IDLE_ACQUIRED);
    mock_live_error = 1;
    assert(gkd_app_idle_lease_live(&lease));
    mock_live_error = 0;
    gkd_app_idle_lease_release(&lease);
}

static void path_failures(void)
{
    char item[PATH_MAX];
    make_tree(); assert(!chmod(fixture_root, 0777));
    struct gkd_app_idle_lease lease = GKD_APP_IDLE_LEASE_INIT;
    assert(gkd_app_idle_lease_acquire(&lease, 10, 11) == GKD_APP_IDLE_ERROR);
    assert_released(&lease);

    make_root(); path(item, sizeof(item), "var");
    assert(!symlink("/tmp", item));
    assert(gkd_app_idle_lease_acquire(&lease, 10, 11) == GKD_APP_IDLE_ERROR);
    assert_released(&lease);

    make_tree(); path(item, sizeof(item), "var"); assert(!chmod(item, 0777));
    assert(gkd_app_idle_lease_acquire(&lease, 10, 11) == GKD_APP_IDLE_ERROR);
    assert_released(&lease);

    make_tree(); path(item, sizeof(item), "var/run/gkd-mini");
    assert(!symlink("/tmp", item));
    assert(gkd_app_idle_lease_acquire(&lease, 10, 11) == GKD_APP_IDLE_ERROR);
    assert_released(&lease);
}

static void malformed_lock(int kind)
{
    make_tree(); make_dir("var/run/gkd-mini", 0700);
    char lock[PATH_MAX], other[PATH_MAX]; lock_path(lock, sizeof(lock));
    path(other, sizeof(other), "other");
    if (kind == 0) assert(!symlink("/dev/null", lock));
    else if (kind == 1) assert(!mkdir(lock, 0700));
    else {
        int fd = open(lock, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC,
                      kind == 2 ? 0660 : 0600);
        assert(fd >= 0 && !close(fd));
        if (kind == 2) assert(!chmod(lock, 0660));
        else assert(!link(lock, other));
    }
    struct gkd_app_idle_lease lease = GKD_APP_IDLE_LEASE_INIT;
    assert(gkd_app_idle_lease_acquire(&lease, 10, 11) == GKD_APP_IDLE_ERROR);
    assert_released(&lease);
}

int main(void)
{
    assert(geteuid() == 0);
    basic_and_busy();
    identity_failures();
    path_failures();
    for (int kind = 0; kind < 4; ++kind) malformed_lock(kind);
    puts("GKD_APP_IDLE_FIXTURE=PASS create/nofollow/root-mode/lock-shape/busy/race/identity/live/release");
    return 0;
}
