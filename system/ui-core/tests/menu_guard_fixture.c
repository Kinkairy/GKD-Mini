/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define GKD_MENU_GUARD_DIR "/tmp/gkd-menu-guard-fixture"
#include "../source/gkd-menu-guard.c"

#define CHECK(expression) do { \
	if (!(expression)) { \
		(void)fprintf(stderr, "%s:%d: %s errno=%d\\n", \
			__FILE__, __LINE__, #expression, errno); \
		return -1; \
	} \
} while (0)

static int remove_tree(void)
{
	(void)unlink(GKD_MENU_GUARD_EPOCH);
	(void)unlink(GKD_MENU_GUARD_LEASE);
	return rmdir(GKD_MENU_GUARD_DIR) < 0 && errno != ENOENT ? -1 : 0;
}

static int read_epoch_file(uint64_t *epoch)
{
	struct gkd_menu_guard_controls guard;
	gkd_menu_guard_controls_init(&guard);
	CHECK(gkd_menu_guard_controls_acquire(&guard, epoch) == 0);
	gkd_menu_guard_controls_release(&guard);
	return 0;
}

static int owner_busy_and_release(void)
{
	struct gkd_menu_guard_owner owner;
	struct gkd_menu_guard_controls controls;
	uint64_t epoch;
	gkd_menu_guard_owner_init(&owner);
	gkd_menu_guard_controls_init(&controls);
	CHECK(gkd_menu_guard_owner_enter(&owner) == 0);
	CHECK(gkd_menu_guard_controls_acquire(&controls, &epoch) == 1 && errno == EBUSY);
	gkd_menu_guard_owner_exit(&owner);
	CHECK(gkd_menu_guard_controls_acquire(&controls, &epoch) == 0 && epoch == 1U);
	gkd_menu_guard_controls_release(&controls);
	return 0;
}

static int owner_failure_cleanup_releases(void)
{
	struct gkd_menu_guard_owner owner;
	struct gkd_menu_guard_controls controls;
	uint64_t epoch;
	gkd_menu_guard_owner_init(&owner);
	gkd_menu_guard_controls_init(&controls);
	CHECK(gkd_menu_guard_owner_enter(&owner) == 0);
	/* Models every post-enter input/marker failure path's mandatory cleanup. */
	gkd_menu_guard_owner_exit(&owner);
	CHECK(gkd_menu_guard_controls_acquire(&controls, &epoch) == 0);
	gkd_menu_guard_controls_release(&controls);
	return 0;
}

static int forked_flock_and_epoch(void)
{
	int ready[2], proceed[2], status;
	char byte;
	pid_t child;
	uint64_t before, after;
	CHECK(read_epoch_file(&before) == 0);
	CHECK(pipe(ready) == 0 && pipe(proceed) == 0);
	child = fork();
	CHECK(child >= 0);
	if (child == 0) {
		struct gkd_menu_guard_owner owner;
		gkd_menu_guard_owner_init(&owner);
		if (gkd_menu_guard_owner_enter(&owner) || write(ready[1], "x", 1) != 1 ||
		    read(proceed[0], &byte, 1) != 1) _exit(2);
		gkd_menu_guard_owner_exit(&owner);
		_exit(0);
	}
	close(ready[1]); close(proceed[0]);
	CHECK(read(ready[0], &byte, 1) == 1);
	{
		struct gkd_menu_guard_controls controls;
		gkd_menu_guard_controls_init(&controls);
		CHECK(gkd_menu_guard_controls_acquire(&controls, &after) == 1 &&
			errno == EBUSY);
	}
	CHECK(write(proceed[1], "x", 1) == 1);
	CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
		WEXITSTATUS(status) == 0);
	CHECK(read_epoch_file(&after) == 0 && after == before + 1U);
	close(ready[0]); close(proceed[1]);
	return 0;
}

static int short_toggle_epoch(void)
{
	struct gkd_menu_guard_owner owner;
	uint64_t before, after;
	CHECK(read_epoch_file(&before) == 0);
	gkd_menu_guard_owner_init(&owner);
	CHECK(gkd_menu_guard_owner_enter(&owner) == 0);
	gkd_menu_guard_owner_exit(&owner);
	CHECK(read_epoch_file(&after) == 0 && after == before + 1U);
	return 0;
}

static int malformed_and_symlink_rejected(void)
{
	struct gkd_menu_guard_owner owner;
	DIR *directory;
	struct dirent *entry;
	unsigned int before = 0U, after = 0U, index;
	int fd;
	CHECK(unlink(GKD_MENU_GUARD_EPOCH) == 0);
	fd = open(GKD_MENU_GUARD_EPOCH, O_WRONLY | O_CREAT | O_EXCL, 0600);
	CHECK(fd >= 0 && write(fd, "0000000000000000\nx", 18) == 18 && close(fd) == 0);
	gkd_menu_guard_owner_init(&owner);
	CHECK(gkd_menu_guard_owner_enter(&owner) < 0);
	directory = opendir("/proc/self/fd");
	CHECK(directory != NULL);
	while ((entry = readdir(directory)) != NULL)
		if (entry->d_name[0] != '.') ++before;
	CHECK(closedir(directory) == 0);
	for (index = 0U; index != 32U; ++index)
		CHECK(gkd_menu_guard_owner_enter(&owner) < 0);
	directory = opendir("/proc/self/fd");
	CHECK(directory != NULL);
	while ((entry = readdir(directory)) != NULL)
		if (entry->d_name[0] != '.') ++after;
	CHECK(closedir(directory) == 0 && after == before);
	CHECK(unlink(GKD_MENU_GUARD_EPOCH) == 0);
	CHECK(symlink("/etc/passwd", GKD_MENU_GUARD_EPOCH) == 0);
	CHECK(gkd_menu_guard_owner_enter(&owner) < 0);
	CHECK(unlink(GKD_MENU_GUARD_EPOCH) == 0);
	return 0;
}

int main(void)
{
	uint64_t epoch;
	CHECK(remove_tree() == 0);
	CHECK(owner_busy_and_release() == 0);
	CHECK(owner_failure_cleanup_releases() == 0);
	CHECK(forked_flock_and_epoch() == 0);
	CHECK(short_toggle_epoch() == 0);
	CHECK(malformed_and_symlink_rejected() == 0);
	CHECK(read_epoch_file(&epoch) == 0);
	CHECK(remove_tree() == 0);
	return 0;
}
