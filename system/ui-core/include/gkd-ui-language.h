/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_UI_LANGUAGE_H
#define GKD_UI_LANGUAGE_H
enum gkd_ui_language { GKD_UI_EN, GKD_UI_CN };
enum gkd_ui_text {
#define GKD_UI_TEXT_ROW(id, key, en, cn) GKD_UI_TEXT_##id,
#include "gkd-ui-language.def"
#undef GKD_UI_TEXT_ROW
    GKD_UI_TEXT_COUNT
};
const char *gkd_ui_text(enum gkd_ui_language language, enum gkd_ui_text key);
struct gkd_ui_catalog { char values[GKD_UI_TEXT_COUNT][2][64]; };
/* Missing optional catalog uses built-in wording; malformed overrides fail atomically. */
int gkd_ui_catalog_load(struct gkd_ui_catalog *, const char *);
const char *gkd_ui_catalog_text(const struct gkd_ui_catalog *, enum gkd_ui_language, enum gkd_ui_text);
#endif
