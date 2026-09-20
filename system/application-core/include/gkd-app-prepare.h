/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_PREPARE_H
#define GKD_APP_PREPARE_H
#include "gkd-app-exec.h"
#include <stdint.h>
struct gkd_app_prepare_request {
    struct gkd_app_exec_request app;
    int init_executable_fd, error_fd, release_fd;
    int controller_pidns_fd, controller_mntns_fd;
    pid_t controller_mapped_pid;
    uid_t controller_mapped_uid;
    uint64_t timeout_ms;
};
/* One-use SINGLE-THREADED worker already in a verified prepared root and
 * private PID/mount namespace as PID 1. Controller namespace descriptors must
 * be opened/verified by the original RAM controller, not chosen by the worker.
 * All nine input descriptors are distinct CLOEXEC >=3. Channels are the
 * existing ready sender, watch-error sender and release receiver.
 * app.preparer_pid is overwritten with 1; strings/storage remain valid until exec.
 *
 * Checks namespace separation and empty, root-owned, read-only /etc/inittab.
 * Does not create namespaces, mount, seal/hash a root or obtain hardware ownership.
 * After preflight it REPLACES THE WORKER'S FD TABLE and signal state, forks one
 * blocked child, hands off its pidfd and execs pinned existing init. Child uses
 * shared release + exec-report; never a second init/reaper or wire protocol.
 * Success does not return. ANY -1/errno return requires immediate _exit by the
 * worker, including after fork/partial replacement. Its external controller
 * owns the pinned namespace lifetime and teardown. Not for the RAM supervisor.
 */
int gkd_app_prepare_replace(const struct gkd_app_prepare_request *request);
#endif
