/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_MENU_CONFIG_H
#define GKD_APP_MENU_CONFIG_H
#include <stddef.h>
#include "gkd-menu-vt.h"
#define GKD_MENU_PROFILE_MAX 128U
#define GKD_MENU_CHORD_MAX 4U
#define GKD_MENU_CONFIG_BYTES 65536U
#define GKD_PORTRAIT_ORIGINAL_A 65534U
#define GKD_PORTRAIT_CURRENT_Y 65535U
/* No wildcard, shell command, path discovery or emulator settings mutation. */
enum gkd_menu_action { GKD_MENU_DISABLED, GKD_MENU_NATIVE, GKD_MENU_CHORD, GKD_MENU_NONE };
struct gkd_menu_profile {
    char id[64], opk_sha256[65], desktop[256], executable[256];
    enum gkd_menu_action action;
    unsigned short keys[GKD_MENU_CHORD_MAX];
    unsigned key_count, hold_ms;
    char rom[512]; /* Optional exact absolute ROM argument; no globs. */
    unsigned map_count;
    struct gkd_input_route_map maps[GKD_INPUT_ROUTE_MAPS];
    unsigned portrait_map_count;
    struct gkd_input_route_map portrait_maps[GKD_INPUT_ROUTE_MAPS];
};
struct gkd_menu_config {
    unsigned version, count;
    struct gkd_menu_profile profiles[GKD_MENU_PROFILE_MAX];
};
/* Transactional: *out is unchanged on error, bad_line identifies syntax errors.
 * Use a heap allocated config on constrained thread stacks. */
int gkd_menu_config_parse(const char *, size_t, struct gkd_menu_config *, unsigned *bad_line);
/* Read a pinned root-owned regular file, bounded in size, without following a
 * final symlink or accepting writable-by-others data. No automatic fallback. */
int gkd_menu_config_load(const char *, struct gkd_menu_config *, unsigned *bad_line);
int gkd_menu_config_load_fd(int, struct gkd_menu_config *, unsigned *bad_line);
/* Caller supplies identities from the same pinned OPK/selected launch plan.
 * Returns 1 matched, 0 unknown, -1 invalid identity; copies the session profile.
 * Unknown/invalid identities clear *out, preventing reuse of a prior session. */
int gkd_menu_config_select(const struct gkd_menu_config *, const char *opk_sha256,
                          const char *desktop, const char *executable,
                          struct gkd_menu_profile *);
int gkd_menu_config_select_game(const struct gkd_menu_config *, const char *, const char *,
                                const char *, int, char *const [], struct gkd_menu_profile *);
/* Every style keeps the hardware brightness key out of emulator VT input.
 * Raw keeps the other keys unchanged, including the native MENU key. */
/* Derive dot/A from the current physical Y output; do not change the base. */
int gkd_input_route_portrait(const struct gkd_menu_vt_config *,const struct gkd_menu_profile *,struct gkd_menu_vt_config *);
int gkd_input_route_compile(unsigned, unsigned short, unsigned short,
                            const struct gkd_menu_profile *, struct gkd_menu_vt_config *);
const char *gkd_menu_action_name(enum gkd_menu_action);
#endif
