/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_LOOP_H
#define GKD_APP_LOOP_H
#include <stdint.h>
/* Exact host ownership, not a pathname-based claim or a security boundary
 * against root. The pinned driver has no AUTOCLEAR. */
struct gkd_app_loop {
    int fd;
    unsigned number;
    int unlabelled; /* SET_FD authority retained when initial label write fails. */
    uint64_t device, inode;
    char owner[41];
};
#define GKD_APP_LOOP_INIT { .fd = -1 }
int gkd_app_loop_mount_owned(int image_fd, const char *target, const char *owner,
                             struct gkd_app_loop *lease);
int gkd_app_loop_release(struct gkd_app_loop *lease);
int gkd_app_loop_owned_count(const char *owner);
int gkd_app_loop_mount(int image_fd, const char *target, const char *owner);
int gkd_app_loop_cleanup(const char *owner);
#endif
