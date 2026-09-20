// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include "gkd-input-owner.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

#ifndef GKD_INPUT_ROOT
#define GKD_INPUT_ROOT "/dev/input"
#endif
#define GKD_INPUT_PHYSICAL_NAME "gpio-keys"
#define GKD_INPUT_VIRTUAL_NAME "GKD Mini Virtual Controls"
#ifndef GKD_INPUT_OWNER_DIR
#define GKD_INPUT_OWNER_DIR "/run/gkd-menu-owner"
#endif
#define GKD_INPUT_TEST_SESSION GKD_INPUT_OWNER_DIR "/test-session"
#define GKD_INPUT_VIRTUAL_GRAB GKD_INPUT_OWNER_DIR "/virtual-grabbed"
#define GKD_INPUT_VIRTUAL_VENDOR 0x474bU
#define GKD_INPUT_VIRTUAL_PRODUCT 0x0281U
#define BITS_PER_LONG (sizeof(unsigned long) * 8U)
#define BIT_WORD(value) ((value) / BITS_PER_LONG)
#define BIT_MASK(value) (1UL << ((value) % BITS_PER_LONG))

static int secure_directory(void)
{
	struct stat state;
	return lstat(GKD_INPUT_OWNER_DIR, &state) == 0 &&
		S_ISDIR(state.st_mode) && state.st_uid == 0 && state.st_gid == 0 &&
		state.st_nlink >= 2 && (state.st_mode & 07777U) == 0700U ? 0 : -1;
}

static int secure_test_session(void)
{
	struct stat state;
	return secure_directory() == 0 &&
		lstat(GKD_INPUT_TEST_SESSION, &state) == 0 &&
		S_ISREG(state.st_mode) && state.st_uid == 0 && state.st_gid == 0 &&
		state.st_nlink == 1 && (state.st_mode & 07777U) == 0600U &&
		state.st_size == 0 ? 0 : -1;
}

static int process_start(unsigned long long *start)
{
	char value[1024], *cursor, *token, *save = NULL;
	FILE *file = fopen("/proc/self/stat", "r");
	int field = 3;
	if (!file || !fgets(value, sizeof(value), file)) {
		if (file) (void)fclose(file);
		return -1;
	}
	if (fclose(file) != 0) return -1;
	cursor = strrchr(value, ')');
	if (!cursor) return -1;
	token = strtok_r(cursor + 2, " ", &save);
	while (token) {
		if (field == 22) {
			char *end = NULL;
			errno = 0;
			*start = strtoull(token, &end, 10);
			return !errno && *start && end &&
				(*end == '\0' || *end == '\n') ? 0 : -1;
		}
		token = strtok_r(NULL, " ", &save);
		++field;
	}
	return -1;
}

static int safe_remove_marker(void)
{
	struct stat state;
	if (lstat(GKD_INPUT_VIRTUAL_GRAB, &state) < 0)
		return errno == ENOENT ? 0 : -1;
	if (!S_ISREG(state.st_mode) || state.st_uid != 0 || state.st_gid != 0 ||
	    state.st_nlink != 1 || (state.st_mode & 07777U) != 0600U) {
		errno = EPERM;
		return -1;
	}
	return unlink(GKD_INPUT_VIRTUAL_GRAB);
}

static int publish_marker(void)
{
	char payload[128], readback[128];
	unsigned long long start;
	struct stat state;
	ssize_t length, got;
	int fd = -1;
	if (secure_test_session() < 0 || process_start(&start) < 0 ||
	    safe_remove_marker() < 0)
		return -1;
	length = snprintf(payload, sizeof(payload), "%ld %llu %s\n",
		(long)getpid(), start, GKD_INPUT_VIRTUAL_NAME);
	if (length <= 0 || length >= (ssize_t)sizeof(payload)) return -1;
	fd = open(GKD_INPUT_VIRTUAL_GRAB,
		O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0 || write(fd, payload, (size_t)length) != length ||
	    fsync(fd) < 0 || close(fd) < 0) {
		if (fd >= 0) (void)close(fd);
		(void)safe_remove_marker();
		return -1;
	}
	fd = -1;
	if (lstat(GKD_INPUT_VIRTUAL_GRAB, &state) < 0 ||
	    !S_ISREG(state.st_mode) || state.st_uid != 0 || state.st_gid != 0 ||
	    state.st_nlink != 1 || (state.st_mode & 07777U) != 0600U ||
	    state.st_size != length)
		goto fail;
	fd = open(GKD_INPUT_VIRTUAL_GRAB, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) goto fail;
	got = read(fd, readback, sizeof(readback));
	if (close(fd) < 0 || got != length || memcmp(readback, payload, (size_t)length)) {
		fd = -1;
		goto fail;
	}
	return 0;
fail:
	if (fd >= 0) (void)close(fd);
	(void)safe_remove_marker();
	return -1;
}

static int required_keys(int fd)
{
	unsigned long event_bits[(EV_MAX + BITS_PER_LONG) / BITS_PER_LONG];
	unsigned long key_bits[(KEY_MAX + BITS_PER_LONG) / BITS_PER_LONG];
	static const unsigned int keys[] = {
		KEY_UP, KEY_DOWN, KEY_LEFTCTRL, KEY_LEFTALT
	};
	unsigned int index;
	memset(event_bits, 0, sizeof(event_bits));
	memset(key_bits, 0, sizeof(key_bits));
	if (ioctl(fd, EVIOCGBIT(0, sizeof(event_bits)), event_bits) < 0 ||
	    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0 ||
	    !(event_bits[BIT_WORD(EV_KEY)] & BIT_MASK(EV_KEY)))
		return -1;
	for (index = 0; index < sizeof(keys) / sizeof(keys[0]); ++index)
		if (!(key_bits[BIT_WORD(keys[index])] & BIT_MASK(keys[index])))
			return -1;
	return 0;
}

static int matching_device(const char *path, const char *expected, int virtual)
{
	char name[128] = {0};
	struct input_id identity;
	struct stat state;
	int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
	if (fd < 0) return -1;
	memset(&identity, 0, sizeof(identity));
	if (fstat(fd, &state) < 0 || !S_ISCHR(state.st_mode) ||
	    ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0 ||
	    strcmp(name, expected) || required_keys(fd) < 0 ||
	    (virtual && (ioctl(fd, EVIOCGID, &identity) < 0 ||
		identity.bustype != BUS_VIRTUAL ||
		identity.vendor != GKD_INPUT_VIRTUAL_VENDOR ||
		identity.product != GKD_INPUT_VIRTUAL_PRODUCT))) {
		(void)close(fd);
		return -1;
	}
	return fd;
}

static int discover_devices(struct gkd_input_owner *owner)
{
	DIR *directory = opendir(GKD_INPUT_ROOT);
	struct dirent *entry;
	int physical_count = 0, virtual_count = 0;
	if (!directory) return -1;
	while ((entry = readdir(directory))) {
		char path[256];
		int fd;
		if (strncmp(entry->d_name, "event", 5)) continue;
		if (snprintf(path, sizeof(path), "%s/%s", GKD_INPUT_ROOT,
			entry->d_name) >= (int)sizeof(path))
			continue;
		fd = matching_device(path, GKD_INPUT_PHYSICAL_NAME, 0);
		if (fd >= 0) {
			++physical_count;
			if (owner->physical_fd >= 0) (void)close(owner->physical_fd);
			owner->physical_fd = fd;
			continue;
		}
		fd = matching_device(path, GKD_INPUT_VIRTUAL_NAME, 1);
		if (fd >= 0) {
			++virtual_count;
			if (owner->virtual_fd >= 0) (void)close(owner->virtual_fd);
			owner->virtual_fd = fd;
		}
	}
	(void)closedir(directory);
	return physical_count == 1 && virtual_count == 1 ? 0 : -1;
}

static void close_devices(struct gkd_input_owner *owner)
{
	if (owner->virtual_fd >= 0) {
		(void)close(owner->virtual_fd);
		owner->virtual_fd = -1;
	}
	if (owner->physical_fd >= 0) {
		(void)close(owner->physical_fd);
		owner->physical_fd = -1;
	}
}

static int owner_is_idle(const struct gkd_input_owner *owner)
{
	return owner && owner->physical_fd == -1 && owner->virtual_fd == -1 &&
		!owner->physical_grabbed && !owner->virtual_grabbed &&
		!owner->marker_published && owner->menu_guard.lease_fd == -1;
}

static int acquire_owner(struct gkd_input_owner *owner, int test_marker,
	struct gkd_menu_guard_owner *guard)
{
	unsigned int attempt;
	if (!owner_is_idle(owner)) { errno = EINVAL; return -1; }
	if (guard && (test_marker || guard->lease_fd < 0)) { errno = EINVAL; return -1; }
	if (test_marker && secure_test_session() < 0) return -1;
	for (attempt = 0; attempt < 50U; ++attempt) {
		if (discover_devices(owner) == 0) break;
		close_devices(owner);
		(void)usleep(20000U);
	}
	if (attempt == 50U || (!guard &&
	    gkd_menu_guard_owner_enter(&owner->menu_guard) < 0) ||
	    ioctl(owner->physical_fd, EVIOCGRAB, 1) < 0) {
		int saved = errno;
		(void)gkd_input_owner_close(owner);
		errno = saved;
		return -1;
	}
	owner->physical_grabbed = 1;
	if (ioctl(owner->virtual_fd, EVIOCGRAB, 1) < 0) {
		int saved = errno;
		(void)gkd_input_owner_close(owner);
		errno = saved;
		return -1;
	}
	owner->virtual_grabbed = 1;
	if (test_marker && publish_marker() < 0) {
		int saved = errno;
		(void)gkd_input_owner_close(owner);
		errno = saved;
		return -1;
	}
	owner->marker_published = test_marker;
	return 0;
}

void gkd_input_owner_init(struct gkd_input_owner *owner)
{
	owner->physical_fd = -1;
	owner->virtual_fd = -1;
	owner->physical_grabbed = 0;
	owner->virtual_grabbed = 0;
	owner->marker_published = 0;
	gkd_menu_guard_owner_init(&owner->menu_guard);
}

int gkd_input_observer_open(struct gkd_input_owner *owner)
{
    if (!owner || owner->physical_fd != -1 || owner->virtual_fd != -1 ||
        owner->marker_published) { errno = EINVAL; return -1; }
    if (discover_devices(owner)) { close_devices(owner); return -1; }
    return 0;
}

void gkd_input_observer_close(struct gkd_input_owner *owner)
{
    if (owner) close_devices(owner);
}

int gkd_input_owner_open(struct gkd_input_owner *owner)
{
	return acquire_owner(owner, 1, NULL);
}

int gkd_input_owner_open_menu(struct gkd_input_owner *owner)
{
	return acquire_owner(owner, 0, NULL);
}

int gkd_input_owner_open_menu_reuse(struct gkd_input_owner *owner,
	struct gkd_menu_guard_owner *guard)
{
	return acquire_owner(owner, 0, guard);
}

int gkd_input_owner_transfer_menu_guard(struct gkd_input_owner *owner,
	struct gkd_menu_guard_owner *guard)
{
	if (!owner || !guard || owner->physical_fd < 0 || owner->virtual_fd < 0 ||
		!owner->physical_grabbed || !owner->virtual_grabbed ||
		owner->marker_published || owner->menu_guard.lease_fd >= 0 ||
		guard->lease_fd < 0) { errno = EINVAL; return -1; }
	owner->menu_guard.lease_fd = guard->lease_fd;
	guard->lease_fd = -1;
	return 0;
}

static long long input_now_ms(void)
{
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) return -1;
	return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int gkd_input_owner_next_key_timeout(struct gkd_input_owner *owner, int timeout_ms)
{
	struct pollfd pollers[2] = {
		{owner->physical_fd, POLLIN, 0},
		{owner->virtual_fd, POLLIN, 0},
	};
	struct input_event events[16];
	long long deadline = -1;
	if (timeout_ms < -1) { errno = EINVAL; return -1; }
	if (timeout_ms >= 0) {
		deadline = input_now_ms();
		if (deadline < 0) return -1;
		deadline += timeout_ms;
	}
	for (;;) {
		unsigned int source;
		int remaining = -1, ready;
		if (deadline >= 0) {
			long long now = input_now_ms();
			if (now < 0) return -1;
			remaining = now >= deadline ? 0 : (int)(deadline - now);
		}
		ready = poll(pollers, 2, remaining);
		if (ready < 0 && errno == EINTR) continue;
		if (ready == 0) return 0;
		if (ready < 0) return -1;
		for (source = 0; source < 2; ++source) {
			ssize_t count;
			unsigned int index;
			if (pollers[source].revents & (POLLERR | POLLHUP | POLLNVAL))
				return -1;
			if (!(pollers[source].revents & POLLIN)) continue;
			count = read(pollers[source].fd, events, sizeof(events));
			if (count < 0 && (errno == EAGAIN || errno == EINTR)) continue;
			if (count <= 0 || count % (ssize_t)sizeof(events[0])) return -1;
			for (index = 0;
			     index < (unsigned int)(count / (ssize_t)sizeof(events[0]));
			     ++index)
				if (events[index].type == EV_KEY && events[index].value == 1)
					return events[index].code;
		}
	}
}

int gkd_input_owner_next_key(struct gkd_input_owner *owner)
{
	return gkd_input_owner_next_key_timeout(owner, -1);
}

int gkd_input_owner_close(struct gkd_input_owner *owner)
{
	int result = 0;
	if (!owner) return -1;
	if (owner->marker_published && safe_remove_marker() < 0) result = -1;
	owner->marker_published = 0;
	if (owner->virtual_fd >= 0) {
		if (owner->virtual_grabbed && ioctl(owner->virtual_fd, EVIOCGRAB, 0) < 0) result = -1;
		if (close(owner->virtual_fd) < 0) result = -1;
		owner->virtual_fd = -1;
		owner->virtual_grabbed = 0;
	}
	if (owner->physical_fd >= 0) {
		if (owner->physical_grabbed && ioctl(owner->physical_fd, EVIOCGRAB, 0) < 0) result = -1;
		if (close(owner->physical_fd) < 0) result = -1;
		owner->physical_fd = -1;
		owner->physical_grabbed = 0;
	}
	gkd_menu_guard_owner_exit(&owner->menu_guard);
	return result;
}
