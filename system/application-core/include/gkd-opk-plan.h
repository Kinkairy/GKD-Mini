/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GKD_OPK_PLAN_H
#define GKD_OPK_PLAN_H

#define GKD_OPK_PLAN_ARGV_MAX 256U

/*
 * mount_name is the validated raw OpenDingux Name value, not a path. It owns
 * its bytes and is NUL-terminated without altering valid UTF-8. argv owns its
 * strings and is always NULL-terminated on success.
 *
 * Caller supplies a zeroed output or calls gkd_opk_plan_close() before reuse.
 * argc/argv are only arguments following the OPK image in the launcher call.
 */
struct gkd_opk_plan {
    char *argv[GKD_OPK_PLAN_ARGV_MAX];
    char *mount_name;
    int needs_terminal;
    int needs_joystick;
    int needs_gsensor;
    int needs_downscaling;
    char desktop[256];
};

int gkd_opk_plan_open(const char *image_path, const char *metadata,
                      int argc, char *const argv[],
                      struct gkd_opk_plan *out);
void gkd_opk_plan_close(struct gkd_opk_plan *out);

#endif

