/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-menu-guard.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef GKD_MENU_GUARD_DIR
#define GKD_MENU_GUARD_DIR "/run/gkd-menu-owner"
#endif
#ifndef GKD_MENU_GUARD_LEASE
#define GKD_MENU_GUARD_LEASE GKD_MENU_GUARD_DIR "/lease"
#endif
#ifndef GKD_MENU_GUARD_EPOCH
#define GKD_MENU_GUARD_EPOCH GKD_MENU_GUARD_DIR "/epoch"
#endif

static int secure_state(const struct stat *state)
{
	return S_ISREG(state->st_mode) && state->st_uid == geteuid() &&
		state->st_gid == getegid() && state->st_nlink == 1 &&
		(state->st_mode & 07777U) == 0600U;
}

static int secure_directory(void)
{
	struct stat state;
	if (mkdir(GKD_MENU_GUARD_DIR, 0700) < 0 && errno != EEXIST)
		return -1;
	if (lstat(GKD_MENU_GUARD_DIR, &state) < 0 || !S_ISDIR(state.st_mode) ||
	    state.st_uid != geteuid() || state.st_gid != getegid() ||
	    state.st_nlink < 2 || (state.st_mode & 07777U) != 0700U) {
		errno = EPERM;
		return -1;
	}
	return 0;
}

static int open_checked(const char *path)
{
	struct stat state;
	int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK,
		0600);
	if (fd < 0) return -1;
	if (fstat(fd, &state) < 0 || !secure_state(&state)) {
		(void)close(fd);
		errno = EPERM;
		return -1;
	}
	return fd;
}

static int open_lease(int operation)
{
	int fd;
	if (secure_directory() < 0) return -1;
	fd = open_checked(GKD_MENU_GUARD_LEASE);
	if (fd < 0) return -1;
	if (flock(fd, operation | LOCK_NB) < 0) {
		(void)close(fd);
		return -1;
	}
	return fd;
}

static int parse_epoch(const char data[static 17], uint64_t *epoch)
{
	unsigned int index;
	uint64_t value = 0;
	for (index = 0; index != 16U; ++index) {
		unsigned char character = (unsigned char)data[index];
		unsigned int digit;
		if (character >= '0' && character <= '9') digit = character - '0';
		else if (character >= 'a' && character <= 'f') digit = character - 'a' + 10U;
		else { errno = EINVAL; return -1; }
		value = (value << 4) | digit;
	}
	if (data[16] != '\n') { errno = EINVAL; return -1; }
	*epoch = value;
	return 0;
}

static int write_epoch(uint64_t epoch, int exclusive)
{
	char data[18];
	struct stat state;
	int fd, length;
	if (!exclusive) { errno = EPERM; return -1; }
	length = snprintf(data, sizeof(data), "%016llx\n", (unsigned long long)epoch);
	if (length != 17) { errno = EOVERFLOW; return -1; }
	fd = open(GKD_MENU_GUARD_EPOCH,
		O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
	if (fd < 0 && errno == EEXIST)
		fd = open(GKD_MENU_GUARD_EPOCH,
			O_WRONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0) return -1;
	if (fstat(fd, &state) < 0 || !secure_state(&state) ||
	    ftruncate(fd, 0) < 0 || pwrite(fd, data, 17U, 0) != 17 || fsync(fd) < 0) {
		(void)close(fd);
		errno = EPERM;
		return -1;
	}
	return close(fd) < 0 ? -1 : 0;
}

static int read_epoch(uint64_t *epoch, int exclusive)
{
	char data[17];
	struct stat state;
	int fd;
	int saved;
	ssize_t got;
	fd = open(GKD_MENU_GUARD_EPOCH, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0) {
		if (errno != ENOENT || !exclusive) return -1;
		if (write_epoch(0U, 1) < 0) return -1;
		*epoch = 0U;
		return 0;
	}
	got = read(fd, data, sizeof(data));
	if (got < 0) {
		saved = errno;
		(void)close(fd);
		errno = saved;
		return -1;
	}
	if (fstat(fd, &state) < 0) {
		saved = errno;
		(void)close(fd);
		errno = saved;
		return -1;
	}
	if (!secure_state(&state)) {
		(void)close(fd);
		errno = EPERM;
		return -1;
	}
	if (state.st_size != (off_t)sizeof(data) ||
	    got != (ssize_t)sizeof(data)) {
		(void)close(fd);
		errno = EINVAL;
		return -1;
	}
	if (close(fd) < 0)
		return -1;
	return parse_epoch(data, epoch);
}

void gkd_menu_guard_owner_init(struct gkd_menu_guard_owner *owner)
{
	if (owner) owner->lease_fd = -1;
}

int gkd_menu_guard_owner_enter(struct gkd_menu_guard_owner *owner)
{
	uint64_t epoch;
	int fd;
	if (!owner || owner->lease_fd >= 0) { errno = EINVAL; return -1; }
	fd = open_lease(LOCK_EX);
	if (fd < 0) return -1;
	if (read_epoch(&epoch, 1) < 0) {
		(void)flock(fd, LOCK_UN); (void)close(fd);
		return -1;
	}
	if (epoch == UINT64_MAX) {
		errno = EOVERFLOW;
		(void)flock(fd, LOCK_UN); (void)close(fd);
		return -1;
	}
	if (write_epoch(epoch + 1U, 1) < 0) {
		(void)flock(fd, LOCK_UN); (void)close(fd);
		return -1;
	}
	owner->lease_fd = fd;
	return 0;
}

void gkd_menu_guard_owner_exit(struct gkd_menu_guard_owner *owner)
{
	if (!owner || owner->lease_fd < 0) return;
	(void)flock(owner->lease_fd, LOCK_UN);
	(void)close(owner->lease_fd);
	owner->lease_fd = -1;
}

void gkd_menu_guard_controls_init(struct gkd_menu_guard_controls *guard)
{
	if (guard) { guard->lease_fd = -1; guard->epoch = 0U; }
}

int gkd_menu_guard_controls_acquire(struct gkd_menu_guard_controls *guard,
	uint64_t *epoch_out)
{
	uint64_t epoch;
	int fd;
	if (!guard || !epoch_out || guard->lease_fd >= 0) { errno = EINVAL; return -1; }
	fd = open_lease(LOCK_SH);
	if (fd < 0) {
		if (errno == EWOULDBLOCK || errno == EAGAIN) { errno = EBUSY; return 1; }
		return -1;
	}
	if (read_epoch(&epoch, 0) < 0) {
		if (errno != ENOENT) { (void)flock(fd, LOCK_UN); (void)close(fd); return -1; }
		(void)flock(fd, LOCK_UN);
		if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
			(void)close(fd);
			if (errno == EWOULDBLOCK || errno == EAGAIN) { errno = EBUSY; return 1; }
			return -1;
		}
		if (read_epoch(&epoch, 1) < 0) {
			(void)flock(fd, LOCK_UN); (void)close(fd); return -1;
		}
		if (flock(fd, LOCK_SH | LOCK_NB) < 0) {
			(void)close(fd);
			if (errno == EWOULDBLOCK || errno == EAGAIN) { errno = EBUSY; return 1; }
			return -1;
		}
		if (read_epoch(&epoch, 0) < 0) {
			(void)flock(fd, LOCK_UN); (void)close(fd); return -1;
		}
	}
	guard->lease_fd = fd;
	guard->epoch = epoch;
	*epoch_out = epoch;
	return 0;
}

void gkd_menu_guard_controls_release(struct gkd_menu_guard_controls *guard)
{
	if (!guard || guard->lease_fd < 0) return;
	(void)flock(guard->lease_fd, LOCK_UN);
	(void)close(guard->lease_fd);
	guard->lease_fd = -1;
	guard->epoch = 0U;
}
