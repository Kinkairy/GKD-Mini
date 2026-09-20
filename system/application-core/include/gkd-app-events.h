/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_EVENTS_H
#define GKD_APP_EVENTS_H
#include "gkd-input-owner.h"
#include "gkd-app-settings.h"
#include <linux/input.h>
#define GKD_APP_EVENT_POWER 1U
#define GKD_APP_EVENT_RETURN 2U
#define GKD_APP_EVENT_SCREENSHOT 4U
#define GKD_APP_EVENT_ACTIVITY 8U
#define GKD_APP_EVENT_SETTINGS 16U
struct gkd_app_events {
 struct gkd_input_owner observer;
 int exclusive,power_fd,system_fd,dropped[3],barrier,armed,shot_primed,shot_latched;
 unsigned char state[3][KEY_MAX+1];
 unsigned short keys[8], menu_key;
 unsigned shot[2],previous;
};
void gkd_app_events_init(struct gkd_app_events *);
int gkd_app_events_open(struct gkd_app_events *,const struct gkd_app_settings *);
int gkd_app_events_open_menu(struct gkd_app_events *,const struct gkd_app_settings *);
int gkd_app_events_open_menu_reuse(struct gkd_app_events *,
	const struct gkd_app_settings *,struct gkd_menu_guard_owner *);
int gkd_app_events_poll(struct gkd_app_events *,int blocked,unsigned *events);
void gkd_app_events_close(struct gkd_app_events *);
#endif
