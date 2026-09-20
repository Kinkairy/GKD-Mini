/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-hardware-state.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		(void)fprintf(stderr, "%s:%d: %s\\n", __FILE__, __LINE__, #expression); \
		return -1; \
	} \
} while (0)

static int write_file(int dirfd, const char *name, const char *text)
{
	int fd = openat(dirfd, name, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	size_t length = strlen(text);
	if (fd < 0) return -1;
	if (write(fd, text, length) != (ssize_t)length || close(fd) < 0) return -1;
	return 0;
}

static int read_file(int dirfd, const char *name, char *text, size_t capacity)
{
	int fd = openat(dirfd, name, O_RDONLY | O_CLOEXEC);
	ssize_t count;
	if (fd < 0) return -1;
	count = read(fd, text, capacity - 1U);
	if (close(fd) < 0 || count < 0) return -1;
	text[count] = '\0';
	return 0;
}

static int roundtrip_and_bounds(int dirfd)
{
	struct gkd_hardware_state state = {0U, 1U};
	char payload[128];
	CHECK(gkd_hardware_state_save(dirfd, &state) == 0);
	CHECK(read_file(dirfd, "hardware-state", payload, sizeof(payload)) == 0);
	CHECK(strcmp(payload, "version=1\nvolume_percent=0\nbrightness_percent=1\n") == 0);
	state.volume = 99U; state.brightness = 99U;
	CHECK(gkd_hardware_state_load(dirfd, &state) == 0 && state.volume == 0U &&
		state.brightness == 1U);
	state.volume = 100U; state.brightness = 100U;
	CHECK(gkd_hardware_state_save(dirfd, &state) == 0);
	memset(&state, 0, sizeof(state));
	CHECK(gkd_hardware_state_load(dirfd, &state) == 0 && state.volume == 100U &&
		state.brightness == 100U);
	state.volume = 101U;
	CHECK(gkd_hardware_state_save(dirfd, &state) < 0);
	return 0;
}

static int malformed_and_missing(int dirfd)
{
	struct gkd_hardware_state state;
	static const char *const invalid[] = {
		"version=2\nvolume_percent=1\nbrightness_percent=1\n",
		"volume_percent=1\nversion=1\nbrightness_percent=1\n",
		"version=1\nvolume_percent=01\nbrightness_percent=1\n",
		"version=1\nvolume_percent=100\nbrightness_percent=0\n",
		"version=1\nvolume_percent=1\nbrightness_percent=1\nextra\n"
	};
	unsigned int index;
	CHECK(unlinkat(dirfd, "hardware-state", 0) == 0);
	errno = 0;
	CHECK(gkd_hardware_state_load(dirfd, &state) < 0 && errno == ENOENT);
	for (index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
		CHECK(write_file(dirfd, "hardware-state", invalid[index]) == 0);
		CHECK(gkd_hardware_state_load(dirfd, &state) < 0);
	}
	return 0;
}

static int embedded_nul(int dirfd)
{
    struct gkd_hardware_state state = {50U, 70U};
    static const char payload[] = "version=1\nvolume_percent=50\nbrightness_percent=70\n\0hidden";
    int fd = openat(dirfd, "hardware-state", O_WRONLY | O_TRUNC);
    CHECK(fd >= 0);
    CHECK(write(fd, payload, sizeof(payload)-1U) == (ssize_t)sizeof(payload)-1);
    CHECK(close(fd) == 0);
    CHECK(gkd_hardware_state_load(dirfd, &state) < 0 && errno == EPROTO);
    CHECK(state.volume == 50U && state.brightness == 70U);
    CHECK(gkd_hardware_state_load(dirfd, NULL) < 0 && errno == EINVAL);
    return 0;
}

static int link_and_original_protection(int dirfd, const char *directory)
{
	struct gkd_hardware_state state = {50U, 50U};
	char outside[256], payload[128];
	int outside_fd;
	CHECK(snprintf(outside, sizeof(outside), "%s/outside", directory) > 0);
	outside_fd = open(outside, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	CHECK(outside_fd >= 0 && write(outside_fd, "original", 8U) == 8 && close(outside_fd) == 0);
	CHECK(unlinkat(dirfd, "hardware-state", 0) == 0);
	CHECK(symlinkat(outside, dirfd, "hardware-state") == 0);
	CHECK(gkd_hardware_state_load(dirfd, &state) < 0);
	CHECK(gkd_hardware_state_save(dirfd, &state) < 0);
	CHECK(read_file(AT_FDCWD, outside, payload, sizeof(payload)) == 0 &&
		strcmp(payload, "original") == 0);
	CHECK(unlinkat(dirfd, "hardware-state", 0) == 0);
	CHECK(write_file(dirfd, "other", "original") == 0);
	CHECK(linkat(dirfd, "other", dirfd, "hardware-state", 0) == 0);
	CHECK(gkd_hardware_state_save(dirfd, &state) < 0);
	CHECK(read_file(dirfd, "other", payload, sizeof(payload)) == 0 &&
		strcmp(payload, "original") == 0);
	return 0;
}

static int filesystem_failures(int dirfd, const char *directory)
{
	struct gkd_hardware_state state = {10U, 10U};
	int file;
	CHECK(unlinkat(dirfd, "hardware-state", 0) == 0);
	CHECK(chmod(directory, 0500) == 0);
	CHECK(gkd_hardware_state_save(dirfd, &state) < 0);
	CHECK(chmod(directory, 0700) == 0);
	file = openat(dirfd, "not-a-directory", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	CHECK(file >= 0);
	CHECK(gkd_hardware_state_load(file, &state) < 0);
	CHECK(close(file) == 0);
	return 0;
}

int main(void)
{
	char directory[] = "/tmp/gkd-hardware-state.XXXXXX";
	int dirfd;
	if (!mkdtemp(directory)) return 2;
	dirfd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dirfd < 0) return 2;
	if (roundtrip_and_bounds(dirfd) || malformed_and_missing(dirfd) ||
	    embedded_nul(dirfd) || link_and_original_protection(dirfd, directory) || filesystem_failures(dirfd, directory))
		return 1;
	(void)close(dirfd);
	return 0;
}
