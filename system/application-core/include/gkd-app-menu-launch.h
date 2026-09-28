/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_MENU_LAUNCH_H
#define GKD_APP_MENU_LAUNCH_H
#include "gkd-app-menu-config.h"
#include "gkd-game-orientation.h"
struct gkd_app_menu_launch {
 int fd; struct gkd_menu_profile profile;
 struct gkd_menu_vt_config base;
 int portrait, orientation_disabled;
};
#define GKD_APP_MENU_LAUNCH_INIT {.fd=-1}
int gkd_app_menu_prepare(struct gkd_app_menu_launch *,int opk,const char *desktop,const char *exec,int argc,char *const argv[]);
/* 0 settled, 1 retry after held keys/MENU drain, -1 route failure. */
int gkd_app_menu_orientation(struct gkd_app_menu_launch *,const struct gkd_game_orientation *);
int gkd_app_menu_pulse(struct gkd_app_menu_launch *);
int gkd_app_menu_receipt(struct gkd_app_menu_launch *);
int gkd_app_menu_cancel(struct gkd_app_menu_launch *);
void gkd_app_menu_close(struct gkd_app_menu_launch *);
#endif
