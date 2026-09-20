/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_NAMESPACE_H
#define GKD_APP_NAMESPACE_H
#include <sys/types.h>

struct gkd_app_process_identity {
    pid_t ppid, pgid, sid;
    unsigned long long starttime;
};

struct gkd_app_namespace_pin {
    pid_t host, init;
    int host_pidfd, init_pidfd, pidns_fd, mntns_fd, root_fd;
    unsigned long long init_starttime;
};

#define GKD_APP_NAMESPACE_PIN_INIT { \
    .host = -1, .init = -1, .host_pidfd = -1, .init_pidfd = -1, \
    .pidns_fd = -1, .mntns_fd = -1, .root_fd = -1 }

int gkd_app_process_identity_read(pid_t pid,
                                  struct gkd_app_process_identity *identity);
void gkd_app_namespace_pin_init(struct gkd_app_namespace_pin *pin);
int gkd_app_namespace_pin_open(struct gkd_app_namespace_pin *pin,
                               pid_t host, pid_t init);
/* Safe before or after entering the pinned namespace: checks both pidfds. */
int gkd_app_namespace_pin_live(const struct gkd_app_namespace_pin *pin);
void gkd_app_namespace_pin_close(struct gkd_app_namespace_pin *pin);

#endif
