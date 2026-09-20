/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <sys/ioctl.h>
#include <sys/types.h>
#include <time.h>
#include "gkd-controls-hardware.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CONTROL_FD 10
#define BRIGHTNESS_FD 11
#define MAXIMUM_FD 12

struct mock_state {
	int raw;
	int write_raw;
	int ioctl_fail;
	int read_fail;
	int write_fail;
	int short_write;
	int readback_mismatch;
	int malformed_card;
	int malformed_control;
	int mismatched_stereo;
	unsigned int writes;
	const char *brightness;
	const char *maximum;
	char written[16];
	char readback[16];
};

static struct mock_state mock;

static void reset_mock(void)
{
	memset(&mock, 0, sizeof(mock));
	mock.raw = 96;
	mock.brightness = "50\n";
	mock.maximum = "100\n";
}

int __wrap_ioctl(int fd, unsigned long request, ...)
{
	va_list arguments;
	void *argument;
	va_start(arguments, request);
	argument = va_arg(arguments, void *);
	va_end(arguments);
	if (fd != CONTROL_FD || mock.ioctl_fail) { errno = EIO; return -1; }
	if (request == SNDRV_CTL_IOCTL_CARD_INFO) {
		struct snd_ctl_card_info *card = argument;
		memset(card, 0, sizeof(*card));
		card->card = mock.malformed_card ? 1 : 0;
		if (mock.malformed_card) memcpy(card->id, "BAD", 3U);
		else memcpy(card->id, "GCW0", 4U);
		return 0;
	}
	if (request == SNDRV_CTL_IOCTL_ELEM_INFO) {
		struct snd_ctl_elem_info *info = argument;
		info->type = mock.malformed_control ? SNDRV_CTL_ELEM_TYPE_BOOLEAN :
			SNDRV_CTL_ELEM_TYPE_INTEGER;
		info->count = mock.malformed_control ? 1U : 2U;
		info->value.integer.min = 0;
		info->value.integer.max = mock.malformed_control ? 191 : 192;
		info->value.integer.step = 1;
		info->access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_WRITE;
		info->id.numid = 1U;
		info->id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		memcpy(info->id.name, "PCM Playback Volume", 20U);
		return 0;
	}
	if (request == SNDRV_CTL_IOCTL_ELEM_READ) {
		struct snd_ctl_elem_value *value = argument;
		value->value.integer.value[0] = mock.raw;
		value->value.integer.value[1] = mock.mismatched_stereo ? mock.raw + 1 : mock.raw;
		return 0;
	}
	if (request == SNDRV_CTL_IOCTL_ELEM_WRITE) {
		struct snd_ctl_elem_value *value = argument;
		if (mock.write_fail) { errno = EIO; return -1; }
		++mock.writes;
		mock.write_raw = (int)value->value.integer.value[0];
		if (!mock.readback_mismatch) mock.raw = mock.write_raw;
		return 0;
	}
	errno = EINVAL;
	return -1;
}

ssize_t __wrap_pread(int fd, void *buffer, size_t length, off_t offset)
{
	const char *input;
	size_t input_length;
	(void)offset;
	if (mock.read_fail) { errno = EIO; return -1; }
	if (fd == BRIGHTNESS_FD) input = mock.brightness;
	else if (fd == MAXIMUM_FD) input = mock.maximum;
	else { errno = EBADF; return -1; }
	input_length = strlen(input);
	if (input_length > length) input_length = length;
	memcpy(buffer, input, input_length);
	return (ssize_t)input_length;
}

ssize_t __wrap___pread_chk(int fd, void *buffer, size_t length, off_t offset,
	size_t buffer_length)
{
	if (length > buffer_length) { errno = EOVERFLOW; return -1; }
	return __wrap_pread(fd, buffer, length, offset);
}

ssize_t __wrap_pwrite(int fd, const void *buffer, size_t length, off_t offset)
{
	const char *text = buffer;
	(void)offset;
	if (fd != BRIGHTNESS_FD) { errno = EBADF; return -1; }
	if (mock.write_fail) { errno = EIO; return -1; }
	if (length >= sizeof(mock.written)) { errno = EOVERFLOW; return -1; }
	memcpy(mock.written, text, length);
	mock.written[length] = '\0';
	if (mock.short_write) return (ssize_t)(length - 1U);
	if (!mock.readback_mismatch) {
		memcpy(mock.readback, mock.written, length + 1U);
		mock.brightness = mock.readback;
	}
	return (ssize_t)length;
}

#define CHECK(expression) do { \
	if (!(expression)) { \
		(void)fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		return -1; \
	} \
} while (0)

static int start(struct gkd_controls_hardware *hardware)
{
	return gkd_controls_hardware_init(hardware, CONTROL_FD, BRIGHTNESS_FD,
		MAXIMUM_FD);
}

static int startup_and_rejection(void)
{
	struct gkd_controls_hardware hardware;
	int result;
	reset_mock();
	CHECK(start(&hardware) == 0 && mock.writes == 0U && mock.written[0] == '\0');
	CHECK(gkd_controls_hardware_init(&hardware, CONTROL_FD, CONTROL_FD,
		MAXIMUM_FD) < 0);
	mock.malformed_card = 1;
	CHECK(start(&hardware) < 0);
	reset_mock(); mock.malformed_control = 1;
	CHECK(start(&hardware) < 0);
	reset_mock(); mock.maximum = "99\n";
	CHECK(start(&hardware) < 0);
	reset_mock(); mock.maximum = "100x\n";
	CHECK(start(&hardware) < 0);
	reset_mock(); mock.brightness = "101\n";
	CHECK(start(&hardware) < 0);
	reset_mock(); mock.raw = 193;
	CHECK(start(&hardware) < 0);
	reset_mock(); mock.mismatched_stereo = 1;
	CHECK(start(&hardware) < 0);
	reset_mock();
	CHECK(start(&hardware) == 0);
	CHECK(gkd_controls_volume_read(&hardware, NULL) < 0);
	CHECK(gkd_controls_brightness_read(&hardware, &result) == 0 && result == 50);
	return 0;
}

static int volume_mapping_and_fresh_reads(void)
{
	static const int expected_raw[101] = {
		0,73,90,102,108,113,119,123,127,129,132,134,136,138,140,142,144,
		146,148,150,150,152,154,154,156,156,157,157,159,159,161,161,161,
		163,163,165,165,165,167,167,169,169,169,171,171,171,173,173,173,
		173,175,175,175,175,177,177,177,177,179,179,179,179,180,180,180,
		180,180,182,182,182,182,182,182,184,184,184,184,184,186,186,186,
		186,186,186,186,188,188,188,188,188,188,190,190,190,190,190,190,
		190,190,192,192
	};
	struct gkd_controls_hardware hardware;
	unsigned int target;
	int result;
	for (target = 0U; target <= 100U; ++target) {
		unsigned int current = target == 0U ? 1U : target - 1U;
		reset_mock(); mock.raw = expected_raw[current];
		CHECK(start(&hardware) == 0);
		hardware.logical_volume = (int)current;
		hardware.raw_volume = expected_raw[current];
		CHECK(gkd_controls_volume_adjust(&hardware, target == 0U ? -1 : 1,
			&result) == 0 && result == (int)target);
		CHECK(mock.write_raw == expected_raw[target]);
	}
	reset_mock(); mock.raw = 150;
	CHECK(start(&hardware) == 0);
	CHECK(gkd_controls_volume_read(&hardware, &result) == 0 && result == 19);
	hardware.logical_volume = 20; hardware.raw_volume = 150;
	CHECK(gkd_controls_volume_adjust(&hardware, 1, &result) == 0 && result == 21);
	reset_mock(); mock.raw = 73;
	CHECK(start(&hardware) == 0);
	mock.raw = 192;
	CHECK(gkd_controls_volume_adjust(&hardware, -1, &result) == 0 && result == 98 &&
		mock.write_raw == 190);
	return 0;
}

static int explicit_restore(void)
{
    struct gkd_controls_hardware hardware; int value;
    reset_mock(); CHECK(start(&hardware) == 0);
    CHECK(gkd_controls_volume_set(&hardware, 72U, &value) == 0 && value == 72 && mock.raw == 182);
    CHECK(gkd_controls_volume_read(&hardware, &value) == 0 && value == 72);
    CHECK(gkd_controls_brightness_set(&hardware, 70U, &value) == 0 && value == 70);
    CHECK(gkd_controls_volume_set(&hardware, 101U, &value) < 0);
    CHECK(gkd_controls_brightness_set(&hardware, 0U, &value) < 0);
    mock.write_fail = 1;
    CHECK(gkd_controls_volume_set(&hardware, 50U, &value) < 0);
    CHECK(gkd_controls_brightness_set(&hardware, 50U, &value) < 0);
    return 0;
}

static int volume_failures(void)
{
	struct gkd_controls_hardware hardware;
	int result;
	reset_mock(); CHECK(start(&hardware) == 0);
	mock.ioctl_fail = 1;
	CHECK(gkd_controls_volume_adjust(&hardware, 1, &result) < 0 && mock.writes == 0U);
	reset_mock(); CHECK(start(&hardware) == 0);
	mock.write_fail = 1;
	CHECK(gkd_controls_volume_adjust(&hardware, 1, &result) < 0);
	reset_mock(); CHECK(start(&hardware) == 0);
	mock.readback_mismatch = 1;
	CHECK(gkd_controls_volume_adjust(&hardware, 1, &result) < 0 && hardware.logical_volume == -1);
	CHECK(gkd_controls_volume_adjust(&hardware, 0, &result) < 0);
	CHECK(gkd_controls_volume_adjust(&hardware, 101, &result) < 0);
	return 0;
}

static int brightness_paths(void)
{
	static const unsigned steps[] = {10U, 50U, 100U};
	static const unsigned bad_steps[] = {10U, 10U};
	struct gkd_controls_hardware hardware;
	int result;
	reset_mock(); CHECK(start(&hardware) == 0);
	CHECK(gkd_controls_brightness_cycle(&hardware, steps, 3U, &result) == 0 &&
		result == 100 && strcmp(mock.written, "100\n") == 0);
	mock.brightness = "100\n";
	CHECK(gkd_controls_brightness_cycle(&hardware, steps, 3U, &result) == 0 && result == 10);
	mock.short_write = 1;
	CHECK(gkd_controls_brightness_cycle(&hardware, steps, 3U, &result) < 0);
	mock.short_write = 0; mock.readback_mismatch = 1;
	CHECK(gkd_controls_brightness_cycle(&hardware, steps, 3U, &result) < 0);
	mock.readback_mismatch = 0; mock.read_fail = 1;
	CHECK(gkd_controls_brightness_cycle(&hardware, steps, 3U, &result) < 0);
	CHECK(gkd_controls_brightness_cycle(&hardware, bad_steps, 2U, &result) < 0);
	CHECK(gkd_controls_brightness_cycle(&hardware, steps, 1U, &result) < 0);
	mock.read_fail = 0; mock.brightness = "5x\n";
	CHECK(gkd_controls_brightness_read(&hardware, &result) < 0);
	mock.brightness = "";
	CHECK(gkd_controls_brightness_read(&hardware, &result) < 0);
	return 0;
}

int main(void)
{
	if (startup_and_rejection() || volume_mapping_and_fresh_reads() ||
	    explicit_restore() || volume_failures() || brightness_paths())
		return 1;
	return 0;
}
