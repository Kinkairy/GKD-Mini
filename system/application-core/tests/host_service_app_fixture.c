/* SPDX-License-Identifier: GPL-2.0 */
/* Native-only long-lived application for host service lifecycle tests. */
#define _GNU_SOURCE
#include "gkd-app-ready.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) _exit(91); } while (0)

int main(void)
{
    unsigned char token[16];
    const char *hex = getenv("GKD_APP_READY_TOKEN");

    CHECK(getpid() > 1 && getppid() == 1);
    CHECK(hex != NULL && strlen(hex) == 32);
    for (unsigned i = 0; i < sizeof(token); ++i) {
        char byte[3] = {hex[i * 2], hex[i * 2 + 1], 0};
        char *end = NULL;
        long value;

        errno = 0;
        value = strtol(byte, &end, 16);
        CHECK(!errno && end == byte + 2 && value >= 0 && value <= 255);
        token[i] = (unsigned char)value;
    }
    CHECK(unsetenv("GKD_APP_READY_TOKEN") == 0);
    CHECK(fcntl(3, F_GETFD) >= 0);
    CHECK(gkd_app_ready_send(3, token) == 0);
    CHECK(close(3) == 0);
    for (;;) pause();
}
