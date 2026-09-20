// SPDX-License-Identifier: GPL-2.0
#include "gkd-ui.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

static const struct gkd_ui_menu_item items[] = {
	{"EXPORT SYSTEM CARD", 0U}, {"SYSTEM UPDATE", 1U},
	{"REBOOT", 3U}, {"SHUTDOWN", 4U},
};

static int same_button(const uint16_t *left, const uint16_t *right, unsigned x)
{
	for (unsigned y = GKD_UI_OSD_Y; y < GKD_UI_OSD_Y + GKD_UI_OSD_HEIGHT; ++y)
		for (unsigned column = x; column < x + 71U; ++column)
			if (left[y * 323U + column] != right[y * 323U + column]) return 0;
	return 1;
}

int main(int argc, char **argv)
{
	struct gkd_ui_config config;
	struct gkd_ui_font font;
	struct gkd_ui_surface surface;
	struct gkd_ui_menu menu;
	struct gkd_ui_osd osd = {"VOL 50%", 0U, 50, 0};
	struct gkd_ui_menu_item items5[5];
	uint16_t *pixels, *before, *first;
	uint32_t argb[GKD_UI_OSD_WIDTH * GKD_UI_OSD_HEIGHT + 1U];
	uint32_t argb_before[GKD_UI_OSD_WIDTH * GKD_UI_OSD_HEIGHT + 1U];
	unsigned opacity = 99U;
	size_t count = 323U * GKD_UI_HEIGHT;
	if (argc != 2) return 2;
	pixels = calloc(count, sizeof(*pixels)); before = calloc(count, sizeof(*before));
	first = calloc(count, sizeof(*first));
	if (!pixels || !before || !first) return 2;
	memset(pixels, 0xa5, count * sizeof(*pixels));
	gkd_ui_config_defaults(&config);
	if (gkd_ui_font_load(&font, argv[1])) return 2;
	surface = (struct gkd_ui_surface){pixels, GKD_UI_WIDTH, GKD_UI_HEIGHT, 323U};
	menu = (struct gkd_ui_menu){"RECOVERY", items, 4U, 0U,
		GKD_UI_ACTION_ENABLED, GKD_UI_ACTION_DISABLED, "SURE", "CANCEL"};
	memcpy(before, pixels, count * sizeof(*pixels));
	if (gkd_ui_render_menu(NULL, &config, &font, &menu) != -1 ||
		gkd_ui_render_menu(&surface, NULL, &font, &menu) != -1 ||
		gkd_ui_render_menu(&surface, &config, NULL, &menu) != -1 ||
		gkd_ui_render_menu(&surface, &config, &font, NULL) != -1 ||
		memcmp(pixels, before, count * sizeof(*pixels))) return 13;
	menu.selected = 4U;
	if (gkd_ui_render_menu(&surface, &config, &font, &menu) != -1 ||
		memcmp(pixels, before, count * sizeof(*pixels))) return 3;
	menu.selected = 0U;
	memcpy(items5, items, sizeof(items)); items5[4] = items[0];
	menu.items = items5; menu.count = 5U; config.menu_row_y = 60U;
	if (gkd_ui_render_menu(&surface, &config, &font, &menu) != -1 ||
		memcmp(pixels, before, count * sizeof(*pixels))) return 9;
	config.menu_row_y = 48U; menu.count = 4U; menu.items = items;
	config.menu_row_step = 0U;
	if (gkd_ui_render_menu(&surface, &config, &font, &menu) != -1 ||
		memcmp(pixels, before, count * sizeof(*pixels))) return 14;
	config.menu_row_step = UINT_MAX;
	if (gkd_ui_render_menu(&surface, &config, &font, &menu) != -1 ||
		memcmp(pixels, before, count * sizeof(*pixels))) return 15;
	config.menu_row_step = 31U;
	config.menu_row_width = UINT_MAX;
	if (gkd_ui_render_menu(&surface, &config, &font, &menu) != -1 ||
		memcmp(pixels, before, count * sizeof(*pixels))) return 16;
	config.menu_row_width = 240U;
	config.panel_alpha = 256U;
	if (gkd_ui_render_menu(&surface, &config, &font, &menu) != -1 ||
		memcmp(pixels, before, count * sizeof(*pixels))) return 27;
	config.panel_alpha = 235U;
	memset(config.action_yes, 'X', sizeof(config.action_yes));
	if (gkd_ui_render_menu(&surface, &config, &font, &menu) != -1 ||
		memcmp(pixels, before, count * sizeof(*pixels))) return 28;
	memcpy(config.action_yes, "SURE", 5U);
	config.menu_content_shift = 31U;
	if (gkd_ui_render_menu(&surface, &config, &font, &menu) != -1 ||
		memcmp(pixels, before, count * sizeof(*pixels))) return 29;
	config.menu_content_shift = 12U;
	menu.action_a = (enum gkd_ui_action_state)-1;
	if (gkd_ui_render_menu(&surface, &config, &font, &menu) != -1 ||
		memcmp(pixels, before, count * sizeof(*pixels))) return 10;
	menu.action_a = GKD_UI_ACTION_ENABLED;
	font.charsize = 1U;
	if (gkd_ui_render_menu(&surface, &config, &font, &menu) != -1 ||
		memcmp(pixels, before, count * sizeof(*pixels))) return 11;
	font.charsize = 16U;
	if (gkd_ui_render_menu(&surface, &config, &font, &menu)) return 4;
	memcpy(before, pixels, count * sizeof(*pixels));
	menu.action_b = GKD_UI_ACTION_ENABLED;
	if (gkd_ui_render_menu(&surface, &config, &font, &menu)) return 22;
	if (!same_button(before, pixels, 164U) || same_button(before, pixels, 241U)) return 23;
	menu.action_a = GKD_UI_ACTION_HIDDEN;
	menu.action_b = GKD_UI_ACTION_HIDDEN;
	if (gkd_ui_render_menu(&surface, &config, &font, &menu)) return 24;
	memcpy(first, pixels, count * sizeof(*pixels));
#if GKD_APPLICATION_UI
	menu.action_a = menu.action_b = GKD_UI_ACTION_DISABLED;
	if (gkd_ui_render_menu(&surface, &config, &font, &menu) ||
		memcmp(first, pixels, count * sizeof(*pixels))) return 40;
	menu.action_a = menu.action_b = GKD_UI_ACTION_HIDDEN;
#endif
	menu.action_a = GKD_UI_ACTION_ENABLED;
	if (gkd_ui_render_menu(&surface, &config, &font, &menu) ||
		!same_button(first, pixels, 241U)) return 25;
	menu.action_b = GKD_UI_ACTION_DISABLED;
	if (gkd_ui_render_menu(&surface, &config, &font, &menu) ||
		!same_button(before, pixels, 164U) ||
#if GKD_APPLICATION_UI
		!same_button(first, pixels, 241U)
#else
		same_button(first, pixels, 241U)
#endif
		) return 26;
#if GKD_APPLICATION_UI
	/* Legacy absent labels use the validated default labels, including when
	 * both unavailable buttons are kept visible on update progress pages. */
	menu.action_a = menu.action_b = GKD_UI_ACTION_HIDDEN;
	menu.action_a_label = menu.action_b_label = NULL;
	if (gkd_ui_render_menu(&surface, &config, &font, &menu)) return 41;
	memcpy(first, pixels, count * sizeof(*pixels));
	menu.action_a = menu.action_b = GKD_UI_ACTION_DISABLED;
	menu.action_a_label = config.action_yes; menu.action_b_label = config.action_no;
	if (gkd_ui_render_menu(&surface, &config, &font, &menu) ||
		memcmp(first, pixels, count * sizeof(*pixels))) return 42;
	/* Failure pages retain the same disabled A and enabled B primitives. */
	menu.action_b = GKD_UI_ACTION_ENABLED;
	if (gkd_ui_render_menu(&surface, &config, &font, &menu)) return 43;
	memcpy(first, pixels, count * sizeof(*pixels));
	gkd_ui_render_status(&surface, &config, &font, "ACTION FAILED", 1);
	if (!same_button(first, pixels, 164U) || !same_button(first, pixels, 241U)) return 44;
#endif
	memcpy(before, pixels, count * sizeof(*pixels));
	if (gkd_ui_draw_osd(NULL, &config, &font, &osd, 255U) != -1 ||
		gkd_ui_draw_osd(&surface, NULL, &font, &osd, 255U) != -1 ||
		gkd_ui_draw_osd(&surface, &config, NULL, &osd, 255U) != -1 ||
		gkd_ui_draw_osd(&surface, &config, &font, NULL, 255U) != -1 ||
		memcmp(pixels, before, count * sizeof(*pixels))) return 17;
	osd.icon = GKD_UI_OSD_ICON_COUNT;
	if (gkd_ui_draw_osd(&surface, &config, &font, &osd, 255U) != -1 ||
		memcmp(pixels, before, count * sizeof(*pixels))) return 5;
	osd.icon = 0U;
	if (gkd_ui_draw_osd(&surface, &config, &font, &osd, 256U) != -1 ||
		memcmp(pixels, before, count * sizeof(*pixels))) return 12;
	osd.level = 101;
	if (gkd_ui_draw_osd(&surface, &config, &font, &osd, 255U) != -1 ||
		memcmp(pixels, before, count * sizeof(*pixels))) return 18;
	osd.level = 50;
	osd.text = "THIS LABEL IS TOO LONG";
	if (gkd_ui_draw_osd(&surface, &config, &font, &osd, 255U) != -1 ||
		memcmp(pixels, before, count * sizeof(*pixels))) return 19;
	osd.text = "VOL 50%";
	memset(argb, 0xa5, sizeof(argb)); memcpy(argb_before, argb, sizeof(argb));
	if (gkd_ui_export_osd_argb(NULL, GKD_UI_OSD_WIDTH * GKD_UI_OSD_HEIGHT,
		&config, &font, &osd) != -1) return 30;
	if (gkd_ui_export_osd_argb(argb, GKD_UI_OSD_WIDTH * GKD_UI_OSD_HEIGHT - 1U,
		&config, &font, &osd) != -1 || memcmp(argb, argb_before, sizeof(argb))) return 31;
	if (gkd_ui_export_osd_argb(argb, GKD_UI_OSD_WIDTH * GKD_UI_OSD_HEIGHT,
		&config, &font, &osd) || argb[GKD_UI_OSD_WIDTH * GKD_UI_OSD_HEIGHT] != 0xa5a5a5a5U) return 32;
	if (gkd_ui_draw_osd(&surface, &config, &font, &osd, 128U)) return 20;
	memcpy(first, pixels, count * sizeof(*pixels));
	memcpy(pixels, before, count * sizeof(*pixels));
	if (gkd_ui_draw_osd(&surface, &config, &font, &osd, 128U) ||
		memcmp(pixels, first, count * sizeof(*pixels))) return 21;
	memcpy(pixels, before, count * sizeof(*pixels));
	if (gkd_ui_draw_osd(&surface, &config, &font, &osd, 255U)) return 6;
	for (unsigned y = 0; y < GKD_UI_HEIGHT; ++y)
		for (unsigned x = GKD_UI_WIDTH; x < surface.stride; ++x)
			if (pixels[y * surface.stride + x] != 0xa5a5U) return 7;
	if (gkd_ui_fade_opacity(1000U, 1000U, 0U, &opacity) || opacity != 255U ||
		gkd_ui_fade_opacity(1000U, 0U, 1000U, &opacity) || opacity ||
		gkd_ui_fade_opacity(20U, 160U, 160U, &opacity) || opacity != 31U ||
		gkd_ui_fade_opacity(500U, 500U, 1000U, &opacity) || opacity != 127U ||
		gkd_ui_fade_opacity(UINT_MAX, UINT_MAX, 1000U, &opacity) || opacity != 255U ||
		gkd_ui_fade_opacity(1U, 1U, 1001U, &opacity) != -1 || opacity != 255U ||
		gkd_ui_fade_opacity(1U, 1U, 1U, NULL) != -1)
		return 8;
#if GKD_APPLICATION_UI
	/* Battery interior follows charge, including empty and unknown values.
	 * Assert actual colored pixels, rather than a copy of the fill formula. */
	{
		static const int levels[] = {-1, 0, 2, 3, 10, 11, 25, 26, 50, 51, 75, 76, 90, 91, 100};
		static const unsigned columns[] = {0U, 0U, 0U, 1U, 1U, 2U, 2U, 3U, 3U, 4U, 4U, 5U, 5U, 6U, 6U};
		for (unsigned k = 0; k < sizeof(levels)/sizeof(levels[0]); ++k) {
			struct gkd_ui_osd battery = {"POWER", 2U, levels[k], 0};
			uint16_t ink = levels[k] >= 0 && levels[k] <= 10 ? config.button_b : config.normal;
			memset(pixels, 0, count * sizeof(*pixels));
			if (gkd_ui_draw_osd(&surface, &config, &font, &battery, 255U)) return 50;
			for (unsigned y = 212U; y <= 217U; ++y)
				for (unsigned x = 17U; x <= 22U; ++x)
					if ((pixels[y * surface.stride + x] == ink) !=
					    (x - 17U < columns[k])) return 51;
			if (pixels[210U * surface.stride + 15U] != ink) return 52;
		}
		struct gkd_ui_osd critical = {"POWER LOW", 2U, 0, 1};
		if (gkd_ui_draw_osd(&surface, &config, &font, &critical, 255U)) return 53;
	}
#endif
	gkd_ui_font_release(&font); free(first); free(before); free(pixels);
	return 0;
}
