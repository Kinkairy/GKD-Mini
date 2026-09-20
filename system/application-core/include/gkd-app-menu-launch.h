/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_MENU_LAUNCH_H
#define GKD_APP_MENU_LAUNCH_H
#include "gkd-app-menu-config.h"
struct gkd_app_menu_launch { int fd; struct gkd_menu_profile profile; };
#define GKD_APP_MENU_LAUNCH_INIT {.fd=-1}
int gkd_app_menu_prepare(struct gkd_app_menu_launch *,int opk,const char *desktop,const char *exec,int argc,char *const argv[]);
int gkd_app_menu_pulse(struct gkd_app_menu_launch *);
int gkd_app_menu_receipt(struct gkd_app_menu_launch *);
int gkd_app_menu_cancel(struct gkd_app_menu_launch *);
void gkd_app_menu_close(struct gkd_app_menu_launch *);
#endif
