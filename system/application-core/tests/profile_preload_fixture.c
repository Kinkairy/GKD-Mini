/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
__attribute__((constructor)) static void observed_preload(void)
{
    const char *marker = getenv("GKD_PROFILE_MARKER");
    if (!marker) _exit(120);
    int fd = open(marker, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0 || write(fd, "preload\n", 8) != 8) _exit(121);
    close(fd);
}
