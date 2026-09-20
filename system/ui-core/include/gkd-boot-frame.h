/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_BOOT_FRAME_H
#define GKD_BOOT_FRAME_H

/* Shared allocation-free RGB565 compositor; no framebuffer/input ownership.
 * Uses the selected baked artwork and the common palette compiled by build.
 * R's bootstrap calls this before its libc-based menu has been unpacked.
 */
#include "gkd-boot-art.generated.h"

static void gkd_boot_frame(volatile unsigned short *pixels, unsigned stride,
			   unsigned stage, int failed)
{
	unsigned x, y, i;
	if (stage > 7U) stage = 7U;
	for (y = 0; y < 240U; ++y)
		for (x = 0; x < 320U; ++x) {
			i = y * 320U + x;
			pixels[y * stride + x] = gkd_boot_palette[
				(gkd_boot_pixels[i / 2U] >> ((i & 1U) * 4U)) & 15U];
		}
	/* Replace the preview's baked sample bar with real stage progress. */
	for (y = 153; y < 162; ++y)
		for (x = 111; x < 209; ++x)
			pixels[y * stride + x] = gkd_boot_palette[0];
	for (y = 154; y < 161; ++y)
		for (x = 111; x < 208; ++x) {
			unsigned color = 0;
			if (y == 154U || y == 160U || x == 111U || x == 207U)
				color = failed ? 15U : 2U;
			else if (y >= 156U && y <= 158U && x >= 113U &&
				 x < 113U + (93U * stage) / 7U)
				color = failed ? 15U : 1U;
			pixels[y * stride + x] = gkd_boot_palette[color];
		}
}
#endif
