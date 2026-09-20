// SPDX-License-Identifier: GPL-2.0
#define _POSIX_C_SOURCE 200809L
#include "gkd-ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static long long measure(struct gkd_ui_surface *surface,
			 const struct gkd_ui_config *config,
			 const struct gkd_ui_font *font,
			 const struct gkd_ui_osd *osd, const uint16_t *clean,
			 unsigned opacity, unsigned iterations)
{
	struct timespec start, end;
	unsigned i, y;
	if (clock_gettime(CLOCK_MONOTONIC, &start)) return -1;
	for (i = 0; i < iterations; ++i) {
		for (y = 0; y < GKD_UI_OSD_HEIGHT; ++y)
			memcpy(surface->pixels + (GKD_UI_OSD_Y + y) * surface->stride + GKD_UI_OSD_X,
			       clean + y * GKD_UI_OSD_WIDTH, GKD_UI_OSD_WIDTH * sizeof(*clean));
		if (gkd_ui_draw_osd(surface, config, font, osd, opacity)) return -1;
	}
	if (clock_gettime(CLOCK_MONOTONIC, &end)) return -1;
	return (long long)(end.tv_sec - start.tv_sec) * 1000000000LL +
		(long long)(end.tv_nsec - start.tv_nsec);
}

int main(int argc, char **argv)
{
	struct gkd_ui_config config;
	struct gkd_ui_font font;
	struct gkd_ui_surface surface;
	struct gkd_ui_menu_item item = {"EXPORT SYSTEM CARD", 0U};
	struct gkd_ui_menu menu = {"RECOVERY", &item, 1U, 0U,
		GKD_UI_ACTION_ENABLED, GKD_UI_ACTION_DISABLED, "SURE", "CANCEL"};
	struct gkd_ui_osd osd = {"VOL 50%", 0U, 50, 0};
	uint16_t pixels[GKD_UI_WIDTH * GKD_UI_HEIGHT];
	uint16_t clean[GKD_UI_OSD_WIDTH * GKD_UI_OSD_HEIGHT];
	unsigned y, iterations = 1000U;
	long long zero, middle, opaque;
	if (argc != 2) return 2;
	gkd_ui_config_defaults(&config);
	if (gkd_ui_font_load(&font, argv[1])) return 2;
	surface = (struct gkd_ui_surface){pixels, GKD_UI_WIDTH, GKD_UI_HEIGHT,
		GKD_UI_WIDTH};
	if (gkd_ui_render_menu(&surface, &config, &font, &menu)) return 2;
	for (y = 0; y < GKD_UI_OSD_HEIGHT; ++y)
		memcpy(clean + y * GKD_UI_OSD_WIDTH,
		       pixels + (GKD_UI_OSD_Y + y) * surface.stride + GKD_UI_OSD_X,
		       GKD_UI_OSD_WIDTH * sizeof(*clean));
	zero = measure(&surface, &config, &font, &osd, clean, 0U, iterations);
	middle = measure(&surface, &config, &font, &osd, clean, 128U, iterations);
	opaque = measure(&surface, &config, &font, &osd, clean, 255U, iterations);
	if (zero < 0 || middle < 0 || opaque < 0) return 2;
	printf("GKD_UI_OSD_HOST_BENCH iterations=%u tile_restore=1 avg_ns_0=%lld avg_ns_128=%lld avg_ns_255=%lld delta_128_minus_255_ns=%lld\n",
	       iterations, zero / (long long)iterations, middle / (long long)iterations,
	       opaque / (long long)iterations, (middle - opaque) / (long long)iterations);
	gkd_ui_font_release(&font);
	return 0;
}
