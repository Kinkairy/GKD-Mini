/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_IDENTITY_H
#define GKD_APP_IDENTITY_H
#include <stdint.h>
struct gkd_app_identity { uint64_t bytes;char cid[33];unsigned char prefix_sha256[32]; };
/* Exact 20 MiB boot/kernel/layout prefix and physical card identity. */
int gkd_app_identity_read(struct gkd_app_identity *);
int gkd_app_identity_verify(const struct gkd_app_identity *);
#endif
