// SPDX-License-Identifier: GPL-2.0
#ifndef GKD_UI_H
#define GKD_UI_H

#include <stddef.h>
#include <stdint.h>

#define GKD_UI_WIDTH 320U
#define GKD_UI_HEIGHT 240U
#define GKD_UI_MENU_ACTIONS 5U
#if GKD_APPLICATION_UI
#define GKD_UI_MENU_MAX_ITEMS 6U
#else
#define GKD_UI_MENU_MAX_ITEMS 5U
#endif
#define GKD_UI_ICON_COUNT 5U
#define GKD_UI_OSD_ICON_SAVE 8U
#define GKD_UI_OSD_ICON_SUCCESS 9U
#define GKD_UI_OSD_ICON_FAILURE 10U
#define GKD_UI_OSD_ICON_COUNT 11U
#define GKD_UI_OSD_X 8U
#define GKD_UI_OSD_Y 206U
#define GKD_UI_OSD_WIDTH 148U
#define GKD_UI_OSD_HEIGHT 19U
#define GKD_UI_OSD_MAX_TEXT 13U
#if GKD_DEDICATED_RECOVERY || GKD_APPLICATION_UI
#define GKD_UI_MENU_ITEMS 4U
#else
#define GKD_UI_MENU_ITEMS 5U
#endif

/* Preserve the shared config ABI; dedicated R omits retained-version rollback. */
static inline unsigned gkd_ui_menu_action(unsigned row)
{
#if GKD_DEDICATED_RECOVERY
	return row >= 2U ? row + 1U : row;
#else
	return row;
#endif
}

struct gkd_ui_rect {
	unsigned x, y, width, height;
};

struct gkd_ui_config {
	uint16_t normal, dark, highlight, white, button_b;
	unsigned panel_alpha, osd_alpha;
	struct gkd_ui_rect menu;
	unsigned menu_row_x, menu_row_y, menu_row_width, menu_row_height;
	unsigned menu_row_step, menu_icon_size;
	unsigned title_px, body_px, action_px;
	unsigned loading_icon_size, loading_px, loading_interval_ms;
	unsigned action_lane_x, action_lane_y, action_lane_width;
	unsigned action_button_width, action_button_height, action_button_gap;
	unsigned action_key_size;
	unsigned input_style; /* 0 raw, 1 Xbox, 2 PS; entry-frozen UI prompts. */
#if GKD_DEDICATED_RECOVERY || GKD_APPLICATION_UI
	uint16_t accent;
	unsigned action_disabled_alpha;
	unsigned menu_content_shift, menu_text_shift;
	unsigned accent_x, accent_width, accent_height;
	unsigned accent_period, accent_stroke;
	unsigned accent_alpha, accent_dim_alpha, accent_rail_alpha;
#endif
	char font_path[256];
	char title[48];
	char labels[GKD_UI_MENU_ACTIONS][64];
	char action_yes[16], action_no[16];
	char loading_label[32];
};

struct gkd_ui_surface {
	uint16_t *pixels;
	unsigned width, height, stride;
};

struct gkd_ui_font {
	unsigned char *data;
	size_t size;
	uint32_t flags, length, charsize, height, width, headersize;
	/* Explicit borrowed Latin face for a CJK face; caller owns both lifetimes. */
	const struct gkd_ui_font *latin;
	/* Borrowed native 12px CJK face for compact action/OSD text. */
	const struct gkd_ui_font *compact;
};

enum gkd_ui_action_state {
	GKD_UI_ACTION_HIDDEN = 0, /* Legacy callers: A menus render this as disabled. */
	GKD_UI_ACTION_DISABLED,
	GKD_UI_ACTION_ENABLED,
};

struct gkd_ui_menu_item {
	const char *label;
	unsigned icon;
};

struct gkd_ui_menu {
	const char *title;
	const struct gkd_ui_menu_item *items;
	unsigned count;
	unsigned selected;
	enum gkd_ui_action_state action_a;
	enum gkd_ui_action_state action_b;
	const char *action_a_label;
	const char *action_b_label;
};

struct gkd_ui_osd {
	const char *text;
	unsigned icon;
	int level; /* -1 hides the rail; otherwise 0..100. */
	int critical;
};

void gkd_ui_config_defaults(struct gkd_ui_config *config);
int gkd_ui_config_load(struct gkd_ui_config *config, const char *path,
		       int sparse);
int gkd_ui_font_load(struct gkd_ui_font *font, const char *path);
void gkd_ui_font_release(struct gkd_ui_font *font);
/* All input is validated before any target pixel is changed. */
int gkd_ui_render_menu(struct gkd_ui_surface *surface,
		       const struct gkd_ui_config *config,
		       const struct gkd_ui_font *font,
		       const struct gkd_ui_menu *menu);
/* Same native menu primitives; fixed arrow/value columns, without item icons.
 * Values must fit three ASCII glyphs (ON/OFF, 0..60, en/cn). */
int gkd_ui_render_settings(struct gkd_ui_surface *surface,
                          const struct gkd_ui_config *config,
                          const struct gkd_ui_font *font,
                          const struct gkd_ui_menu *menu,
                          const char *const values[], const char *hint);
/* Reusable English body layout: printable ASCII, LF/CRLF, font-aware wrapping.
 * Valid non-ASCII text is omitted; titles retain the caller-selected UI language.
 * Invalid UTF-8, controls, empty filtered text and overflow return errors.
 * The caller owns the text object for the lifetime of its lines. */
#define GKD_UI_TEXT_MAX_BYTES 2048U
#define GKD_UI_TEXT_MAX_LINES 32U
struct gkd_ui_text_layout {
    char storage[GKD_UI_TEXT_MAX_LINES][64];
    const char *lines[GKD_UI_TEXT_MAX_LINES];
    unsigned count;
};
int gkd_ui_layout_text(const struct gkd_ui_config *config,
                       const struct gkd_ui_font *font, const char *text,
                       struct gkd_ui_text_layout *layout);
unsigned gkd_ui_confirmation_visible(const struct gkd_ui_config *config);
/* Plain text confirmation: one panel spanning five menu rows, no selection.
 * Long text uses the same scrollbar; actions use the same permanent footer. */
struct gkd_ui_confirmation {
    const char *title;
    const char *const *lines;
    unsigned count, first;
    enum gkd_ui_action_state action_a, action_b;
};
int gkd_ui_render_confirmation_info(struct gkd_ui_surface *surface,
                                    const struct gkd_ui_config *config,
                                    const struct gkd_ui_font *font,
                                    const struct gkd_ui_confirmation *information);
void gkd_ui_render_recovery_menu(struct gkd_ui_surface *surface,
				 const struct gkd_ui_config *config,
				 const struct gkd_ui_font *font,
				 unsigned selected);
void gkd_ui_render_status(struct gkd_ui_surface *surface,
			  const struct gkd_ui_config *config,
			  const struct gkd_ui_font *font,
			  const char *label, int failed);
void gkd_ui_render_confirmation(struct gkd_ui_surface *surface,
				const struct gkd_ui_config *config,
				const struct gkd_ui_font *font,
				const char *label);
void gkd_ui_render_loading(struct gkd_ui_surface *surface,
			   const struct gkd_ui_config *config,
			   const struct gkd_ui_font *font,
			   unsigned frame);
/* `surface` must contain the current clean base frame on every call.
 * opacity 0 preserves it; 255 matches the opaque approved OSD composition. */
int gkd_ui_draw_osd(struct gkd_ui_surface *surface,
		    const struct gkd_ui_config *config,
		    const struct gkd_ui_font *font,
		    const struct gkd_ui_osd *osd, unsigned opacity);
/* Fixed 148x19 straight-ARGB8888 tile; writes exactly 2812 pixels. */
int gkd_ui_export_osd_argb(uint32_t *pixels, size_t pixel_count,
		    const struct gkd_ui_config *config,
		    const struct gkd_ui_font *font,
		    const struct gkd_ui_osd *osd);
/* Pure envelope helper for the existing display refresh owner; no clock/state. */
int gkd_ui_fade_opacity(unsigned elapsed_ms, unsigned remaining_ms,
			unsigned fade_ms, unsigned *opacity);
int gkd_ui_write_raw(const struct gkd_ui_surface *surface, const char *path);

#endif
