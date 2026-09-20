/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_DISPLAY_H
#define GKD_APP_DISPLAY_H
/* Only the sole startup owner may call this after the old application is gone.
 * Refuse process-controlled terminals; acquire VT2 before framebuffer writing. */
int gkd_app_display_prepare(unsigned timeout_ms);
#endif
