/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-controls-hardware.h"
#include "gkd-volume-curve.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int reject(int error) { errno = error; return -1; }
static int read_percent(int fd, int *value)
{
    char b[32]; ssize_t n; unsigned v = 0; ssize_t i;
    n = pread(fd, b, sizeof(b), 0);
    if (n < 0) return -1;
    if (n < 1 || n == (ssize_t)sizeof(b)) return reject(EPROTO);
    if (b[n - 1] == '\n') --n;
    if (!n) return reject(EPROTO);
    for (i = 0; i < n; ++i) {
        if (b[i] < '0' || b[i] > '9' || v > 100U) return reject(EPROTO);
        v = v * 10U + (unsigned)(b[i] - '0');
    }
    if (v > 100U) return reject(ERANGE);
    *value = (int)v; return 0;
}
static int pcm_info(struct gkd_controls_hardware *hw)
{
    struct snd_ctl_elem_info info;
    memset(&info, 0, sizeof(info)); info.id = hw->pcm;
    if (ioctl(hw->control_fd, SNDRV_CTL_IOCTL_ELEM_INFO, &info) < 0) return -1;
    if (info.type != SNDRV_CTL_ELEM_TYPE_INTEGER || info.count != 2U ||
        info.value.integer.min != 0 || info.value.integer.max != 192 ||
        (info.value.integer.step != 0 && info.value.integer.step != 1) ||
        (info.access & (SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_WRITE)) !=
        (SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_WRITE) ||
        info.access & SNDRV_CTL_ELEM_ACCESS_INACTIVE || !info.id.numid ||
        info.id.iface != SNDRV_CTL_ELEM_IFACE_MIXER || info.id.device ||
        info.id.subdevice || info.id.index ||
        strncmp((const char *)info.id.name, "PCM Playback Volume", sizeof(info.id.name)))
        return reject(EPROTO);
    hw->pcm = info.id; return 0;
}
static int raw_read(struct gkd_controls_hardware *hw, int *raw)
{
    struct snd_ctl_elem_value value; long left, right;
    if (pcm_info(hw)) return -1;
    memset(&value, 0, sizeof(value)); value.id = hw->pcm;
    if (ioctl(hw->control_fd, SNDRV_CTL_IOCTL_ELEM_READ, &value) < 0) return -1;
    left = value.value.integer.value[0]; right = value.value.integer.value[1];
    /* The accepted control is linked stereo. Do not silently erase an external balance. */
    if (left < 0 || left > 192 || right != left) return reject(ERANGE);
    *raw = (int)left; return 0;
}
static int raw_for(unsigned logical)
{
    /* Same nearest-integer conversion as Linux6.1 snd_mixer_oss_conv2,
     * now targeting the native ALSA control, without opening /dev/mixer. */
    return ((int)gkd_volume_curve[logical] * 192 + 50) / 100;
}
int gkd_controls_volume_read(struct gkd_controls_hardware *hw, int *logical)
{
    int raw, best = 0, distance = INT_MAX; unsigned i;
    if (!hw || !logical) return reject(EINVAL);
    if (raw_read(hw, &raw)) return -1;
    if (hw->logical_volume >= 0 && hw->logical_volume <= 100 &&
        hw->raw_volume == raw && raw_for((unsigned)hw->logical_volume) == raw) {
        *logical = hw->logical_volume; return 0;
    }
    for (i = 0; i <= 100U; ++i) {
        int d = raw_for(i) - raw; if (d < 0) d = -d;
        if (d < distance) { distance = d; best = (int)i; }
    }
    /* Many UI percentages map to one raw step. External changes choose the
     * lowest nearest logical value; successful own changes keep their exact hint. */
    hw->logical_volume = best; hw->raw_volume = raw; *logical = best; return 0;
}
int gkd_controls_brightness_read(struct gkd_controls_hardware *hw, int *percent)
{
    if (!hw || !percent) return reject(EINVAL);
    return read_percent(hw->brightness_fd, percent);
}
int gkd_controls_hardware_init(struct gkd_controls_hardware *hw, int ctl, int brightness, int maximum)
{
    struct snd_ctl_card_info card; int value;
    if (!hw || ctl < 0 || brightness < 0 || maximum < 0 || ctl == brightness ||
        ctl == maximum || brightness == maximum) return reject(EINVAL);
    memset(hw, 0, sizeof(*hw)); hw->control_fd = ctl; hw->brightness_fd = brightness;
    hw->logical_volume = -1; hw->raw_volume = -1;
    memset(&card, 0, sizeof(card));
    if (ioctl(ctl, SNDRV_CTL_IOCTL_CARD_INFO, &card) < 0) return -1;
    if (card.card != 0 || strncmp((const char *)card.id, "GCW0", sizeof(card.id))) return reject(ENODEV);
    hw->pcm.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
    memcpy(hw->pcm.name, "PCM Playback Volume", sizeof("PCM Playback Volume"));
    if (read_percent(maximum, &value)) return -1;
    if (value != 100) return reject(EPROTO);
    if (gkd_controls_volume_read(hw, &value) || gkd_controls_brightness_read(hw, &value)) return -1;
    return 0;
}
int gkd_controls_volume_set(struct gkd_controls_hardware *hw, unsigned target, int *logical)
{
    struct snd_ctl_elem_value value; int raw, current;
    if (!hw || !logical || target > 100U) return reject(EINVAL);
    if (gkd_controls_volume_read(hw, &current)) return -1;
    memset(&value, 0, sizeof(value)); value.id = hw->pcm;
    value.value.integer.value[0] = raw_for(target);
    value.value.integer.value[1] = value.value.integer.value[0];
    if (ioctl(hw->control_fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &value) < 0) return -1;
    if (raw_read(hw, &raw)) return -1;
    if (raw != raw_for(target)) { hw->logical_volume = -1; return reject(EIO); }
    hw->raw_volume = raw; hw->logical_volume = (int)target; *logical = (int)target; return 0;
}
int gkd_controls_volume_adjust(struct gkd_controls_hardware *hw, int delta, int *logical)
{
    int current, next;
    if (!hw || !logical || delta < -100 || delta > 100 || !delta) return reject(EINVAL);
    if (gkd_controls_volume_read(hw, &current)) return -1;
    next = current + delta; if (next < 0) next = 0; if (next > 100) next = 100;
    return gkd_controls_volume_set(hw, (unsigned)next, logical);
}
int gkd_controls_brightness_set(struct gkd_controls_hardware *hw, unsigned target, int *percent)
{
    int n, got; char text[16]; ssize_t wrote;
    if (!hw || !percent || !target || target > 100U) return reject(EINVAL);
    n = snprintf(text, sizeof(text), "%u\n", target);
    if (n < 1 || n >= (int)sizeof(text)) return reject(EOVERFLOW);
    wrote = pwrite(hw->brightness_fd, text, (size_t)n, 0);
    if (wrote < 0) return -1;
    if (wrote != n) return reject(EIO);
    if (read_percent(hw->brightness_fd, &got)) return -1;
    if (got != (int)target) return reject(EIO);
    *percent = got; return 0;
}
int gkd_controls_brightness_cycle(struct gkd_controls_hardware *hw, const unsigned *steps, unsigned count, int *percent)
{
    int current, next; unsigned i;
    if (!hw || !steps || !percent || count < 2U || count > 16U) return reject(EINVAL);
    for (i = 0; i < count; ++i)
        if (!steps[i] || steps[i] > 100U || (i && steps[i] <= steps[i - 1U])) return reject(EINVAL);
    if (gkd_controls_brightness_read(hw, &current)) return -1;
    next = (int)steps[0];
    for (i = 0; i < count; ++i) if ((int)steps[i] > current) { next = (int)steps[i]; break; }
    return gkd_controls_brightness_set(hw, (unsigned)next, percent);
}
