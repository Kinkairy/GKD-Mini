/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_SETTINGS_H
#define GKD_APP_SETTINGS_H
struct gkd_app_settings {
    unsigned input_style;
    unsigned effects, show_fps, chinese, frontend_timeout;
    unsigned usb_default, power_default, auto_suspend_seconds, usb_notify_ms;
    unsigned battery_poll_ms, battery_low, battery_critical, battery_hysteresis;
    unsigned battery_suspend_enabled, battery_suspend_delay_ms;
    unsigned battery_curve[5], battery_leds[3];
    unsigned screenshot_pair[2], screenshot_notify_ms, battery_notify_ms;
    char screenshot_directory[256];
    char keys[8][24];
    char menu_key[24], brightness_key[24], left_key[24], right_key[24];
};
/* One atomic compiled config snapshot; no defaults or alternate config path. */
int gkd_app_settings_load(const char *,struct gkd_app_settings *);
#endif
