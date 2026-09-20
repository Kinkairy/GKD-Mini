// SPDX-License-Identifier: GPL-2.0
#define _POSIX_C_SOURCE 200809L
#include "gkd-ui.h"
#include "gkd-ui-pixels.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PSF2_MAGIC 0x864ab572U
#define PSF2_HAS_UNICODE_TABLE 1U
#define MAX_FONT_BYTES (8U * 1024U * 1024U)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#ifndef GKD_DEDICATED_RECOVERY
#define GKD_DEDICATED_RECOVERY 0
#endif
#ifndef GKD_APPLICATION_UI
#define GKD_APPLICATION_UI 0
#endif
#define GKD_SHARED_CRT (GKD_DEDICATED_RECOVERY || GKD_APPLICATION_UI)

static uint32_t le32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rgb565(unsigned r, unsigned g, unsigned b)
{
	return (uint16_t)(((r & 0xf8U) << 8) | ((g & 0xfcU) << 3) |
			  ((b & 0xf8U) >> 3));
}

static int parse_hex_color(const char *value, uint16_t *out)
{
	char *end = NULL;
	unsigned long color;
	if (!value || value[0] != '#' || strlen(value) != 7U) return -1;
	errno = 0;
	color = strtoul(value + 1, &end, 16);
	if (errno || !end || *end || color > 0xffffffUL) return -1;
	*out = rgb565((unsigned)(color >> 16), (unsigned)(color >> 8),
		      (unsigned)color);
	return 0;
}

static int parse_uint(const char *value, unsigned min, unsigned max,
		      unsigned *out)
{
	char *end = NULL;
	unsigned long parsed;
	errno = 0;
	parsed = strtoul(value, &end, 10);
	if (errno || !value[0] || !end || *end || parsed < min || parsed > max)
		return -1;
	*out = (unsigned)parsed;
	return 0;
}

static int copy_text(char *out, size_t size, const char *value)
{
	size_t length = strlen(value);
	if (!length || length >= size || strchr(value, '\n') || strchr(value, '\r'))
		return -1;
	memcpy(out, value, length + 1U);
	return 0;
}

void gkd_ui_config_defaults(struct gkd_ui_config *c)
{
	static const char *labels[GKD_UI_MENU_ACTIONS] = {
		"EXPORT SYSTEM CARD", "SYSTEM UPDATE", "RESTORE LAST SYSTEM",
		"REBOOT", "SHUTDOWN"
	};
	unsigned i;
	memset(c, 0, sizeof(*c));
	c->normal = rgb565(0x76, 0xff, 0x89);
	c->dark = rgb565(0x00, 0x05, 0x02);
	c->highlight = rgb565(0x36, 0xad, 0x4e);
	c->white = rgb565(0xbe, 0xff, 0xb9);
	c->button_b = rgb565(0xff, 0x5a, 0x67);
	c->panel_alpha = 235; c->osd_alpha = 220;
	c->menu = (struct gkd_ui_rect){0, 0, 320, 240};
	c->menu_row_x = 40; c->menu_row_y = 48; c->menu_row_width = 240;
	c->menu_row_height = 27; c->menu_row_step = 31; c->menu_icon_size = 14;
	c->loading_icon_size = 14; c->loading_px = 12;
	c->loading_interval_ms = 120;
	c->title_px = 18; c->body_px = 16; c->action_px = 12;
	c->action_lane_x = 164; c->action_lane_y = 206;
	c->action_lane_width = 148; c->action_button_width = 71;
	c->action_button_height = 19; c->action_button_gap = 6;
	c->action_key_size = 11;
#if GKD_SHARED_CRT
	c->accent = rgb565(0xd6, 0xb8, 0x5a);
	c->action_disabled_alpha = 72;
	c->menu_content_shift = 12; c->menu_text_shift = 1;
	c->accent_x = 16; c->accent_width = 132; c->accent_height = 16;
	c->accent_period = 20; c->accent_stroke = 11;
	c->accent_alpha = 156; c->accent_dim_alpha = 124; c->accent_rail_alpha = 65;
#endif
	(void)copy_text(c->font_path, sizeof(c->font_path),
		"/etc/gkd-mini/fonts/fallback.psf");
	(void)copy_text(c->title, sizeof(c->title), "RECOVERY");
	for (i = 0; i < GKD_UI_MENU_ACTIONS; ++i)
		(void)copy_text(c->labels[i], sizeof(c->labels[i]), labels[i]);
	(void)copy_text(c->action_yes, sizeof(c->action_yes), "SURE");
	(void)copy_text(c->action_no, sizeof(c->action_no), "CANCEL");
	(void)copy_text(c->loading_label, sizeof(c->loading_label), "LOADING");
}

static int assign_config(struct gkd_ui_config *c, const char *key,
			 const char *value)
{
#define U(name, min, max) if (!strcmp(key, #name)) return parse_uint(value, min, max, &c->name)
#define C(name) if (!strcmp(key, #name)) return parse_hex_color(value, &c->name)
	C(normal); C(dark); C(highlight); C(white); C(button_b);
	U(panel_alpha, 0, 255); U(osd_alpha, 0, 255);
	U(menu_row_x, 0, 319); U(menu_row_y, 0, 239);
	U(menu_row_width, 80, 320); U(menu_row_height, 18, 48);
	U(menu_row_step, 18, 56); U(menu_icon_size, 8, 24);
	U(loading_icon_size, 8, 24); U(loading_px, 8, 18);
	U(loading_interval_ms, 50, 1000);
	U(title_px, 8, 24); U(body_px, 8, 24); U(action_px, 8, 18);
	U(action_lane_x, 0, 319); U(action_lane_y, 0, 239);
	U(action_lane_width, 80, 320); U(action_button_width, 40, 148);
	U(action_button_height, 15, 32); U(action_button_gap, 0, 24);
	U(action_key_size, 9, 16);
#if GKD_SHARED_CRT
	C(accent);
	U(action_disabled_alpha, 0, 255);
	U(menu_content_shift, 0, 30); U(menu_text_shift, 0, 4);
	U(accent_x, 0, 319); U(accent_width, 12, 160); U(accent_height, 6, 19);
	U(accent_period, 2, 40); U(accent_stroke, 1, 39);
	U(accent_alpha, 0, 255); U(accent_dim_alpha, 0, 255);
	U(accent_rail_alpha, 0, 255);
#endif
#undef U
#undef C
	if (!strcmp(key, "font_path"))
		return copy_text(c->font_path, sizeof(c->font_path), value);
	if (!strcmp(key, "recovery_title"))
		return copy_text(c->title, sizeof(c->title), value);
	if (!strcmp(key, "recovery_export_label"))
		return copy_text(c->labels[0], sizeof(c->labels[0]), value);
	if (!strcmp(key, "recovery_update_label"))
		return copy_text(c->labels[1], sizeof(c->labels[1]), value);
	if (!strcmp(key, "recovery_restore_label"))
		return copy_text(c->labels[2], sizeof(c->labels[2]), value);
	if (!strcmp(key, "recovery_reboot_label"))
		return copy_text(c->labels[3], sizeof(c->labels[3]), value);
	if (!strcmp(key, "recovery_shutdown_label"))
		return copy_text(c->labels[4], sizeof(c->labels[4]), value);
	if (!strcmp(key, "action_yes_label"))
		return copy_text(c->action_yes, sizeof(c->action_yes), value);
	if (!strcmp(key, "action_no_label"))
		return copy_text(c->action_no, sizeof(c->action_no), value);
	if (!strcmp(key, "loading_label"))
		return copy_text(c->loading_label, sizeof(c->loading_label), value);
	return 1;
}

static int validate_config(const struct gkd_ui_config *c)
{
	unsigned buttons = c->action_button_width * 2U + c->action_button_gap;
	if (c->menu_row_x + c->menu_row_width > GKD_UI_WIDTH ||
	    c->menu_row_y + (GKD_UI_MENU_ITEMS - 1U) * c->menu_row_step +
		c->menu_row_height > GKD_UI_HEIGHT ||
	    c->action_lane_x + c->action_lane_width > GKD_UI_WIDTH ||
	    c->action_lane_y + c->action_button_height > GKD_UI_HEIGHT ||
	    buttons > c->action_lane_width || c->action_px != 12U)
		return -1;
#if GKD_SHARED_CRT
	if (c->accent_x + c->accent_width > c->action_lane_x ||
	    c->accent_height > c->action_button_height ||
	    c->accent_stroke >= c->accent_period)
		return -1;
#endif
	return 0;
}

int gkd_ui_config_load(struct gkd_ui_config *c, const char *path, int sparse)
{
	struct gkd_ui_config candidate;
	char line[512];
	FILE *file;
	unsigned seen = 0;
	if (!c || !path) return -1;
	candidate = *c;
	file = fopen(path, "r");
	if (!file) return -1;
	while (fgets(line, sizeof(line), file)) {
		char *key, *value, *end;
		int result;
		if (!strchr(line, '\n') && !feof(file)) { fclose(file); return -1; }
		line[strcspn(line, "\r\n")] = 0;
		key = line;
		while (isspace((unsigned char)*key)) ++key;
		if (!*key || *key == '#') continue;
		value = strchr(key, '=');
		if (!value) { fclose(file); return -1; }
		*value++ = 0;
		end = key + strlen(key);
		while (end > key && isspace((unsigned char)end[-1])) *--end = 0;
		while (isspace((unsigned char)*value)) ++value;
		end = value + strlen(value);
		while (end > value && isspace((unsigned char)end[-1])) *--end = 0;
		result = assign_config(&candidate, key, value);
		if (result < 0 || (!sparse && result > 0)) { fclose(file); return -1; }
		if (!result) ++seen;
	}
	if (ferror(file) || fclose(file) || (!sparse && !seen) ||
	    validate_config(&candidate))
		return -1;
	*c = candidate;
	return 0;
}

int gkd_ui_font_load(struct gkd_ui_font *font, const char *path)
{
	struct stat st;
	unsigned char header[32];
	int fd;
	ssize_t got;
	size_t total;
	memset(font, 0, sizeof(*font));
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_nlink != 1 ||
	    st.st_size < 32 || st.st_size > (off_t)MAX_FONT_BYTES) goto fail;
	got = read(fd, header, sizeof(header));
	if (got != (ssize_t)sizeof(header) || le32(header) != PSF2_MAGIC) goto fail;
	font->headersize = le32(header + 8); font->flags = le32(header + 12);
	font->length = le32(header + 16); font->charsize = le32(header + 20);
	font->height = le32(header + 24); font->width = le32(header + 28);
	if (font->headersize < 32 || !font->length || font->length > 65536U ||
	    !font->height || font->height > 32U || !font->width || font->width > 32U ||
	    font->charsize < font->height * ((font->width + 7U) / 8U) ||
	    (uint64_t)font->headersize + (uint64_t)font->length * font->charsize >
		(uint64_t)st.st_size) goto fail;
	font->size = (size_t)st.st_size;
	font->data = malloc(font->size);
	if (!font->data || lseek(fd, 0, SEEK_SET) != 0) goto fail;
	for (total = 0; total < font->size; total += (size_t)got) {
		got = read(fd, font->data + total, font->size - total);
		if (got < 0 && errno == EINTR) { got = 0; continue; }
		if (got <= 0) goto fail;
	}
	if (close(fd)) { fd = -1; goto fail; }
	return 0;
fail:
	if (fd >= 0) (void)close(fd);
	free(font->data); memset(font, 0, sizeof(*font));
	return -1;
}

void gkd_ui_font_release(struct gkd_ui_font *font)
{
	if (font) { free(font->data); memset(font, 0, sizeof(*font)); }
}

static uint32_t utf8_next(const unsigned char **cursor)
{
	const unsigned char *p = *cursor;
	uint32_t value;
	if (*p < 0x80U) { *cursor = p + 1; return *p; }
	if ((*p & 0xe0U) == 0xc0U && p[1] && (p[1] & 0xc0U) == 0x80U) {
		value = ((uint32_t)(p[0] & 0x1fU) << 6) | (p[1] & 0x3fU);
		*cursor = p + 2; return value >= 0x80U ? value : '?';
	}
	if ((*p & 0xf0U) == 0xe0U && p[1] && p[2] &&
	    (p[1] & 0xc0U) == 0x80U &&
	    (p[2] & 0xc0U) == 0x80U) {
		value = ((uint32_t)(p[0] & 0x0fU) << 12) |
			((uint32_t)(p[1] & 0x3fU) << 6) | (p[2] & 0x3fU);
		*cursor = p + 3; return value >= 0x800U ? value : '?';
	}
	if ((*p & 0xf8U) == 0xf0U && p[1] && p[2] && p[3] &&
	    (p[1] & 0xc0U) == 0x80U &&
	    (p[2] & 0xc0U) == 0x80U && (p[3] & 0xc0U) == 0x80U) {
		value = ((uint32_t)(p[0] & 7U) << 18) |
			((uint32_t)(p[1] & 0x3fU) << 12) |
			((uint32_t)(p[2] & 0x3fU) << 6) | (p[3] & 0x3fU);
		*cursor = p + 4; return value >= 0x10000U && value <= 0x10ffffU ? value : '?';
	}
	*cursor = p + 1; return '?';
}

static unsigned font_index(const struct gkd_ui_font *font, uint32_t codepoint)
{
	const unsigned char *p, *end;
	unsigned glyph = 0;
	if (!(font->flags & PSF2_HAS_UNICODE_TABLE))
		return codepoint < font->length ? (unsigned)codepoint : (unsigned)'?';
	p = font->data + font->headersize + (size_t)font->length * font->charsize;
	end = font->data + font->size;
	while (p < end && glyph < font->length) {
		if (*p == 0xffU) { ++glyph; ++p; continue; }
		if (*p == 0xfeU) { ++p; continue; }
		if (utf8_next(&p) == codepoint) return glyph;
	}
	return UINT32_MAX;
}

enum draw_target_format { DRAW_RGB565, DRAW_ARGB8888 };
struct draw_target {
	uint16_t *pixels;
	unsigned width, height, stride;
	uint32_t *argb;
	enum draw_target_format format;
};
static struct draw_target rgb_target(struct gkd_ui_surface *surface)
{
	return (struct draw_target){surface->pixels, surface->width, surface->height,
		surface->stride, NULL, DRAW_RGB565};
}
static uint32_t argb_from_565(uint16_t color, unsigned alpha)
{
	unsigned r = (color >> 11) & 31U, g = (color >> 5) & 63U, b = color & 31U;
	return (alpha << 24) | ((r << 3 | r >> 2) << 16) |
		((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}

static void pixel(struct draw_target *s, int x, int y, uint16_t color,
		  unsigned alpha)
{
	size_t index;
	if (x < 0 || y < 0 || (unsigned)x >= s->width || (unsigned)y >= s->height)
		return;
	index = (size_t)(unsigned)y * s->stride + (unsigned)x;
	if (s->format == DRAW_RGB565) {
		uint16_t *p = &s->pixels[index];
		*p = alpha == 255U ? color : gkd_ui_blend565_value(*p, color, alpha);
	} else {
		uint32_t *p = &s->argb[index], source = argb_from_565(color, alpha);
		unsigned da = *p >> 24, oa, inv, r, g, b;
		if (!alpha) return;
		if (alpha == 255U || !da) { *p = source; return; }
		inv = 255U - alpha; oa = alpha + (da * inv + 127U) / 255U;
		if ((*p & 0x00ffffffU) == (source & 0x00ffffffU)) {
			*p = (source & 0x00ffffffU) | (oa << 24); return;
		}
		r = ((((source >> 16) & 255U) * alpha + (((*p >> 16 & 255U) * da * inv + 127U) / 255U) + oa / 2U) / oa);
		g = ((((source >> 8) & 255U) * alpha + (((*p >> 8 & 255U) * da * inv + 127U) / 255U) + oa / 2U) / oa);
		b = (((source & 255U) * alpha + (((*p & 255U) * da * inv + 127U) / 255U) + oa / 2U) / oa);
		if (r > 255U) r = 255U;
		if (g > 255U) g = 255U;
		if (b > 255U) b = 255U;
		*p = (oa << 24) | (r << 16) | (g << 8) | b;
	}
}

static void fill(struct draw_target *s, int x, int y, unsigned w, unsigned h,
		 uint16_t color, unsigned alpha)
{
	unsigned xx, yy;
	for (yy = 0; yy < h; ++yy)
		for (xx = 0; xx < w; ++xx) pixel(s, x + (int)xx, y + (int)yy, color, alpha);
}

static void rect(struct draw_target *s, int x, int y, unsigned w, unsigned h,
		 uint16_t fill_color, unsigned alpha, uint16_t outline)
{
	if (w < 3U || h < 3U) return;
	fill(s, x + 1, y, w - 2U, h, fill_color, alpha);
	fill(s, x, y + 1, w, h - 2U, fill_color, alpha);
	fill(s, x + 1, y, w - 2U, 1, outline, 255);
	fill(s, x + 1, y + (int)h - 1, w - 2U, 1, outline, 255);
	fill(s, x, y + 1, 1, h - 2U, outline, 255);
	fill(s, x + (int)w - 1, y + 1, 1, h - 2U, outline, 255);
}

static const struct gkd_ui_font *codepoint_font(const struct gkd_ui_font *font,
                                               uint32_t cp, unsigned px)
{
	if (cp < 256U && font->latin) return font->latin;
	return cp >= 256U && px <= 12U && font->compact ? font->compact : font;
}

/* A CJK face explicitly borrows Latin; retain its native bitmap dimensions.
 * Latin continues to use the original slot size and stroke-preserving path. */
static unsigned glyph_height(const struct gkd_ui_font *font, unsigned px)
{
	return font->latin ? font->height : px;
}

static unsigned glyph_width(const struct gkd_ui_font *font, unsigned px)
{
#if GKD_SHARED_CRT
	/* A 12-pixel action cell can hold the native 8x16 font's ten ink rows.
	 * Keep its horizontal strokes instead of decimating an 8x16 bitmap. */
	if (px == 12U && font->width == 8U && font->height == 16U) return 8U;
#endif
	px = glyph_height(font, px);
	return (font->width * px + font->height / 2U) / font->height;
}

static void draw_glyph(struct draw_target *s, const struct gkd_ui_font *font,
		       uint32_t codepoint, int x, int y, unsigned px, uint16_t color)
{
	font = codepoint_font(font, codepoint, px);
	unsigned index = font_index(font, codepoint);
	if (index >= font->length) {
		/* Explicit Latin reuse also supplies the visible unknown-character glyph. */
		font = codepoint_font(font, '?', px);
		index = font_index(font, '?');
		if (index >= font->length) return;
	}
	y += ((int)px - (int)glyph_height(font, px)) / 2;
	px = glyph_height(font, px);
	unsigned dw = glyph_width(font, px), dx, dy;
	unsigned row_bytes = (font->width + 7U) / 8U;
	const unsigned char *glyph = font->data + font->headersize +
		(size_t)index * font->charsize;
#if GKD_SHARED_CRT
	if (px == 12U && font->width == 8U && font->height == 16U) {
		unsigned first = 0, last = 16;
		while (first < last && !glyph[first]) ++first;
		while (last > first && !glyph[last - 1U]) --last;
		if (last - first <= px) {
			unsigned top = (px - (last - first)) / 2U;
			for (dy = first; dy < last; ++dy)
				for (dx = 0; dx < 8U; ++dx)
					if (glyph[dy] & (0x80U >> dx))
						pixel(s, x + (int)dx,
						      y + (int)(top + dy - first), color, 255);
			return;
		}
	}
#endif
	for (dy = 0; dy < px; ++dy) {
		unsigned sy = dy * font->height / px;
		for (dx = 0; dx < dw; ++dx) {
			unsigned sx = dx * font->width / dw;
			if (glyph[sy * row_bytes + sx / 8U] & (0x80U >> (sx & 7U)))
				pixel(s, x + (int)dx, y + (int)dy, color, 255);
		}
	}
}

static unsigned text_width(const struct gkd_ui_font *font, const char *text,
			   unsigned px)
{
	const unsigned char *p = (const unsigned char *)text;
	unsigned width = 0;
	while (*p) {
		uint32_t cp = utf8_next(&p);
		width += glyph_width(codepoint_font(font, cp, px), px) + 1U;
	}
	return width ? width - 1U : 0U;
}

static void draw_text(struct draw_target *s, const struct gkd_ui_font *font,
		      const char *text, int x, int y, unsigned px, uint16_t color)
{
	const unsigned char *p = (const unsigned char *)text;
	while (*p) {
		uint32_t codepoint = utf8_next(&p);
		draw_glyph(s, font, codepoint, x, y, px, color);
		x += (int)glyph_width(codepoint_font(font, codepoint, px), px) + 1;
	}
}

static void draw_icon(struct draw_target *s, unsigned icon, int x, int y,
		      unsigned size, uint16_t color)
{
#if GKD_SHARED_CRT
	/* Native 14x14 masters: card export, update, restore, reboot, power. */
	static const char master[5][14][15] = {
		{
			"..............", "..######......", ".#......#.....",
			".#.####.#.....", ".#......#.....", ".#.........#..",
			".#........##..", ".#....#######.", ".#........##..",
			".#.........#..", ".#......#.....", ".#.####.#.....",
			"..######......", "..............",
		}, {
			"..............", "......##......", "......##......",
			"......##......", "......##......", "...#..##..#...",
			"....#.##.#....", ".....####.....", "......##......",
			"..............", "..#........#..", "..#........#..",
			"...########...", "..............",
		}, {
			"..............", "..............", ".....#........",
			"....##........", "...#########..", "....##.....#..",
			".....#.....#..", "...........#..", "...........#..",
			"...........#..", "...#########..", "..............",
			"..............", "..............",
		}, {
			"..............", "..............", "....######....",
			"...#......##..", "..#........#..", "..#......####.",
			"..#.......##..", "..#.......#...", "..#...........",
			"..#........#..", "...#......#...", "....######....",
			"..............", "..............",
		}, {
			"..............", "......##......", "......##......",
			"...#..##..#...", "..#...##...#..", ".#....##....#.",
			".#..........#.", ".#..........#.", ".#..........#.",
			"..#........#..", "...#......#...", "....######....",
			"..............", "..............",
		},
	};
	unsigned xx, yy;
	if (icon >= 5U || !size) return;
	for (yy = 0; yy < size; ++yy)
		for (xx = 0; xx < size; ++xx)
			if (master[icon][yy * 14U / size][xx * 14U / size] == '#')
				pixel(s, x + (int)xx, y + (int)yy, color, 255);
#else
	unsigned i;
	if (icon == 0U) {
		rect(s, x + 1, y + 3, size - 2U, size - 5U, s->pixels[0], 0, color);
		fill(s, x + 3, y + 6, size - 6U, 1, color, 255);
		fill(s, x + 4, y + 9, 3, 2, color, 255);
	} else if (icon == 1U) {
		for (i = 2; i + 2 < size; ++i) {
			pixel(s, x + (int)i, y + 1, color, 255);
			pixel(s, x + (int)(size - 1U - i), y + (int)size - 2, color, 255);
		}
		fill(s, x + (int)size / 2 - 1, y + 3, 2, size - 6U, color, 255);
	} else if (icon == 2U || icon == 3U) {
		for (i = 1; i + 1 < size; ++i) {
			pixel(s, x + (int)i, y + 1, color, 255);
			pixel(s, x + (int)i, y + (int)size - 2, color, 255);
		}
		fill(s, x + 1, y + 2, 1, size - 4U, color, 255);
		fill(s, x + (int)size - 2, y + 2, 1, size - 4U, color, 255);
		if (icon == 3U) {
			fill(s, x + (int)size - 5, y, 5, 3, color, 255);
		}
	} else {
		fill(s, x + (int)size / 2 - 1, y, 2, size / 2U + 1U, color, 255);
		for (i = 2; i + 2 < size; ++i) {
			pixel(s, x + (int)i, y + (int)size - 3, color, 255);
		}
	}
#endif
}

static void draw_keycap(struct draw_target *s,
			const struct gkd_ui_config *c,
			const struct gkd_ui_font *font, char letter,
			int x, int y, int red)
{
	char text[2] = {letter, 0};
	uint16_t key = red ? c->button_b : c->highlight;
	rect(s, x, y, c->action_key_size, c->action_key_size,
	     red ? key : c->dark, 255, key);
	if(c->input_style==2U){
        /* Draw actual PS cross/circle; no font-dependent X/O substitution. */
        int cx=x+(int)c->action_key_size/2,cy=y+(int)c->action_key_size/2;
        uint16_t ink=red?c->dark:c->white;
        if(!red)for(int d=-3;d<=3;d++){pixel(s,cx+d,cy+d,ink,255);pixel(s,cx+d,cy-d,ink,255);}
        else for(int d=-2;d<=2;d++){pixel(s,cx+d,cy-3,ink,255);pixel(s,cx+d,cy+3,ink,255);pixel(s,cx-3,cy+d,ink,255);pixel(s,cx+3,cy+d,ink,255);}
    }else draw_text(s, font, text, x + 3, y + 1, 8, red ? c->dark : c->white);
}

static void draw_action(struct draw_target *s,
			const struct gkd_ui_config *c,
			const struct gkd_ui_font *font, int x,
			char key, const char *label, int red)
{
	int key_y = (int)c->action_lane_y +
		((int)c->action_button_height - (int)c->action_key_size) / 2;
	int text_y = (int)c->action_lane_y +
		((int)c->action_button_height - (int)c->action_px) / 2;
#if GKD_SHARED_CRT
#if GKD_APPLICATION_UI
	/* One accepted button pair across every A surface, font and state. */
	label = red ? "NO" : "YES";
#else
	unsigned yes_width = text_width(font, c->action_yes, c->action_px);
	unsigned no_width = text_width(font, c->action_no, c->action_px);

	/* Use the owner's approved short labels when either full label cannot fit. */
	if (yes_width + c->action_key_size + 22U > c->action_button_width ||
	    no_width + c->action_key_size + 22U > c->action_button_width)
		label = red ? "NO" : "YES";
#endif
	unsigned label_width, content_width;
	int content_x;
	label_width = text_width(font, label, c->action_px);
	content_width = c->action_key_size + 7U + label_width;
	content_x = x + ((int)c->action_button_width - (int)content_width) / 2;
	rect(s, x, (int)c->action_lane_y, c->action_button_width,
	     c->action_button_height, c->dark, c->osd_alpha, c->highlight);
	draw_keycap(s, c, font, key, content_x, key_y, red);
	draw_text(s, font, label, content_x + (int)c->action_key_size + 7,
		  text_y, c->action_px, c->normal);
#else
	rect(s, x, (int)c->action_lane_y, c->action_button_width,
	     c->action_button_height, c->dark, c->osd_alpha, c->highlight);
	draw_keycap(s, c, font, key, x + 7, key_y, red);
	draw_text(s, font, label, x + 22, text_y, c->action_px, c->highlight);
#endif
}

#if GKD_SHARED_CRT
static void draw_background_accent(struct draw_target *s,
				   const struct gkd_ui_config *c)
{
#if GKD_APPLICATION_UI
	unsigned x, y;
	/* Accepted A-only dark phosphor background.  Menu and button code stays shared. */
	for (y = 10U; y + 10U < s->height; ++y) {
		for (x = 10U; x + 10U < s->width; ++x) {
			int dx = (int)x - 142;
			int dy = (int)y - 88;
			unsigned radius = (unsigned)(dx * dx + 2 * dy * dy);
			unsigned glow = radius < 48000U ? (48000U - radius) / 3200U : 0U;
			unsigned edge_x = x < s->width - 1U - x ? x : s->width - 1U - x;
			unsigned edge_y = y < s->height - 1U - y ? y : s->height - 1U - y;
			unsigned edge = edge_x < edge_y ? edge_x : edge_y;
			unsigned alpha = (3U + glow) * (edge < 30U ? edge : 30U) / 30U;
			if (y % 3U == 0U) alpha = alpha * 2U / 3U;
			if (((x & 1U) ^ (y & 1U)) && alpha) --alpha;
			pixel(s, (int)x, (int)y, gkd_ui_blend565_value(c->dark, c->normal, alpha), 255U);
		}
	}
#else
	/* Industrial CRT band: broad 45-degree stripes, no warning state implied. */
	int x = (int)c->accent_x;
	int y = (int)c->action_lane_y +
		((int)c->action_button_height - (int)c->accent_height) / 2;
	unsigned xx, yy;
	fill(s, x, y, c->accent_width, 1, c->accent, c->accent_rail_alpha);
	fill(s, x, y + (int)c->accent_height - 1, c->accent_width, 1,
	     c->accent, c->accent_rail_alpha);
	for (yy = 0; yy < c->accent_height - 4U; ++yy)
		for (xx = 0; xx < c->accent_width - 8U; ++xx)
			if ((xx + yy) % c->accent_period < c->accent_stroke)
				pixel(s, x + 4 + (int)xx, y + 2 + (int)yy,
				      c->accent, yy % 3U == 0U ?
				      c->accent_dim_alpha : c->accent_alpha);
#endif
}
#endif

static void clear_crt(struct draw_target *s, const struct gkd_ui_config *c)
{
	unsigned y;
	fill(s, 0, 0, s->width, s->height, c->dark, 255);
#if GKD_SHARED_CRT
	/* Rounded glass/bezel, with all shades blended from the locked palette. */
	for (y = 2; y + 2U < s->height; ++y) {
		unsigned distance = y - 2U;
		unsigned bottom = s->height - 3U - y;
		unsigned inset, left, right, x;
		if (bottom < distance) distance = bottom;
		inset = distance == 0U ? 9U : distance == 1U ? 6U :
			distance == 2U ? 4U : distance == 3U ? 3U :
			distance == 4U ? 2U : distance == 5U ? 1U : 0U;
		left = 2U + inset; right = s->width - 3U - inset;
		for (x = left; x <= right; ++x) {
			unsigned edge = x - left;
			unsigned far_edge = right - x;
			if (far_edge < edge) edge = far_edge;
			if (distance < edge) edge = distance;
			if (edge == 0U) pixel(s, (int)x, (int)y, c->highlight, 95);
			else if (edge == 1U) pixel(s, (int)x, (int)y, c->normal, 35);
			else if (edge == 3U) pixel(s, (int)x, (int)y, c->highlight, 72);
			else if (edge > 3U) pixel(s, (int)x, (int)y,
				c->normal, y % 3U == 0U ? 9U : 3U);
		}
	}
	for (y = 0; y < 4U; ++y) {
		int cx = y & 1U ? (int)s->width - 6 : 5;
		int cy = y & 2U ? (int)s->height - 6 : 5;
		pixel(s, cx, cy, c->normal, 105);
		pixel(s, cx - 1, cy, c->highlight, 90);
		pixel(s, cx + 1, cy, c->highlight, 90);
		pixel(s, cx, cy - 1, c->highlight, 90);
		pixel(s, cx, cy + 1, c->highlight, 90);
	}
	draw_background_accent(s, c);
#else
	for (y = 1; y < s->height; y += 3U)
		fill(s, 0, (int)y, s->width, 1, rgb565(0, 17, 7), 85);
	rect(s, 2, 2, s->width - 4U, s->height - 4U, c->dark,
	     c->panel_alpha, c->highlight);
#endif
}

static int valid_surface(const struct gkd_ui_surface *s)
{
	return s && s->pixels && s->width == GKD_UI_WIDTH &&
		s->height == GKD_UI_HEIGHT && s->stride >= s->width &&
		s->stride <= UINT32_MAX / s->height;
}

static int valid_text(const char *text, size_t max)
{
	size_t length;
	if (!text) return 0;
	for (length = 0; length <= max; ++length)
		if (!text[length]) return length != 0U;
	return 0;
}

static int valid_font(const struct gkd_ui_font *font)
{
	uint64_t glyph_bytes;
	if (!font || !font->data || font->headersize < 32U || !font->length ||
		!font->charsize || !font->width || !font->height ||
		font->width > 32U || font->height > 32U) return 0;
	if (font->latin && (font->latin == font || font->latin->latin ||
		font->latin->compact || !valid_font(font->latin))) return 0;
	if (font->compact && (font->compact == font || font->compact->compact ||
		!font->latin || font->compact->latin != font->latin ||
		font->height != 14U || font->width != 14U ||
		font->compact->height != 12U || font->compact->width != 12U ||
		!valid_font(font->compact))) return 0;
	glyph_bytes = (uint64_t)font->length * font->charsize;
	return font->charsize >= font->height * ((font->width + 7U) / 8U) &&
		font->headersize <= font->size && glyph_bytes <= font->size - font->headersize;
}

/* Public render calls can receive manually-built config, unlike the loader. */
static int valid_public_config(const struct gkd_ui_config *c)
{
	if (!c || !c->menu_row_step || c->menu_row_x > GKD_UI_WIDTH ||
		c->menu_row_width > GKD_UI_WIDTH - c->menu_row_x ||
		c->menu_row_y > GKD_UI_HEIGHT ||
		c->menu_row_height > GKD_UI_HEIGHT - c->menu_row_y ||
		c->menu_row_width < 60U || c->menu_row_height < 18U ||
		c->menu_icon_size < 8U || c->menu_icon_size > 24U ||
		c->menu_icon_size > c->menu_row_width - 38U ||
		c->title_px < 8U || c->title_px > 24U ||
		c->body_px < 8U || c->body_px > 24U || c->action_px != 12U ||
		c->panel_alpha > 255U || c->osd_alpha > 255U ||
		!valid_text(c->action_yes, sizeof(c->action_yes) - 1U) ||
		!valid_text(c->action_no, sizeof(c->action_no) - 1U) ||
		c->action_lane_x > GKD_UI_WIDTH ||
		c->action_lane_width > GKD_UI_WIDTH - c->action_lane_x ||
		c->action_lane_y > GKD_UI_HEIGHT ||
		c->action_button_height > GKD_UI_HEIGHT - c->action_lane_y ||
		c->action_button_height < 15U || c->action_button_height > 32U ||
		c->action_button_width < 40U || c->action_button_gap > c->action_lane_width ||
		c->action_button_width > (c->action_lane_width - c->action_button_gap) / 2U ||
		c->action_key_size < 9U || c->action_key_size > c->action_button_height)
		return 0;
#if GKD_SHARED_CRT
	if (c->accent_x > c->action_lane_x || c->accent_width > c->action_lane_x - c->accent_x ||
		c->accent_height > c->action_button_height || c->accent_period < 2U ||
		c->accent_stroke >= c->accent_period || c->action_disabled_alpha > 255U ||
		c->menu_content_shift > 30U || c->menu_text_shift > 4U ||
		c->accent_alpha > 255U || c->accent_dim_alpha > 255U ||
		c->accent_rail_alpha > 255U) return 0;
#endif
	return 1;
}

static int valid_menu(const struct gkd_ui_surface *s,
			      const struct gkd_ui_font *font,
			      const struct gkd_ui_menu *menu)
{
	unsigned i;
	if (!valid_surface(s) || !valid_font(font) || !menu || !menu->items || !menu->count ||
		menu->count > GKD_UI_MENU_MAX_ITEMS || menu->selected >= menu->count ||
		!valid_text(menu->title, 47U)) return 0;
	if ((int)menu->action_a < 0 || (unsigned)menu->action_a > GKD_UI_ACTION_ENABLED ||
		(int)menu->action_b < 0 || (unsigned)menu->action_b > GKD_UI_ACTION_ENABLED)
		return 0;
	if ((menu->action_a != GKD_UI_ACTION_HIDDEN &&
		!valid_text(menu->action_a_label, 15U)) ||
		(menu->action_b != GKD_UI_ACTION_HIDDEN &&
		!valid_text(menu->action_b_label, 15U))) return 0;
	for (i = 0; i < menu->count; ++i)
		if (menu->items[i].icon >= GKD_UI_ICON_COUNT ||
			!valid_text(menu->items[i].label, 20U)) return 0;
	return 1;
}

static int valid_menu_geometry(const struct gkd_ui_config *c,
			       const struct gkd_ui_font *font,
			       const struct gkd_ui_menu *menu)
{
	unsigned label_space;
	if (c->menu_row_y > c->action_lane_y ||
		c->menu_row_height > c->action_lane_y - c->menu_row_y) return 0;
	unsigned visible = menu->count > 5U ? 5U : menu->count;
	if (menu->count > visible && c->menu_row_x + c->menu_row_width > GKD_UI_WIDTH - 9U) return 0;
	if (visible > 1U && c->menu_row_step >
		(c->action_lane_y - c->menu_row_y - c->menu_row_height) /
		(visible - 1U)) return 0;
	if (text_width(font, menu->title, c->title_px) > GKD_UI_WIDTH) return 0;
	label_space = c->menu_row_width - 60U;
	for (unsigned i = 0; i < menu->count; ++i)
		if (text_width(font, menu->items[i].label, c->body_px) > label_space)
			return 0;
	return 1;
}

static void draw_menu_action(struct draw_target *s,
			     const struct gkd_ui_config *c,
			     const struct gkd_ui_font *font, int x, char key,
			     const char *label, int red,
			     enum gkd_ui_action_state state)
{
	if (state == GKD_UI_ACTION_HIDDEN) return;
#if GKD_SHARED_CRT
	if (state == GKD_UI_ACTION_DISABLED) {
		struct gkd_ui_config disabled = *c;
		disabled.normal = gkd_ui_blend565_value(c->dark, c->normal, c->action_disabled_alpha);
		disabled.highlight = gkd_ui_blend565_value(c->dark, c->highlight, c->action_disabled_alpha);
		disabled.white = gkd_ui_blend565_value(c->dark, c->white, c->action_disabled_alpha);
		disabled.button_b = gkd_ui_blend565_value(c->dark, c->button_b, c->action_disabled_alpha);
		draw_action(s, &disabled, font, x, key, label, red);
		return;
	}
#else
	(void)state;
#endif
	draw_action(s, c, font, x, key, label, red);
}

/* All surfaces reuse this footer and the same enabled/disabled primitives. */
static void draw_actions(struct draw_target *target, const struct gkd_ui_config *c,
                         const struct gkd_ui_font *font,
                         enum gkd_ui_action_state a, enum gkd_ui_action_state b,
                         const char *yes, const char *no)
{
    unsigned total = c->action_button_width * 2U + c->action_button_gap;
    unsigned x = c->action_lane_x + (c->action_lane_width - total) / 2U;
    draw_menu_action(target, c, font, (int)x, 'A', yes, 0, a);
    draw_menu_action(target, c, font, (int)(x + c->action_button_width + c->action_button_gap), 'B', no, 1, b);
}

static void draw_scrollbar(struct draw_target *target, const struct gkd_ui_config *c,
                           int x, int y, unsigned height,
                           unsigned total, unsigned visible, unsigned first)
{
    if (total <= visible) return;
    unsigned thumb = height * visible / total;
    if (thumb < 4U) thumb = 4U;
    unsigned offset = first * (height - thumb) / (total - visible);
    fill(target, x, y, 3U, height, c->highlight, c->action_disabled_alpha);
    fill(target, x, y + (int)offset, 3U, thumb, c->normal, 255U);
}

static int render_menu(struct gkd_ui_surface *s,
		       const struct gkd_ui_config *c,
		       const struct gkd_ui_font *font,
		       const struct gkd_ui_menu *menu,
		       const char *const values[], const char *hint)
{
	struct draw_target target;
	unsigned i;
	unsigned title_width, hint_px = 0;
	int content_shift, text_shift;
#if GKD_APPLICATION_UI
	/* A/B hints are permanent. Preserve old callers without hiding controls. */
	struct gkd_ui_menu permanent;
	if (menu) {
		permanent = *menu;
		permanent.action_a_label = "YES";
		permanent.action_b_label = "NO";
		if (permanent.action_a == GKD_UI_ACTION_HIDDEN) permanent.action_a = GKD_UI_ACTION_DISABLED;
		if (permanent.action_b == GKD_UI_ACTION_HIDDEN) permanent.action_b = GKD_UI_ACTION_DISABLED;
		menu = &permanent;
	}
#endif
	if (!valid_public_config(c) || !valid_menu(s, font, menu) ||
		!valid_menu_geometry(c, font, menu)) return -1;
	if (values) {
		if (c->menu_row_width < 132U || (hint && !valid_text(hint, 60U))) return -1;
		if (hint) {
			for (hint_px = 16U; hint_px >= 8U; --hint_px)
				if (text_width(font, hint, hint_px) <= c->action_lane_width) break;
			if (hint_px < 8U) return -1;
		}
		for (i = 0; i < menu->count; ++i) {
			if (!values[i]) continue; /* Action row reuses a regular menu row. */
			if (!valid_text(values[i], menu->count>=5U?32U:3U) ||
			    text_width(font, values[i], menu->count>=5U?12U:c->body_px) > (menu->count>=5U?76U:36U) ||
			    text_width(font, menu->items[i].label, c->body_px) >
			        c->menu_row_width - (menu->count>=5U?146U:110U)) return -1;
			for (unsigned k = 0; menu->count<5U && values[i][k]; ++k)
				if ((unsigned char)values[i][k] < 32U ||
				    (unsigned char)values[i][k] > 126U) return -1;
		}
	}
#if GKD_SHARED_CRT
	content_shift = (int)c->menu_content_shift;
	text_shift = (int)c->menu_text_shift;
#else
	content_shift = 0;
	text_shift = 0;
#endif
	target = rgb_target(s);
	clear_crt(&target, c);
	title_width = text_width(font, menu->title, c->title_px);
	draw_text(&target, font, menu->title, ((int)s->width - (int)title_width) / 2,
		  17, c->title_px, c->white);
	unsigned first = menu->selected >= 5U ? menu->selected - 4U : 0U;
	for (i = first; i < menu->count && i < first + 5U; ++i) {
		int y = (int)c->menu_row_y + (int)((i - first) * c->menu_row_step);
		uint16_t foreground = i == menu->selected ? c->dark : c->normal;
		uint16_t background = i == menu->selected ? c->highlight : c->dark;
		rect(&target, (int)c->menu_row_x, y, c->menu_row_width,
		     c->menu_row_height, background,
		     i == menu->selected ? 255U : c->panel_alpha, c->highlight);
		if (i == menu->selected) draw_text(&target, font, ">", (int)c->menu_row_x + 8,
			 y + 5, c->body_px, foreground);
		if (values && values[i]) {
			int left = (int)(c->menu_row_x + c->menu_row_width) - (menu->count>=5U?106:66);
			int right = (int)(c->menu_row_x + c->menu_row_width) - 12;
			int center = (left + right) / 2;
			int cy = y + (int)c->menu_row_height / 2;
			unsigned value_px=menu->count>=5U?12U:c->body_px;
			unsigned width = text_width(font, values[i], value_px);
			/* Shared fixed geometry, independent of value width and selection. */
			for (int dy = -4; dy <= 4; ++dy) {
				unsigned span = (unsigned)(5 - (dy < 0 ? -dy : dy));
				fill(&target, left + 2 - (int)span, cy + dy, span, 1U, foreground, 255U);
				fill(&target, right - 2, cy + dy, span, 1U, foreground, 255U);
			}
			draw_text(&target, font, values[i], center - (int)width / 2,
			          y + ((int)c->menu_row_height - (int)value_px) / 2 + text_shift,
			          value_px, foreground);
			draw_text(&target, font, menu->items[i].label, (int)c->menu_row_x + 26,
			          y + ((int)c->menu_row_height - (int)c->body_px) / 2 + text_shift,
			          c->body_px, foreground);
			continue;
		}
		draw_icon(&target, menu->items[i].icon, (int)c->menu_row_x + 38 - content_shift,
			  y + ((int)c->menu_row_height - (int)c->menu_icon_size) / 2,
			  c->menu_icon_size, foreground);
		draw_text(&target, font, menu->items[i].label, (int)c->menu_row_x + 60 - content_shift,
			  y + ((int)c->menu_row_height - (int)c->body_px) / 2 + text_shift,
			  c->body_px, foreground);
	}
    /* Shared overflow indicator: identical geometry for every menu type. */
    if (menu->count > 5U) {
        unsigned height = 4U * c->menu_row_step + c->menu_row_height;
        draw_scrollbar(&target, c, (int)(c->menu_row_x + c->menu_row_width + 6U),
                       (int)c->menu_row_y, height, menu->count, 5U, first);
    }
	if (values && hint)
		draw_text(&target, font, hint, (int)c->action_lane_x,
		          (int)c->action_lane_y - 20, hint_px, c->normal);
    draw_actions(&target, c, font, menu->action_a, menu->action_b,
                 menu->action_a_label, menu->action_b_label);
	return 0;
}


int gkd_ui_render_menu(struct gkd_ui_surface *s, const struct gkd_ui_config *c,
                      const struct gkd_ui_font *font, const struct gkd_ui_menu *menu)
{
    return render_menu(s, c, font, menu, NULL, NULL);
}
int gkd_ui_render_settings(struct gkd_ui_surface *s, const struct gkd_ui_config *c,
                          const struct gkd_ui_font *font, const struct gkd_ui_menu *menu,
                          const char *const values[], const char *hint)
{
    if (!values || !valid_public_config(c) || !menu ||
        (menu->count < 4U || menu->count > GKD_UI_MENU_MAX_ITEMS) || menu->action_a == GKD_UI_ACTION_HIDDEN ||
        menu->action_b == GKD_UI_ACTION_HIDDEN) return -1;
    return render_menu(s, c, font, menu, values, hint);
}

unsigned gkd_ui_confirmation_visible(const struct gkd_ui_config *c)
{
    if (!valid_public_config(c) || c->menu_row_y > c->action_lane_y ||
        c->menu_row_height > c->action_lane_y - c->menu_row_y ||
        c->menu_row_step > (c->action_lane_y - c->menu_row_y - c->menu_row_height) / 4U)
        return 0U;
    unsigned height = c->menu_row_height + 4U * c->menu_row_step;
    return height < 16U + c->body_px ? 0U : (height - 14U) / (c->body_px + 2U);
}

int gkd_ui_layout_text(const struct gkd_ui_config *c, const struct gkd_ui_font *font,
                       const char *text, struct gkd_ui_text_layout *layout)
{
    struct gkd_ui_text_layout next = {0};
    unsigned row = 0U, used = 0U, width = 0U;
    if (!valid_public_config(c) || !valid_font(font) || !layout || !text ||
        !valid_text(text, GKD_UI_TEXT_MAX_BYTES)) return -1;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        if (*p == '\n' || *p == '\r') {
            if (*p++ == '\r' && *p == '\n') ++p;
            if (++row == GKD_UI_TEXT_MAX_LINES) return -1;
            used = width = 0U;continue;
        }
        const unsigned char *start = p;
        uint32_t cp = utf8_next(&p);
        if (cp < 32U || cp == 127U ||
            (cp >= 0xd800U && cp <= 0xdfffU) || (cp == '?' && *start != '?')) return -1;
        /* Body-only policy: omit valid non-ASCII text, independent of UI locale. */
        if (cp > 126U) continue;
        const struct gkd_ui_font *face = codepoint_font(font, cp, c->body_px);
        if ((!(face->flags & PSF2_HAS_UNICODE_TABLE) && cp >= face->length) ||
            font_index(face, cp) >= face->length) return -1;
        unsigned bytes = (unsigned)(p - start);
        unsigned advance = glyph_width(face, c->body_px) + 1U;
        if (advance > c->menu_row_width - 16U) return -1;
        /* Keep a fitting word together; longer tokens still wrap by glyph. */
        if (width && cp != ' ' && start > (const unsigned char *)text && start[-1] == ' ') {
            const unsigned char *word = start;
            unsigned word_width = 0U;
            while (*word && *word != ' ' && *word != '\n' && *word != '\r') {
                uint32_t letter = utf8_next(&word);
                if (letter >= 32U && letter <= 126U)
                    word_width += glyph_width(codepoint_font(font, letter, c->body_px), c->body_px) + 1U;
            }
            if (word_width <= c->menu_row_width - 15U && width + word_width > c->menu_row_width - 15U) {
                if (++row == GKD_UI_TEXT_MAX_LINES) return -1;
                used = width = 0U;
            }
        }
        if (width + advance - 1U > c->menu_row_width - 16U || used + bytes >= 64U) {
            if (++row == GKD_UI_TEXT_MAX_LINES) return -1;
            used = width = 0U;
        }
        memcpy(next.storage[row] + used, start, bytes);
        used += bytes;width += advance;
    }
    int has_text = 0;
    for (unsigned i = 0; i <= row; ++i)
        for (const char *value = next.storage[i]; *value; ++value)
            if (*value != ' ') has_text = 1;
    if (!has_text) return -1;
    next.count = row + 1U;
    *layout = next;
    for (unsigned i = 0; i < layout->count; ++i) layout->lines[i] = layout->storage[i];
    return 0;
}

int gkd_ui_render_confirmation_info(struct gkd_ui_surface *s,
                                    const struct gkd_ui_config *c,
                                    const struct gkd_ui_font *font,
                                    const struct gkd_ui_confirmation *info)
{
    /* One text panel spans exactly the outer bounds of five menu rows. */
    unsigned visible = gkd_ui_confirmation_visible(c);
    if (!visible || !valid_surface(s) || !valid_font(font)) return -1;
    const unsigned height = c->menu_row_height + 4U * c->menu_row_step;
    const unsigned inset = 8U, step = c->body_px + 2U;
    const unsigned top = c->menu_row_y + inset;
    if (!info || !info->lines || !info->count || info->count > 32U ||
        info->first > (info->count > visible ? info->count - visible : 0U) ||
        !valid_text(info->title, 47U) || text_width(font, info->title, c->title_px) > s->width ||
        (info->count > visible && c->menu_row_x + c->menu_row_width > s->width - 9U) ||
        info->action_a < GKD_UI_ACTION_DISABLED || info->action_a > GKD_UI_ACTION_ENABLED ||
        info->action_b < GKD_UI_ACTION_DISABLED || info->action_b > GKD_UI_ACTION_ENABLED) return -1;
    for (unsigned i = 0; i < info->count; ++i) {
        if (!info->lines[i] || (info->lines[i][0] && !valid_text(info->lines[i], 63U)) ||
            text_width(font, info->lines[i], c->body_px) > c->menu_row_width - 2U * inset) return -1;
        for (const unsigned char *p = (const unsigned char *)info->lines[i]; *p; ++p)
            if (*p < 32U || *p > 126U) return -1;
    }
    struct draw_target target = rgb_target(s);
    clear_crt(&target, c);
    unsigned width = text_width(font, info->title, c->title_px);
    draw_text(&target, font, info->title, ((int)s->width - (int)width) / 2, 17, c->title_px, c->white);
    rect(&target, (int)c->menu_row_x, (int)c->menu_row_y,
         c->menu_row_width, height, c->dark, c->panel_alpha, c->highlight);
    for (unsigned i = info->first; i < info->count && i < info->first + visible; ++i)
        draw_text(&target, font, info->lines[i], (int)(c->menu_row_x + inset),
                  (int)(top + (i - info->first) * step), c->body_px, c->normal);
    draw_scrollbar(&target, c, (int)(c->menu_row_x + c->menu_row_width + 6U),
                   (int)c->menu_row_y, height, info->count, visible, info->first);
    draw_actions(&target, c, font, info->action_a, info->action_b, c->action_yes, c->action_no);
    return 0;
}

void gkd_ui_render_recovery_menu(struct gkd_ui_surface *s,
				 const struct gkd_ui_config *c,
				 const struct gkd_ui_font *font,
				 unsigned selected)
{
	struct gkd_ui_menu_item items[GKD_UI_MENU_ITEMS];
	struct gkd_ui_menu menu;
	unsigned i;
	if (!c) return;
	for (i = 0; i < GKD_UI_MENU_ITEMS; ++i) {
		unsigned action = gkd_ui_menu_action(i);
		items[i].label = c->labels[action];
		items[i].icon = action;
	}
	menu = (struct gkd_ui_menu){c->title, items, GKD_UI_MENU_ITEMS, selected,
		GKD_UI_ACTION_ENABLED,
#if GKD_DEDICATED_RECOVERY
		GKD_UI_ACTION_DISABLED,
#else
		GKD_UI_ACTION_ENABLED,
#endif
		c->action_yes, c->action_no};
	(void)gkd_ui_render_menu(s, c, font, &menu);
}

static void draw_osd_icon(struct draw_target *s, unsigned icon, int x, int y,
			  uint16_t color, int level)
{
	static const char masters[GKD_UI_OSD_ICON_COUNT][14][15] = {
		{"..............",".......#......","......##..#...","..######...#..",
		 "..######.#..#.","..######..#.#.","..######..#.#.","..######..#.#.",
		 "..######.#..#.","..######...#..","......##..#...",".......#......",
		 "..............",".............."},
		{"......##......","......##......","..#........#..","...#......#...",
		 ".....####.....","....#....#....","##..#....#..##","##..#....#..##",
		 "....#....#....",".....####.....","...#......#...","..#........#..",
		 "......##......","......##......"},
		{"..............","..............",".##########...",".#........#...",
		 ".#.##.##..###.",".#.##.##..#.#.",".#.##.##..#.#.",".#.##.##..#.#.",
		 ".#.##.##..#.#.",".#.##.##..###.",".#........#...",".##########...",
		 "..............",".............."},
		{"..............","....#####.....","...#.....#....",".############.",
		 ".#..........#.",".#...####...#.",".#..#....#..#.",".#..#....#..#.",
		 ".#..#....#..#.",".#...####...#.",".#..........#.",".############.",
		 "..............",".............."},
		{"....######....","....#....#....","....#....#....","....######....",
		 "......##......","......##......","..##########..","..#........#..",
		 ".####....####.",".#..#....#..#.",".####....####.","..............",
		 "..............",".............."},
		{".......##.....","......##......",".....##.......","....##........",
		 "...##.........","..##########..",".......###....","......###.....",
		 ".....###......","....###.......","...###........","..###.........",
		 "..##..........",".............."},
		{"..............","...########...","..#.......#...",".#..#.#.#.#...",
		 ".#........#...",".#........#...",".#........#...",".#........#...",
		 ".#........#...",".#..####..#...",".#..#..#..#...",".#..####..#...",
		 ".##########...",".............."},
		{"..............","....######....","....#....#....","..##########..",
		 "..#.#.##.#.#..","###.#.##.#.###","..#.#....#.#..","###.#.##.#.###",
		 "..#.#.##.#.#..","..##########..","....#....#....","....######....",
		 "..............",".............."},
		/* Save notification glyph; camera remains dedicated to screenshots. */
		{"..............",".###########..",".#..#....#..#.",".#..#....#..#.",".#..######..#.",".#..........#.",".#..........#.",".#..######..#.",".#..#....#..#.",".#..#....#..#.",".#..#....#..#.",".#..######..#.",".############.",".............."},

		/* Shared results: 11px solid circle aligned with camera icon height; bold cutout marks. */
		{"..............",".....#####....","....#######...","...#########..","..########.##.","..#######..##.","..##.###..###.","..##..#..####.","..###...#####.","...###.#####..","....#######...",".....#####....","..............",".............."},
		{"..............",".....#####....","....#######...","...#..###..#..","..###..#..###.","..####...####.","..#####.#####.","..####...####.","..###..#..###.","...#..###..#..","....#######...",".....#####....","..............",".............."},


	};
	unsigned xx, yy;
#if !GKD_APPLICATION_UI
	(void)level; /* Frozen recovery keeps its accepted glyph. */
#endif
	for (yy = 0; yy < 14U; ++yy)
		for (xx = 0; xx < 14U; ++xx) {
			int lit = masters[icon][yy][xx] == '#';
#if GKD_APPLICATION_UI
			/* Seven owner-selected states share the original outline.
			 * Unknown/0-2 stays empty; the remaining bands fill 1-6 columns. */
			if (icon == 2U && yy >= 4U && yy <= 9U && xx >= 3U && xx <= 8U)
				lit = xx - 3U < (unsigned)((level > 2) + (level > 10) +
				      (level > 25) + (level > 50) + (level > 75) + (level > 90));
#endif
			if (lit) pixel(s, x + (int)xx, y + (int)yy, color, 255U);
		}
}

static int valid_osd_values(const struct gkd_ui_config *c,
			       const struct gkd_ui_font *font,
			       const struct gkd_ui_osd *osd, unsigned opacity)
{
	return valid_public_config(c) && valid_font(font) && osd &&
		osd->icon < GKD_UI_OSD_ICON_COUNT && osd->level >= -1 &&
		osd->level <= 100 && (osd->critical == 0 || osd->critical == 1) &&
		opacity <= 255U && c->action_px == 12U &&
		valid_text(osd->text, GKD_UI_OSD_MAX_TEXT) &&
		text_width(font, osd->text, c->action_px) <= GKD_UI_OSD_WIDTH - 30U;
}
static int valid_osd(const struct gkd_ui_surface *s,
		     const struct gkd_ui_config *c, const struct gkd_ui_font *font,
		     const struct gkd_ui_osd *osd, unsigned opacity)
{
	return valid_surface(s) && valid_osd_values(c, font, osd, opacity);
}

static void draw_osd_opaque(struct draw_target *s,
			    const struct gkd_ui_config *c,
			    const struct gkd_ui_font *font,
			    const struct gkd_ui_osd *osd, int x, int y)
{
	int warning = osd->critical;
#if GKD_APPLICATION_UI
	warning |= osd->icon == 2U && osd->level >= 0 && osd->level <= 10;
#endif
	uint16_t ink = warning ? c->button_b : c->normal;
	unsigned rail;
	rect(s, x, y, GKD_UI_OSD_WIDTH, GKD_UI_OSD_HEIGHT, c->dark, c->panel_alpha,
	     gkd_ui_blend565_value(c->dark, warning ? c->button_b : c->highlight, 120U));
	draw_osd_icon(s, osd->icon, x + 6, y + 2, ink, osd->level);
	draw_text(s, font, osd->text, x + 26, y + 3, c->action_px, ink);
	if (osd->level >= 0) {
		rail = GKD_UI_OSD_WIDTH - 36U;
		fill(s, x + 26, y + (int)GKD_UI_OSD_HEIGHT - 3, rail, 1U,
		     gkd_ui_blend565_value(c->dark, c->highlight, 75U), 255U);
		fill(s, x + 26, y + (int)GKD_UI_OSD_HEIGHT - 3,
		     rail * (unsigned)osd->level / 100U, 1U, ink, 255U);
	}
}

int gkd_ui_draw_osd(struct gkd_ui_surface *s, const struct gkd_ui_config *c,
		    const struct gkd_ui_font *font, const struct gkd_ui_osd *osd,
		    unsigned opacity)
{
	uint16_t composed[GKD_UI_OSD_WIDTH * GKD_UI_OSD_HEIGHT];
	struct gkd_ui_surface local;
	struct draw_target target, local_target;
	unsigned x, y;
	if (!valid_osd(s, c, font, osd, opacity)) return -1;
	target = rgb_target(s);
	if (!opacity) return 0;
	if (opacity == 255U) {
		draw_osd_opaque(&target, c, font, osd, GKD_UI_OSD_X, GKD_UI_OSD_Y);
		return 0;
	}
	for (y = 0; y < GKD_UI_OSD_HEIGHT; ++y)
		for (x = 0; x < GKD_UI_OSD_WIDTH; ++x)
			composed[y * GKD_UI_OSD_WIDTH + x] =
				s->pixels[(GKD_UI_OSD_Y + y) * s->stride + GKD_UI_OSD_X + x];
	local = (struct gkd_ui_surface){composed, GKD_UI_OSD_WIDTH,
		GKD_UI_OSD_HEIGHT, GKD_UI_OSD_WIDTH};
	local_target = rgb_target(&local);
	draw_osd_opaque(&local_target, c, font, osd, 0, 0);
	for (y = 0; y < GKD_UI_OSD_HEIGHT; ++y)
		for (x = 0; x < GKD_UI_OSD_WIDTH; ++x) {
			size_t destination = (size_t)(GKD_UI_OSD_Y + y) * s->stride +
				GKD_UI_OSD_X + x;
			s->pixels[destination] = gkd_ui_blend565_value(s->pixels[destination],
				composed[y * GKD_UI_OSD_WIDTH + x], opacity);
		}
	return 0;
}

int gkd_ui_export_osd_argb(uint32_t *pixels, size_t pixel_count,
		const struct gkd_ui_config *c, const struct gkd_ui_font *font,
		const struct gkd_ui_osd *osd)
{
	struct draw_target target;
	if (!pixels || pixel_count < GKD_UI_OSD_WIDTH * GKD_UI_OSD_HEIGHT ||
		!valid_osd_values(c, font, osd, 255U)) return -1;
	memset(pixels, 0, GKD_UI_OSD_WIDTH * GKD_UI_OSD_HEIGHT * sizeof(*pixels));
	target = (struct draw_target){NULL, GKD_UI_OSD_WIDTH, GKD_UI_OSD_HEIGHT,
		GKD_UI_OSD_WIDTH, pixels, DRAW_ARGB8888};
	draw_osd_opaque(&target, c, font, osd, 0, 0);
	return 0;
}

int gkd_ui_fade_opacity(unsigned elapsed_ms, unsigned remaining_ms,
			unsigned fade_ms, unsigned *opacity)
{
	return gkd_ui_fade_value(elapsed_ms, remaining_ms, fade_ms, opacity);
}

void gkd_ui_render_status(struct gkd_ui_surface *s,
			  const struct gkd_ui_config *c,
			  const struct gkd_ui_font *font,
			  const char *label, int failed)
{
	struct draw_target target;
	unsigned width;
	target = rgb_target(s);
	clear_crt(&target, c);
	draw_icon(&target, failed ? 3U : 1U, 153, 87, 14,
		  failed ? c->button_b : c->normal);
	width = text_width(font, label, c->body_px);
	draw_text(&target, font, label, ((int)s->width - (int)width) / 2, 111,
		  c->body_px, failed ? c->button_b : c->normal);
#if GKD_APPLICATION_UI
	/* Error/status pages also retain the unavailable confirm hint. */
	draw_menu_action(&target, c, font, (int)c->action_lane_x,
		'A', c->action_yes, 0, GKD_UI_ACTION_DISABLED);
#endif
	draw_action(&target, c, font,
		(int)(c->action_lane_x + c->action_lane_width - c->action_button_width),
		'B', c->action_no, 1);
}

void gkd_ui_render_confirmation(struct gkd_ui_surface *s,
				const struct gkd_ui_config *c,
				const struct gkd_ui_font *font,
				const char *label)
{
	struct draw_target target;
	unsigned width, button_total, action_x;
	target = rgb_target(s);
	clear_crt(&target, c);
	width = text_width(font, "CONFIRM", c->title_px);
	draw_text(&target, font, "CONFIRM", ((int)s->width - (int)width) / 2,
		  62, c->title_px, c->white);
	width = text_width(font, label, c->body_px);
	draw_text(&target, font, label, ((int)s->width - (int)width) / 2,
		  106, c->body_px, c->normal);
	button_total = c->action_button_width * 2U + c->action_button_gap;
	action_x = c->action_lane_x +
		(c->action_lane_width - button_total) / 2U;
	draw_action(&target, c, font, (int)action_x, 'A', c->action_yes, 0);
	draw_action(&target, c, font,
		(int)(action_x + c->action_button_width + c->action_button_gap),
		'B', c->action_no, 1);
}

void gkd_ui_render_loading(struct gkd_ui_surface *s,
			   const struct gkd_ui_config *c,
			   const struct gkd_ui_font *font,
			   unsigned frame)
{
	struct draw_target target;
	static const signed char points[8][2] = {
		{3, 0}, {5, 1}, {6, 3}, {5, 5},
		{3, 6}, {1, 5}, {0, 3}, {1, 1}
	};
	unsigned i, width;
	int x, y, scale;
	target = rgb_target(s);
	clear_crt(&target, c);
	scale = (int)c->loading_icon_size / 7;
	if (scale < 1) scale = 1;
	x = ((int)s->width - 7 * scale) / 2;
	y = 101;
	for (i = 0; i < ARRAY_SIZE(points); ++i)
		fill(&target, x + points[i][0] * scale, y + points[i][1] * scale,
		     (unsigned)scale, (unsigned)scale,
		     i == frame % ARRAY_SIZE(points) ? c->white : c->normal,
		     i == frame % ARRAY_SIZE(points) ? 255U : 150U);
	width = text_width(font, c->loading_label, c->loading_px);
	draw_text(&target, font, c->loading_label,
		  ((int)s->width - (int)width) / 2,
		  y + (int)c->loading_icon_size + 7,
		  c->loading_px, c->normal);
}

int gkd_ui_write_raw(const struct gkd_ui_surface *surface, const char *path)
{
	FILE *file = fopen(path, "wb");
	unsigned y;
	if (!file) return -1;
	for (y = 0; y < surface->height; ++y)
		if (fwrite(surface->pixels + y * surface->stride,
			   sizeof(uint16_t), surface->width, file) != surface->width) {
			fclose(file); return -1;
		}
	return fclose(file);
}
