#define _GNU_SOURCE
#include "gkd-app-battery.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_text(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(fd >= 0);
    assert(write(fd, text, strlen(text)) == (ssize_t)strlen(text));
    assert(close(fd) == 0);
}

static void make_dir(const char *path) { assert(mkdir(path, 0700) == 0); }

int main(void)
{
    char root[] = GKD_APP_BATTERY_SYSFS_ROOT;
    char led_root[] = GKD_APP_BATTERY_LED_ROOT;
    char path[512];
    struct gkd_app_battery battery;
    const unsigned thresholds[3] = {25U, 50U, 75U};
    const unsigned curve[5] = {3300U, 3549U, 3645U, 3845U, 4200U};
    char value[8];

    assert(mkdir(root, 0700) == 0);
    snprintf(path, sizeof(path), "%s/battery", root); make_dir(path);
    snprintf(path, sizeof(path), "%s/usb", root); make_dir(path);
    assert(mkdir(led_root, 0700) == 0);
    snprintf(path, sizeof(path), "%s/battery/voltage_now", root); write_text(path, "3700000\n");
    snprintf(path, sizeof(path), "%s/usb/online", root); write_text(path, "1\n");
    assert(gkd_app_battery_read(&battery, curve) == 0);
    assert(battery.percent > 50 && battery.percent < 75 && battery.voltage_mv == 3700 && battery.external_power == 1);
    snprintf(path, sizeof(path), "%s/battery/voltage_now", root); write_text(path, "5000001\n");
    errno = 0; assert(gkd_app_battery_read(&battery, curve) < 0 && errno == EINVAL);
    write_text(path, "3700x\n"); errno = 0; assert(gkd_app_battery_read(&battery, curve) < 0 && errno == EINVAL);
    unlink(path); errno = 0; assert(gkd_app_battery_read(&battery, curve) < 0 && errno == ENOENT);
    write_text(path, "3700000\n");
    assert(gkd_app_battery_percent_from_voltage(3645, curve, &battery.percent) == 0 && battery.percent == 50);
    assert(gkd_app_battery_percent_from_voltage(3700, curve, &battery.percent) == 0 && battery.percent > 50 && battery.percent < 75);
    { const unsigned invalid[5] = {3300U, 3549U, 3549U, 3845U, 4200U};
      errno = 0; assert(gkd_app_battery_percent_from_voltage(3700, invalid, &battery.percent) < 0 && errno == EINVAL); }
    battery.percent = 50;
    for (unsigned i = 1; i <= 4; ++i) {
        snprintf(path, sizeof(path), "%s/gkd350:battery:%u", led_root, i); make_dir(path);
        snprintf(path, sizeof(path), "%s/gkd350:battery:%u/brightness", led_root, i); write_text(path, "0\n");
    }
    assert(gkd_app_battery_led(&battery, thresholds) == 0);
    for (unsigned i = 1; i <= 4; ++i) {
        snprintf(path, sizeof(path), "%s/gkd350:battery:%u/brightness", led_root, i);
        int fd = open(path, O_RDONLY); assert(fd >= 0); assert(read(fd, value, sizeof(value)) == 2); assert(close(fd) == 0);
        assert(value[0] == (i <= 3 ? '1' : '0'));
    }
    { const unsigned invalid[3] = {25U, 75U, 50U}; errno = 0;
      assert(gkd_app_battery_led(&battery, invalid) < 0 && errno == EINVAL); }
    for (unsigned i = 1; i <= 4; ++i) {
        snprintf(path, sizeof(path), "%s/gkd350:battery:%u/brightness", led_root, i); assert(unlink(path) == 0);
        snprintf(path, sizeof(path), "%s/gkd350:battery:%u", led_root, i); assert(rmdir(path) == 0);
    }
    assert(rmdir(led_root) == 0);
    snprintf(path, sizeof(path), "%s/battery/voltage_now", root); assert(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/usb/online", root); assert(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/battery", root); assert(rmdir(path) == 0);
    snprintf(path, sizeof(path), "%s/usb", root); assert(rmdir(path) == 0);
    assert(rmdir(root) == 0);
    puts("GKD_APP_BATTERY_FIXTURE=PASS");
    return 0;
}
