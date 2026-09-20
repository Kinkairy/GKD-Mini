/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_DIAGNOSTICS_H
#define GKD_APP_DIAGNOSTICS_H
#include <stdint.h>
#define GKD_APP_DIAGNOSTICS_LIMIT 65536U
struct gkd_app_diagnostics {
    int reader, writer, file;
    int capture_errno, read_errno, eof;
    uint64_t retained, discarded;
};
#define GKD_APP_DIAGNOSTICS_INIT { .reader = -1, .writer = -1, .file = -1 }
/* One exclusive 0600 log in an existing owner-only RAM directory. The
 * controller owns file/reader; only writer enters the child namespace.
 * No implicit output destination and no persistent filesystem accepted. */
int gkd_app_diagnostics_open(struct gkd_app_diagnostics *, const char *directory);
/* At most 16KiB consumed per call. Retain a 64KiB prefix, drain/discard the
 * rest. Capture errors do not stop draining or influence readiness gates.
 * Writers are NONBLOCK: bursts can lose output before the reader sees it. */
void gkd_app_diagnostics_pump(struct gkd_app_diagnostics *);
/* Call only after the namespace has been reaped and parent writer closed.
 * Finite final drain, even for an unexpectedly retained writer. */
void gkd_app_diagnostics_finish(struct gkd_app_diagnostics *);
void gkd_app_diagnostics_close(struct gkd_app_diagnostics *);
#endif
