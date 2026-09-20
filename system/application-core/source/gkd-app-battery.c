#include "gkd-app-battery.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef GKD_APP_BATTERY_SYSFS_ROOT
#define GKD_APP_BATTERY_SYSFS_ROOT "/sys/class/power_supply"
#endif

#ifndef GKD_APP_BATTERY_LED_ROOT
#define GKD_APP_BATTERY_LED_ROOT "/sys/class/leds"
#endif

static int read_uint_file(const char *path, unsigned maximum, unsigned *out)
{
    char text[64], *end = NULL;
    ssize_t count;
    unsigned long value;
    int fd;

    if (!path || !out) { errno = EINVAL; return -1; }
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    count = read(fd, text, sizeof(text) - 1U);
    if (close(fd) < 0 || count <= 0 || count >= (ssize_t)sizeof(text)) {
        if (count >= 0) errno = EIO;
        return -1;
    }
    text[count] = '\0';
    if (text[count - 1] == '\n') text[--count] = '\0';
    if (count == 0 || (count > 1 && text[0] == '0')) { errno = EINVAL; return -1; }
    for (ssize_t i = 0; i < count; ++i)
        if (text[i] < '0' || text[i] > '9') { errno = EINVAL; return -1; }
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || end != text + count || value > maximum) { errno = EINVAL; return -1; }
    *out = (unsigned)value;
    return 0;
}

static int sysfs_path(char *out, size_t bytes, const char *name, const char *attribute)
{
    int length = snprintf(out, bytes, "%s/%s/%s", GKD_APP_BATTERY_SYSFS_ROOT,
                          name, attribute);
    if (length < 0 || (size_t)length >= bytes) { errno = ENAMETOOLONG; return -1; }
    return 0;
}

int gkd_app_battery_read(struct gkd_app_battery *out,
                         const unsigned curve_mv[5])
{
    char path[160];
    unsigned voltage_uv, online;

    if (!out || !curve_mv) { errno = EINVAL; return -1; }
    memset(out, 0, sizeof(*out));
    if (sysfs_path(path, sizeof(path), "battery", "voltage_now") ||
        read_uint_file(path, 5000000U, &voltage_uv) ||
        voltage_uv < 2500000U ||
        sysfs_path(path, sizeof(path), "usb", "online") ||
        read_uint_file(path, 1U, &online)) {
        if (!errno) errno = EIO;
        return -1;
    }
    out->external_power = (int)online;
    out->voltage_mv = (int)(voltage_uv / 1000U);
    return gkd_app_battery_percent_from_voltage(out->voltage_mv, curve_mv,
                                                 &out->percent);
}

int gkd_app_battery_percent_from_voltage(int voltage_mv,
                                         const unsigned curve_mv[5], int *percent)
{
    unsigned index;
    if (!curve_mv || !percent || voltage_mv < 0) { errno = EINVAL; return -1; }
    for (index = 0; index < 5U; ++index)
        if (curve_mv[index] < 2800U || curve_mv[index] > 4500U ||
            (index && curve_mv[index] <= curve_mv[index - 1U])) {
            errno = EINVAL; return -1;
        }
    if ((unsigned)voltage_mv <= curve_mv[0]) { *percent = 0; return 0; }
    if ((unsigned)voltage_mv >= curve_mv[4]) { *percent = 100; return 0; }
    for (index = 1; index < 5U; ++index) {
        if ((unsigned)voltage_mv <= curve_mv[index]) {
            unsigned low = curve_mv[index - 1U], high = curve_mv[index];
            unsigned base = (index - 1U) * 25U;
            *percent = (int)(base + ((unsigned)(voltage_mv - (int)low) * 25U +
                                      (high - low) / 2U) / (high - low));
            return 0;
        }
    }
    errno = EIO;
    return -1;
}

int gkd_app_battery_led(const struct gkd_app_battery *battery,
                        const unsigned thresholds[3])
{
    unsigned segments = 1U, index;
    char path[192];
    static const char *const led_names[4] = {
        "gkd350:battery:1", "gkd350:battery:2",
        "gkd350:battery:3", "gkd350:battery:4",
    };

    if (!battery || !thresholds || battery->percent < 0 || battery->percent > 100) {
        errno = EINVAL; return -1;
    }
    for (index = 0; index < 3U; ++index)
        if (thresholds[index] < 1U || thresholds[index] > 99U ||
            (index && thresholds[index] <= thresholds[index - 1U])) {
            errno = EINVAL; return -1;
        }
    for (index = 0; index < 3U; ++index)
        if ((unsigned)battery->percent >= thresholds[index]) ++segments;
    for (index = 0; index < 4U; ++index) {
        int length = snprintf(path, sizeof(path), "%s/%s/brightness",
                              GKD_APP_BATTERY_LED_ROOT, led_names[index]);
        int fd;
        if (length < 0 || (size_t)length >= sizeof(path)) { errno = ENAMETOOLONG; return -1; }
        fd = open(path, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) return -1;
        if (write(fd, index < segments ? "1\n" : "0\n", 2) != 2) {
            int saved = errno; (void)close(fd); errno = saved; return -1;
        }
        if (close(fd) < 0) return -1;
    }
    return 0;
}
