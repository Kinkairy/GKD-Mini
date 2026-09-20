// SPDX-License-Identifier: GPL-2.0
/* Compare production transparent export with independent RGB565 composition. */
#include "gkd-ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t blend565(uint16_t under, uint16_t over, unsigned alpha)
{
	unsigned ur = (under >> 11) & 31U, ug = (under >> 5) & 63U, ub = under & 31U;
	unsigned or_ = (over >> 11) & 31U, og = (over >> 5) & 63U, ob = over & 31U;
	unsigned r = (ur * (255U - alpha) + or_ * alpha + 127U) / 255U;
	unsigned g = (ug * (255U - alpha) + og * alpha + 127U) / 255U;
	unsigned b = (ub * (255U - alpha) + ob * alpha + 127U) / 255U;
	return (uint16_t)((r << 11) | (g << 5) | b);
}
static uint16_t argb_to_565(uint32_t argb)
{
	unsigned r = (argb >> 16) & 255U, g = (argb >> 8) & 255U, b = argb & 255U;
	return (uint16_t)(((r & 0xf8U) << 8) | ((g & 0xfcU) << 3) | ((b & 0xf8U) >> 3));
}
static void background(uint16_t *p, unsigned kind,
		       const struct gkd_ui_config *c, const struct gkd_ui_font *font)
{
	unsigned x, y;
	if (kind == 0U) { memset(p, 0, GKD_UI_WIDTH * GKD_UI_HEIGHT * sizeof(*p)); return; }
	if (kind == 1U) { for (x = 0; x < GKD_UI_WIDTH * GKD_UI_HEIGHT; ++x) p[x] = 0xffffU; return; }
	if (kind == 2U) {
		for (y = 0; y < GKD_UI_HEIGHT; ++y) for (x = 0; x < GKD_UI_WIDTH; ++x)
			p[y * GKD_UI_WIDTH + x] = ((x / 7U + y / 5U) & 1U) ? 0x07e0U :
				(uint16_t)(((x * 3U & 31U) << 11) | ((y * 5U & 63U) << 5) | ((x + y) & 31U));
		return;
	}
	{
		struct gkd_ui_surface s = {p, GKD_UI_WIDTH, GKD_UI_HEIGHT, GKD_UI_WIDTH};
		gkd_ui_render_recovery_menu(&s, c, font, 0U);
	}
}
int main(int argc, char **argv)
{
	static const char *bg_names[] = {"black", "white", "pattern", "approved-menu"};
	static const struct gkd_ui_osd states[] = {
		{"VOL 50%", 0U, 50, 0}, {"BRT 70%", 1U, 70, 0},
		{"BAT 76%", 2U, 76, 0}, {"SHOT SAVED", 3U, -1, 0},
		{"BAT 5%", 2U, 5, 1}, {"192.168.137.2", 4U, -1, 0},
		{"CHARGING", 5U, -1, 0}, {"STORAGE", 6U, -1, 0}, {"DEBUG", 7U, -1, 0},
#if defined(GKD_APPLICATION_UI) && GKD_APPLICATION_UI
		{"SAVED", GKD_UI_OSD_ICON_SAVE, -1, 0},
		{"SUCCESS", GKD_UI_OSD_ICON_SUCCESS, -1, 0},
		{"FAILED", GKD_UI_OSD_ICON_FAILURE, -1, 1},
#endif
	};
	static const char *state_names[] = {"volume", "brightness", "battery", "screenshot",
		"low-battery", "network", "charge", "storage", "debug"
#if defined(GKD_APPLICATION_UI) && GKD_APPLICATION_UI
        , "save", "success", "failure"
#endif
    };
	static const unsigned opacities[] = {0U, 127U, 255U};
	struct gkd_ui_config c; struct gkd_ui_font font;
	uint16_t base[GKD_UI_WIDTH * GKD_UI_HEIGHT], direct[GKD_UI_WIDTH * GKD_UI_HEIGHT];
	uint16_t plane[GKD_UI_WIDTH * GKD_UI_HEIGHT];
	uint32_t tile[GKD_UI_OSD_WIDTH * GKD_UI_OSD_HEIGHT];
	struct gkd_ui_surface s;
	unsigned bg, st, op, x, y, n, out, menu_outside = 0, total = 0, nonzero = 0;
	if (argc != 2) return 2;
	gkd_ui_config_defaults(&c);
	if (gkd_ui_font_load(&font, argv[1])) return 3;
	s = (struct gkd_ui_surface){direct, GKD_UI_WIDTH, GKD_UI_HEIGHT, GKD_UI_WIDTH};
	for (bg = 0; bg < 4U; ++bg) for (st = 0; st < sizeof(states)/sizeof(states[0]); ++st) {
		if (gkd_ui_export_osd_argb(tile, GKD_UI_OSD_WIDTH * GKD_UI_OSD_HEIGHT, &c, &font, &states[st])) return 5;
		for (op = 0; op < 3U; ++op) {
			background(base, bg, &c, &font);
			memcpy(direct, base, sizeof(base)); memcpy(plane, base, sizeof(base));
			if (gkd_ui_draw_osd(&s, &c, &font, &states[st], opacities[op])) return 6;
			for (y = 0; y < GKD_UI_OSD_HEIGHT; ++y) for (x = 0; x < GKD_UI_OSD_WIDTH; ++x) {
				size_t i = (GKD_UI_OSD_Y + y) * GKD_UI_WIDTH + GKD_UI_OSD_X + x;
				uint32_t a = tile[y * GKD_UI_OSD_WIDTH + x];
				uint16_t composed = blend565(base[i], argb_to_565(a), a >> 24);
				plane[i] = blend565(base[i], composed, opacities[op]);
			}
			n = out = 0;
			for (y = 0; y < GKD_UI_HEIGHT; ++y) for (x = 0; x < GKD_UI_WIDTH; ++x) {
				size_t i = y * GKD_UI_WIDTH + x;
				if (direct[i] != plane[i]) {
					if (!n) printf("first bg=%s state=%s opacity=%u xy=%u,%u direct=%04x plane=%04x ",
						bg_names[bg], state_names[st], opacities[op], x, y, direct[i], plane[i]);
					++n;
					if (x < GKD_UI_OSD_X || x >= GKD_UI_OSD_X + GKD_UI_OSD_WIDTH ||
					    y < GKD_UI_OSD_Y || y >= GKD_UI_OSD_Y + GKD_UI_OSD_HEIGHT) ++out;
				}
			}
			if (n) printf("\n");
			printf("case bg=%s state=%s opacity=%u diff=%u outside=%u\n",
				bg_names[bg], state_names[st], opacities[op], n, out);
			++total; if (n) ++nonzero; menu_outside += out;
		}
	}
	{ uint32_t invalid_tile[GKD_UI_OSD_WIDTH * GKD_UI_OSD_HEIGHT + 1U], before[GKD_UI_OSD_WIDTH * GKD_UI_OSD_HEIGHT + 1U];
	  memset(invalid_tile, 0xa5, sizeof(invalid_tile)); memcpy(before, invalid_tile, sizeof(invalid_tile)); c.panel_alpha = 256U;
	  if (gkd_ui_export_osd_argb(invalid_tile, GKD_UI_OSD_WIDTH * GKD_UI_OSD_HEIGHT, &c, &font, &states[0]) != -1 || memcmp(invalid_tile,before,sizeof(invalid_tile))) return 8;
	  c.panel_alpha=235U; font.charsize=1U;
	  if (gkd_ui_export_osd_argb(invalid_tile, GKD_UI_OSD_WIDTH * GKD_UI_OSD_HEIGHT, &c, &font, &states[0]) != -1 || memcmp(invalid_tile,before,sizeof(invalid_tile))) return 9;
	  font.charsize=16U; { struct gkd_ui_osd bad=states[0]; bad.icon=GKD_UI_OSD_ICON_COUNT;
	  if (gkd_ui_export_osd_argb(invalid_tile, GKD_UI_OSD_WIDTH * GKD_UI_OSD_HEIGHT, &c, &font, &bad) != -1 || memcmp(invalid_tile,before,sizeof(invalid_tile))) return 10; } }
	printf("summary cases=%u nonzero=%u zero=%u menu_outside=%u\n",
		total, nonzero, total - nonzero, menu_outside);
	gkd_ui_font_release(&font);
	return nonzero || menu_outside ? 1 : 0;
}
