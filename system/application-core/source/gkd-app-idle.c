/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-idle.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static int secure_directory(int fd)
{
    struct stat st;
    if (fstat(fd, &st)) return -1;
    if (!S_ISDIR(st.st_mode) || st.st_uid || st.st_gid || (st.st_mode & 0022)) {
        errno = EPERM; return -1;
    }
    return 0;
}

static int directory_at(int parent, const char *name, int create)
{
    int fd;
    if (create && mkdirat(parent, name, 0700) && errno != EEXIST) return -1;
    fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    if (!secure_directory(fd)) return fd;
    int saved = errno; close(fd); errno = saved; return -1;
}

void gkd_app_idle_lease_init(struct gkd_app_idle_lease *lease)
{
    if (lease) *lease = (struct gkd_app_idle_lease)GKD_APP_IDLE_LEASE_INIT;
}

void gkd_app_idle_lease_release(struct gkd_app_idle_lease *lease)
{
    if (!lease) return;
    if (lease->launcher_lock_fd >= 0) close(lease->launcher_lock_fd);
    if (lease->game_dir_fd >= 0) close(lease->game_dir_fd);
    gkd_app_namespace_pin_close(&lease->pin);
    gkd_app_idle_lease_init(lease);
}

int gkd_app_idle_lease_live(const struct gkd_app_idle_lease *lease)
{
    struct stat st;
    if (!lease || lease->game_dir_fd < 0 || lease->launcher_lock_fd < 0 ||
        gkd_app_namespace_pin_live(&lease->pin) || secure_directory(lease->game_dir_fd))
        return -1;
    if (fstat(lease->launcher_lock_fd, &st)) return -1;
    if (!S_ISREG(st.st_mode) || st.st_uid || st.st_nlink != 1 || (st.st_mode & 0077)) {
        errno = EPERM; return -1;
    }
    return 0;
}

enum gkd_app_idle_result gkd_app_idle_lease_acquire(
    struct gkd_app_idle_lease *lease, pid_t host, pid_t init)
{
    int var = -1, run = -1, saved;
    struct stat st;
    if (!lease || lease->game_dir_fd >= 0 || lease->launcher_lock_fd >= 0 ||
        lease->pin.host_pidfd >= 0 || lease->pin.init_pidfd >= 0 ||
        lease->pin.pidns_fd >= 0 || lease->pin.mntns_fd >= 0 || lease->pin.root_fd >= 0) {
        errno = EINVAL; return GKD_APP_IDLE_ERROR;
    }
    if (gkd_app_namespace_pin_open(&lease->pin, host, init)) return GKD_APP_IDLE_ERROR;
    if (secure_directory(lease->pin.root_fd)) goto error;
    var = directory_at(lease->pin.root_fd, "var", 0);
    if (var < 0 || (run = directory_at(var, "run", 0)) < 0 ||
        (lease->game_dir_fd = directory_at(run, "gkd-mini", 1)) < 0)
        goto error;
    lease->launcher_lock_fd = openat(lease->game_dir_fd, "launcher.lock",
        O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lease->launcher_lock_fd < 0) goto error;
    if (fstat(lease->launcher_lock_fd, &st)) goto error;
    if (!S_ISREG(st.st_mode) || st.st_uid || st.st_nlink != 1 ||
        (st.st_mode & 0077)) { errno = EPERM; goto error; }
    if (flock(lease->launcher_lock_fd, LOCK_EX | LOCK_NB)) {
        saved = errno;
        close(var); close(run);
        gkd_app_idle_lease_release(lease);
        errno = saved;
        return saved == EWOULDBLOCK || saved == EAGAIN ?
            GKD_APP_IDLE_BUSY : GKD_APP_IDLE_ERROR;
    }
    if (secure_directory(lease->pin.root_fd) || secure_directory(var) ||
        secure_directory(run) || gkd_app_idle_lease_live(lease)) goto error;
    close(var); close(run);
    return GKD_APP_IDLE_ACQUIRED;
error:
    saved = errno ? errno : EPERM;
    if (var >= 0) close(var);
    if (run >= 0) close(run);
    gkd_app_idle_lease_release(lease);
    errno = saved; return GKD_APP_IDLE_ERROR;
}
