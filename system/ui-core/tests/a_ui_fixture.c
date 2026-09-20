// SPDX-License-Identifier: GPL-2.0
#include "gkd-ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct gkd_ui_menu_item menu_items[] = {
	{"EXPORT SYSTEM CARD", 0U}, {"SYSTEM UPDATE", 1U},
	{"REBOOT", 3U}, {"SHUTDOWN", 4U},
};

static const struct {
	const char *name;
	struct gkd_ui_osd value;
} osds[] = {
	{"volume", {"VOL 50%", 0U, 50, 0}},
	{"brightness", {"BRT 70%", 1U, 70, 0}},
	{"battery", {"BAT 76%", 2U, 76, 0}},
	{"screenshot", {"SHOT SAVED", 3U, -1, 0}},
	{"low-battery", {"BAT 5%", 2U, 5, 1}},
	{"network", {"192.168.137.2", 4U, -1, 0}},
	{"charge", {"CHARGING", 5U, -1, 0}},
	{"storage", {"STORAGE", 6U, -1, 0}},
	{"debug", {"DEBUG", 7U, -1, 0}},
};

int main(int argc, char **argv)
{
	struct gkd_ui_config config;
	struct gkd_ui_font font;
	struct gkd_ui_surface surface;
	struct gkd_ui_menu menu;
	uint16_t pixels[GKD_UI_WIDTH * GKD_UI_HEIGHT];
	char *end = NULL;
	unsigned index, opacity;
	if (argc != 5) return 2;
	opacity = (unsigned)strtoul(argv[4], &end, 10);
	if (!end || *end || opacity > 255U) return 2;
	for (index = 0; index < sizeof(osds) / sizeof(osds[0]); ++index)
		if (!strcmp(argv[3], osds[index].name)) break;
	if (index == sizeof(osds) / sizeof(osds[0])) return 2;
	gkd_ui_config_defaults(&config);
	if (gkd_ui_font_load(&font, argv[1])) return 2;
	surface = (struct gkd_ui_surface){pixels, GKD_UI_WIDTH, GKD_UI_HEIGHT,
		GKD_UI_WIDTH};
	menu = (struct gkd_ui_menu){"RECOVERY", menu_items, 4U, 0U,
		GKD_UI_ACTION_ENABLED, GKD_UI_ACTION_DISABLED, "SURE", "CANCEL"};
	if (gkd_ui_render_menu(&surface, &config, &font, &menu) ||
		gkd_ui_draw_osd(&surface, &config, &font, &osds[index].value, opacity) ||
		gkd_ui_write_raw(&surface, argv[2])) {
		gkd_ui_font_release(&font); return 2;
	}
	gkd_ui_font_release(&font);
	return 0;
}
