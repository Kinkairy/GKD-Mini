/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_IDLE_H
#define GKD_APP_IDLE_H
#include "gkd-app-namespace.h"

enum gkd_app_idle_result {
    GKD_APP_IDLE_ERROR = -1,
    GKD_APP_IDLE_BUSY = 0,
    GKD_APP_IDLE_ACQUIRED = 1
};

struct gkd_app_idle_lease {
    struct gkd_app_namespace_pin pin;
    int game_dir_fd, launcher_lock_fd;
};

#define GKD_APP_IDLE_LEASE_INIT { \
    .pin = GKD_APP_NAMESPACE_PIN_INIT, .game_dir_fd = -1, \
    .launcher_lock_fd = -1 }

void gkd_app_idle_lease_init(struct gkd_app_idle_lease *lease);
/* Takes the launcher's exact nonblocking EX lock in the pinned SM root. */
enum gkd_app_idle_result gkd_app_idle_lease_acquire(
    struct gkd_app_idle_lease *lease, pid_t host, pid_t init);
int gkd_app_idle_lease_live(const struct gkd_app_idle_lease *lease);
void gkd_app_idle_lease_release(struct gkd_app_idle_lease *lease);

#endif
