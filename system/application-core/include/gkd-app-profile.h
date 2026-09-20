/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_PROFILE_H
#define GKD_APP_PROFILE_H
#include <sys/types.h>
/* Trusted, existing paths; callers abandon the mount namespace on failure. */
int gkd_app_bind_readonly(const char *source, const char *target);
int gkd_app_profile_mount(const char *profile, const char *target);
int gkd_app_update_entry_mount(const char *socket_path,const char *service_path,const char *root);
/* Only after the direct host is reaped and its mount namespace is released. */
int gkd_app_profile_cleanup(pid_t host_pid);
#endif
