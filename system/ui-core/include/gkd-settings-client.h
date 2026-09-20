/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_SETTINGS_CLIENT_H
#define GKD_SETTINGS_CLIENT_H
#include <stddef.h>
/* One root-only local seqpacket; each call is bounded to 50 ms. */
int gkd_settings_exchange(const char *path,const char *request,char *reply,size_t capacity);
#endif
