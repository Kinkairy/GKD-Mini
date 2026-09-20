/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_SERVICE_H
#define GKD_APP_SERVICE_H
#include <stdint.h>
#define GKD_APP_SERVICE_MAGIC 0x474b4131U
struct gkd_app_service_ready { uint32_t magic, version; int32_t host, init, application; };
#endif
