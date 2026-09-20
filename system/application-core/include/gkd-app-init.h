/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_INIT_H
#define GKD_APP_INIT_H
#include <sys/types.h>

/* Borrow distinct CLOEXEC descriptors >=3. The caller pins the expected ELF,
 * validates its content, and supplies a trusted procfs in its own PID namespace.
 * Returns 1: exact live process image matches the pinned dev/inode; 0: it has
 * not matched; -1: observation/argument error with errno. No ownership transfer,
 * sleep, signal delivery, reap or sticky success. Call again to refresh evidence.
 * This does NOT prove init initialization, namespace provenance, content sealing,
 * or application readiness. A privileged process is outside this trust boundary.
 */
int gkd_app_init_image(int pidfd, pid_t pid, int executable_fd);
#endif
