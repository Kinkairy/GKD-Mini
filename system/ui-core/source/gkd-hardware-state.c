/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-hardware-state.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define GKD_HARDWARE_STATE_NAME "hardware-state"
#define GKD_HARDWARE_STATE_LIMIT 127U

static unsigned long temporary_sequence;

static int fail(int error)
{
	errno = error;
	return -1;
}

static int trusted_directory(int dirfd)
{
	struct stat state;
	if (fstat(dirfd, &state) < 0) return -1;
	if (!S_ISDIR(state.st_mode) || state.st_uid != geteuid() ||
	    (state.st_mode & 0022U) != 0U)
		return fail(EPERM);
	return 0;
}

static int trusted_file(const struct stat *state)
{
	if (!S_ISREG(state->st_mode) || state->st_uid != geteuid() ||
	    state->st_nlink != 1 || (state->st_mode & 0022U) != 0U ||
	    state->st_size < 0 || (uintmax_t)state->st_size > GKD_HARDWARE_STATE_LIMIT)
		return fail(EPERM);
	return 0;
}

static int parse_value(const char **cursor, const char *name, unsigned int *value)
{
	const char *text = *cursor;
	unsigned int result = 0U;
	size_t index = 0U;
	if (strncmp(text, name, strlen(name)) != 0) return fail(EPROTO);
	text += strlen(name);
	if (*text < '0' || *text > '9') return fail(EPROTO);
	if (*text == '0' && text[1] != '\n') return fail(EPROTO);
	while (*text >= '0' && *text <= '9') {
		unsigned int digit = (unsigned int)(*text - '0');
		if (index++ >= 3U || result > (UINT_MAX - digit) / 10U)
			return fail(ERANGE);
		result = result * 10U + digit;
		++text;
	}
	if (*text != '\n') return fail(EPROTO);
	*cursor = text + 1;
	*value = result;
	return 0;
}

static int parse_payload(char *payload, struct gkd_hardware_state *state)
{
	const char *cursor = payload;
	unsigned int version, volume, brightness;
	if (parse_value(&cursor, "version=", &version) < 0 ||
	    parse_value(&cursor, "volume_percent=", &volume) < 0 ||
	    parse_value(&cursor, "brightness_percent=", &brightness) < 0)
		return -1;
	if (*cursor != '\0') return fail(EPROTO);
	if (version != 1U || volume > 100U || brightness == 0U || brightness > 100U)
		return fail(ERANGE);
	state->volume = volume;
	state->brightness = brightness;
	return 0;
}

int gkd_hardware_state_load(int dirfd, struct gkd_hardware_state *state)
{
	char payload[GKD_HARDWARE_STATE_LIMIT + 1U];
	struct stat metadata;
	ssize_t length;
	int fd, saved_errno;
	if (!state) return fail(EINVAL);
	if (trusted_directory(dirfd) < 0) return -1;
	fd = openat(dirfd, GKD_HARDWARE_STATE_NAME,
		O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0) return -1;
	if (fstat(fd, &metadata) < 0 || trusted_file(&metadata) < 0) {
		saved_errno = errno;
		(void)close(fd);
		errno = saved_errno;
		return -1;
	}
	length = read(fd, payload, sizeof(payload));
	saved_errno = errno;
	if (close(fd) < 0 && length >= 0) saved_errno = errno;
	if (length < 0 || (uintmax_t)length != (uintmax_t)metadata.st_size ||
	    (size_t)length > GKD_HARDWARE_STATE_LIMIT) {
		errno = length < 0 ? saved_errno : EPROTO;
		return -1;
	}
	if (memchr(payload, '\0', (size_t)length)) return fail(EPROTO);
	payload[length] = '\0';
	return parse_payload(payload, state);
}

int gkd_hardware_state_save(int dirfd, const struct gkd_hardware_state *state)
{
	char payload[80], temporary[80];
	struct stat existing;
	int fd = -1, length, saved_errno;
	ssize_t written;
	unsigned int attempt;
	if (!state || state->volume > 100U || state->brightness == 0U ||
	    state->brightness > 100U)
		return fail(EINVAL);
	if (trusted_directory(dirfd) < 0) return -1;
	length = snprintf(payload, sizeof(payload),
		"version=1\nvolume_percent=%u\nbrightness_percent=%u\n",
		state->volume, state->brightness);
	if (length <= 0 || (size_t)length > GKD_HARDWARE_STATE_LIMIT ||
	    length >= (int)sizeof(payload))
		return fail(EOVERFLOW);
	if (fstatat(dirfd, GKD_HARDWARE_STATE_NAME, &existing, AT_SYMLINK_NOFOLLOW) == 0) {
		if (trusted_file(&existing) < 0) return -1;
	} else if (errno != ENOENT) {
		return -1;
	}
	for (attempt = 0U; attempt < 32U; ++attempt) {
		unsigned long sequence = ++temporary_sequence;
		if (snprintf(temporary, sizeof(temporary), ".hardware-state.%ld.%lu",
			(long)getpid(), sequence) >= (int)sizeof(temporary))
			return fail(EOVERFLOW);
		fd = openat(dirfd, temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC |
			O_NOFOLLOW | O_NONBLOCK, 0600);
		if (fd >= 0) break;
		if (errno != EEXIST) return -1;
	}
	if (fd < 0) return fail(EEXIST);
	written = write(fd, payload, (size_t)length);
	if (written != length) {
		saved_errno = written < 0 ? errno : EIO;
		(void)close(fd);
		(void)unlinkat(dirfd, temporary, 0);
		errno = saved_errno;
		return -1;
	}
	if (fsync(fd) < 0) {
		saved_errno = errno;
		(void)close(fd);
		(void)unlinkat(dirfd, temporary, 0);
		errno = saved_errno;
		return -1;
	}
	if (close(fd) < 0) {
		saved_errno = errno;
		fd = -1;
		(void)unlinkat(dirfd, temporary, 0);
		errno = saved_errno;
		return -1;
	}
	fd = -1;
	if (renameat(dirfd, temporary, dirfd, GKD_HARDWARE_STATE_NAME) < 0) {
		saved_errno = errno;
		(void)unlinkat(dirfd, temporary, 0);
		errno = saved_errno;
		return -1;
	}
	return fsync(dirfd);
}
