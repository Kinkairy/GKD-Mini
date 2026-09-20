// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <stdarg.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int power_test_ioctl(int fd, unsigned long request, ...);
#define ioctl power_test_ioctl
#define GKD_R_POWER_TEST 1
#include "../source/gkd-r-poweroff.c"
#undef ioctl
static const char *fixture_root;
static unsigned writes;
static int bad_probe, bad_write;
const char *gkd_power_test_path(const char *path)
{
    static char full[4096];
    if (snprintf(full, sizeof(full), "%s%s", fixture_root, path) >= (int)sizeof(full)) abort();
    return full;
}
static int power_test_ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    struct i2c_rdwr_ioctl_data *transfer;
    struct i2c_msg *m;
    (void)fd;
    if (request != I2C_RDWR) return -1;
    va_start(ap, request); transfer = va_arg(ap, struct i2c_rdwr_ioctl_data *); va_end(ap);
    m = transfer->msgs;
    if (transfer->nmsgs == 2 && m[0].addr == 0x34 && m[0].len == 1 && m[1].len == 1) {
        unsigned reg = m[0].buf[0];
        m[1].buf[0] = reg == 0xb9 ? (uint8_t)(bad_probe ? 101 : 50) : (uint8_t)5;
        return 2;
    }
    if (transfer->nmsgs == 1 && m[0].addr == 0x34 && m[0].len == 2 &&
        m[0].buf[0] == 0x32 && m[0].buf[1] == 0x85) {
        ++writes; return bad_write ? -1 : 1;
    }
    abort();
}
int main(int argc, char **argv)
{
    unsigned expected;
    if (argc != 5) return 64;
    fixture_root = argv[1]; expected = (unsigned)atoi(argv[2]);
    bad_probe = atoi(argv[3]); bad_write = atoi(argv[4]);
    if (gkd_r_poweroff() != -1 || writes != expected) return 1;
    printf("R_POWER_FIXTURE_PASS writes=%u\n", writes);
    return 0;
}
