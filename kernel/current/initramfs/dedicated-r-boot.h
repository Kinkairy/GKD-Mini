/* SPDX-License-Identifier: GPL-2.0 */
#include "gkd-boot-frame.h"

static int r_boot_fd = -1;
static void *r_boot_mapping;
static unsigned r_boot_stage;

static void r_boot_present(unsigned stage, int failed)
{
	volatile unsigned short *pixels = r_boot_mapping;
	if (!pixels) return;
	r_boot_stage = stage;
	gkd_boot_frame(pixels, FB_XRES, stage, failed);
	gkd_boot_frame(pixels + FB_XRES * FB_YRES, FB_XRES, stage, failed);
	__sync_synchronize();
	(void)fsync(r_boot_fd);
}

static void r_boot_close(void)
{
	if (r_boot_mapping) (void)munmap(r_boot_mapping, FB_MEMORY_BYTES);
	if (r_boot_fd >= 0) (void)close(r_boot_fd);
	r_boot_mapping = 0;
	r_boot_fd = -1;
}

static void r_boot_begin(void)
{
	struct fb_var_screeninfo pan;
	struct fb_fix_screeninfo fixed;
	void *mapping;
	r_boot_fd = open("/dev/fb0", O_RDWR | O_CLOEXEC, 0);
	if (r_boot_fd < 0) return;
	if (ioctl(r_boot_fd, FBIOGET_VSCREENINFO, &pan) < 0 ||
	    ioctl(r_boot_fd, FBIOGET_FSCREENINFO, &fixed) < 0 ||
	    pan.xres != FB_XRES || pan.yres != FB_YRES ||
	    pan.bits_per_pixel != 16U || fixed.line_length != FB_LINE_LENGTH ||
	    fixed.smem_len < FB_MEMORY_BYTES) { r_boot_close(); return; }
	mapping = round27_mmap(0, FB_MEMORY_BYTES, PROT_READ | PROT_WRITE,
			       MAP_SHARED, r_boot_fd, 0);
	if (mapping == MAP_FAILED) { r_boot_close(); return; }
	r_boot_mapping = mapping;
	r_boot_present(0, 0);
	pan.xoffset = 0; pan.yoffset = 0; pan.activate = FB_ACTIVATE_VBL;
	(void)ioctl(r_boot_fd, FBIOPAN_DISPLAY, &pan);
}

static int r_boot_trace(const char marker[4])
{
	/* Reuse real loader stages. No animation child, sleeps or competing owner. */
	if (marker[0] == 'P' && marker[1] >= '0' && marker[1] <= '7' &&
	    marker[2] == '0' && marker[3] == '0') {
		r_boot_present((unsigned)(marker[1] - '0'), 0);
		if (marker[1] == '7') r_boot_close(); /* before recovery UI exec */
	} else if (marker[0] == '8') {
		r_boot_present(r_boot_stage, 1);
	}
	return r80i_trace(marker);
}
