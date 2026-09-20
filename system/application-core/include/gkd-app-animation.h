/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_ANIMATION_H
#define GKD_APP_ANIMATION_H
#include <sys/types.h>
struct gkd_app_animation { pid_t pid; int command; unsigned stage; };
#define GKD_APP_ANIMATION_INIT {-1, -1, 0}
/* Borrow a caller-verified immutable asset descriptor. Start acknowledges the
 * first submitted frame. Successful finish proves reap. Failed finish forbids
 * application release; the namespace owner must exit if reap remains pending. */
int gkd_app_animation_begin(struct gkd_app_animation *, int, unsigned);
int gkd_app_animation_stage(struct gkd_app_animation *, unsigned);
int gkd_app_animation_finish(struct gkd_app_animation *);
void gkd_app_animation_progress(unsigned short *, unsigned);
#endif
