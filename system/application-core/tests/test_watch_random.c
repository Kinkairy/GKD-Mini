/* SPDX-License-Identifier: GPL-2.0 */
/* Linker-only syscall fault injection, never linked into runtime artifacts. */
#define _GNU_SOURCE
#include "gkd-app-watch.h"
#include <errno.h>
#include <linux/random.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "random line=%d\n", __LINE__); exit(1); } } while (0)
static unsigned int mode;
long __wrap_syscall(long number, ...)
{
    va_list values; va_start(values, number);
    CHECK(number == SYS_getrandom);
    void *data = va_arg(values, void *);
    size_t bytes = va_arg(values, size_t);
    unsigned int flags = va_arg(values, unsigned int);
    va_end(values);
    CHECK(bytes == 16 && flags == GRND_NONBLOCK);
    if (mode < 2) { errno = mode ? EINTR : EAGAIN; return -1; }
    memset(data, mode == 3 ? 0 : 1, bytes);
    return mode == 2 ? 8 : 16;
}
int main(void)
{
    unsigned char token[16], zero[16] = {0};
    for (mode = 0; mode < 4; ++mode) {
        memset(token, 0xa5, sizeof(token));
        CHECK(gkd_app_launch_token(token) == -1 && !memcmp(token, zero, sizeof(token)));
        CHECK(errno == (mode == 0 ? EAGAIN : mode == 1 ? EINTR : EIO));
    }
    mode = 4;
    CHECK(gkd_app_launch_token(token) == 0);
    for (size_t i = 0; i < sizeof(token); ++i) CHECK(token[i] == 1);
    puts("GKD_APP_TOKEN_FAULTS=PASS cases=5 failure-clears-output=PASS nonblocking=PASS");
    return 0;
}
