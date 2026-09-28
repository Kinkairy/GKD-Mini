/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_ORIENTATION_H
#define GKD_APP_ORIENTATION_H
#include "gkd-game-orientation.h"
#include "gkd-app-menu-config.h"
struct gkd_app_orientation {
    struct gkd_game_orientation launch;
    int fd,lifetime;
    struct gkd_orientation_page *page;
};
#define GKD_APP_ORIENTATION_INIT {GKD_GAME_ORIENTATION_INIT,-1,-1,NULL}
void gkd_app_orientation_prepare(struct gkd_app_orientation *,const struct gkd_menu_profile *,int,char *const []);
void gkd_app_orientation_close(struct gkd_app_orientation *);
void gkd_app_orientation_read(const struct gkd_app_orientation *,struct gkd_game_orientation *);
/* Adapter entry for bounded ROM parsing; no launcher or device dependencies. */
void gkd_app_orientation_rom(const char *,int,struct gkd_game_orientation *);
#endif
