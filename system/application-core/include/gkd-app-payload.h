/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_PAYLOAD_H
#define GKD_APP_PAYLOAD_H
#include <signal.h>
struct gkd_app_fps_launch;
struct gkd_app_menu_launch;
struct gkd_app_payload_result { int wait_status, client, forced, reaped; };
/* A fully populated fps argument transfers its three fds to the new namespace
 * on successful clone; the structure is reset before this blocking call waits. */
int gkd_app_payload_run(char *const argv[],const char *directory,int listener,
                        unsigned long long start,volatile sig_atomic_t *stopping,
                        struct gkd_app_fps_launch *fps,
                        struct gkd_app_payload_result *result);
int gkd_app_payload_run_menu(char *const [],const char *,int,unsigned long long,
 volatile sig_atomic_t *,struct gkd_app_fps_launch *,struct gkd_app_menu_launch *,struct gkd_app_payload_result *);
#endif
