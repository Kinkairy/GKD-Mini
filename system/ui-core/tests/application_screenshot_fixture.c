// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int gkd_screenshot_actual_main(int argc, char **argv);

enum failure_mode {
	FAIL_NONE,
	FAIL_ROOT_DEVICE,
	FAIL_BLOCK_TYPE,
	FAIL_DIRECTORY_SYMLINK,
	FAIL_SNAPSHOT_OPEN,
	FAIL_SNAPSHOT_SHORT,
	FAIL_FILE_SYNC,
};

static enum failure_mode failure;
static unsigned snapshot_opens, directory_opens, png_opens, png_unlinks;
static unsigned file_syncs, directory_syncs, png_writes;
static unsigned local_root_stats;
static int collide_once;
static int saw_snapshot_flags, saw_directory_flags, saw_png_flags;
static size_t png_bytes;
static unsigned char png_prefix[8];
static char result_text[512];

static void reset(void)
{
	failure = FAIL_NONE;
	snapshot_opens = directory_opens = png_opens = png_unlinks = 0;
	file_syncs = directory_syncs = png_writes = 0;
	local_root_stats = 0;
	collide_once = saw_snapshot_flags = saw_directory_flags = 0;
	saw_png_flags = 0;
	png_bytes = 0;
	memset(png_prefix, 0, sizeof(png_prefix));
	memset(result_text, 0, sizeof(result_text));
}

int __wrap_fstat(int fd, struct stat *st)
{
	memset(st, 0, sizeof(*st));
	if (fd == 3) {
		st->st_mode = S_IFDIR | 0700;
		st->st_dev = failure == FAIL_ROOT_DEVICE ? 43 : 42;
		st->st_ino = 7;
		return 0;
	}
	if (fd == 5) {
		st->st_mode = S_IFDIR | 0755;
		st->st_dev = 42;
		st->st_ino = 8;
		return 0;
	}
	errno = EBADF;
	return -1;
}

int __wrap_fstat64(int fd, struct stat64 *st)
{
	struct stat plain;
	int result = __wrap_fstat(fd, &plain);

	memset(st, 0, sizeof(*st));
	st->st_mode = plain.st_mode;
	st->st_dev = plain.st_dev;
	st->st_ino = plain.st_ino;
	return result;
}

int __wrap_lstat(const char *path, struct stat *st)
{
	assert(!strcmp(path, "/media/sdcard"));
	++local_root_stats;
	memset(st, 0, sizeof(*st));
	errno = ENOENT;
	return -1;
}

int __wrap_lstat64(const char *path, struct stat64 *st)
{
	struct stat plain;
	int result = __wrap_lstat(path, &plain);

	memset(st, 0, sizeof(*st));
	st->st_mode = plain.st_mode;
	st->st_dev = plain.st_dev;
	st->st_ino = plain.st_ino;
	return result;
}

int __wrap_stat(const char *path, struct stat *st)
{
	assert(!strcmp(path, "/dev/mmcblk1p1"));
	memset(st, 0, sizeof(*st));
	st->st_mode = (failure == FAIL_BLOCK_TYPE ? S_IFREG : S_IFBLK) | 0600;
	st->st_rdev = 42;
	return 0;
}

int __wrap_stat64(const char *path, struct stat64 *st)
{
	struct stat plain;
	int result = __wrap_stat(path, &plain);

	memset(st, 0, sizeof(*st));
	st->st_mode = plain.st_mode;
	st->st_rdev = plain.st_rdev;
	return result;
}

int __wrap_fcntl(int fd, int command, ...)
{
	if (fd == 3) {
		assert(command == F_DUPFD);
		return 4;
	}
	assert(fd == 4);
	assert(command == F_SETFD);
	return 0;
}

int __wrap_fcntl64(int fd, int command, ...)
{
	return __wrap_fcntl(fd, command);
}

int __wrap_mkdirat(int fd, const char *path, mode_t mode)
{
	assert(fd == 4);
	assert(!strcmp(path, "screenshots"));
	assert(mode == 0755);
	return 0;
}

int __wrap_openat(int fd, const char *path, int flags, ...)
{
	if (fd == 4) {
		assert(!strcmp(path, "screenshots"));
		++directory_opens;
		saw_directory_flags =
			(flags & (O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)) ==
			(O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
		if (failure == FAIL_DIRECTORY_SYMLINK) {
			errno = ELOOP;
			return -1;
		}
		return 5;
	}
	assert(fd == 5);
	assert(!strncmp(path, "GKD-", 4));
	assert(strstr(path, ".png"));
	++png_opens;
	saw_png_flags = (flags & (O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW |
				     O_CLOEXEC)) ==
			(O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC);
	if (collide_once && png_opens == 1) {
		errno = EEXIST;
		return -1;
	}
	return 7;
}

int __wrap_openat64(int fd, const char *path, int flags, ...)
{
	return __wrap_openat(fd, path, flags);
}

int __wrap_open(const char *path, int flags, ...)
{
	assert(!strcmp(path,
		"/sys/class/graphics/fb0/gkd_snapshot_rgb565"));
	++snapshot_opens;
	saw_snapshot_flags = (flags & (O_CLOEXEC | O_NOFOLLOW)) ==
		(O_CLOEXEC | O_NOFOLLOW);
	if (failure == FAIL_SNAPSHOT_OPEN) {
		errno = ENOENT;
		return -1;
	}
	return 6;
}

int __wrap_open64(const char *path, int flags, ...)
{
	return __wrap_open(path, flags);
}

ssize_t __wrap_pread(int fd, void *buffer, size_t length, off_t offset)
{
	static int short_once;
	assert(fd == 6);
	if (offset == 0)
		short_once = 0;
	if (failure == FAIL_SNAPSHOT_SHORT && short_once++)
		return 0;
	if (failure == FAIL_SNAPSHOT_SHORT && length > 16)
		length = 16;
	memset(buffer, 0x1f, length);
	return (ssize_t)length;
}

ssize_t __wrap_pread64(int fd, void *buffer, size_t length, off64_t offset)
{
	return __wrap_pread(fd, buffer, length, (off_t)offset);
}

ssize_t __wrap_write(int fd, const void *buffer, size_t length)
{
	assert(fd == 7);
	++png_writes;
	png_bytes += length;
	if (png_writes == 1) {
		size_t copy = length < sizeof(png_prefix) ? length : sizeof(png_prefix);
		memcpy(png_prefix, buffer, copy);
	}
	return (ssize_t)length;
}

int __wrap_fsync(int fd)
{
	if (fd == 7) {
		++file_syncs;
		if (failure == FAIL_FILE_SYNC) {
			errno = EIO;
			return -1;
		}
		return 0;
	}
	assert(fd == 5);
	++directory_syncs;
	return 0;
}

int __wrap_close(int fd)
{
	assert(fd >= 4 && fd <= 7);
	return 0;
}

int __wrap_unlinkat(int fd, const char *path, int flags)
{
	assert(fd == 5);
	assert(!strncmp(path, "GKD-", 4));
	assert(flags == 0);
	++png_unlinks;
	return 0;
}

time_t __wrap_time(time_t *value)
{
	time_t fixed = 1788873000;

	if (value)
		*value = fixed;
	return fixed;
}

int __wrap_printf(const char *format, ...)
{
	va_list arguments;
	int length;

	va_start(arguments, format);
	length = vsnprintf(result_text, sizeof(result_text), format, arguments);
	va_end(arguments);
	return length;
}

int __wrap_fflush(FILE *stream)
{
	assert(stream == stdout);
	return 0;
}

static int invoke(const char *path)
{
	char *argv[] = {
		"gkd-screenshot", "--application-capture", "3", (char *)path,
		NULL
	};

	return gkd_screenshot_actual_main(4, argv);
}

static void success_case(void)
{
	static const unsigned char signature[8] =
		{137, 80, 78, 71, 13, 10, 26, 10};

	reset();
	assert(invoke("/media/sdcard/screenshots") == 0);
	assert(snapshot_opens == 1 && directory_opens == 1 && png_opens == 1);
	assert(local_root_stats == 0);
	assert(saw_snapshot_flags && saw_directory_flags && saw_png_flags);
	assert(png_writes == 1 && png_bytes > 1000);
	assert(!memcmp(png_prefix, signature, sizeof(signature)));
	assert(file_syncs == 1 && directory_syncs == 1 && png_unlinks == 0);
	assert(strstr(result_text, "GKD_SCREENSHOT_RESULT=success filename="));
	assert(strstr(result_text, "/media/sdcard/screenshots/GKD-"));
}

static void collision_case(void)
{
	reset();
	collide_once = 1;
	assert(invoke("/media/sdcard/screenshots") == 0);
	assert(png_opens == 2);
	assert(strstr(result_text, "-001.png"));
}

static void invalid_root_cases(void)
{
	reset();
	failure = FAIL_ROOT_DEVICE;
	assert(invoke("/media/sdcard/screenshots") == 1);
	assert(snapshot_opens == 0 && png_opens == 0);
	assert(strstr(result_text, "GKD_SCREENSHOT_RESULT=failure"));
	reset();
	failure = FAIL_BLOCK_TYPE;
	assert(invoke("/media/sdcard/screenshots") == 1);
	assert(snapshot_opens == 0 && png_opens == 0);
}

static void invalid_path_cases(void)
{
	static const char *const paths[] = {
		"/tmp/screenshots",
		"/media/sdcard/../escape",
		"/media/sdcard/./screenshots",
		"/media/sdcard//screenshots",
		"/media/sdcard/screenshots/",
	};
	size_t i;

	for (i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
		reset();
		assert(invoke(paths[i]) == 1);
		assert(directory_opens == 0 && snapshot_opens == 0 && png_opens == 0);
	}
}

static void symlink_case(void)
{
	reset();
	failure = FAIL_DIRECTORY_SYMLINK;
	assert(invoke("/media/sdcard/screenshots") == 1);
	assert(directory_opens == 1 && snapshot_opens == 0 && png_opens == 0);
}

static void snapshot_failure_cases(void)
{
	reset();
	failure = FAIL_SNAPSHOT_OPEN;
	assert(invoke("/media/sdcard/screenshots") == 1);
	assert(snapshot_opens == 1 && png_opens == 0);
	reset();
	failure = FAIL_SNAPSHOT_SHORT;
	assert(invoke("/media/sdcard/screenshots") == 1);
	assert(snapshot_opens == 1 && png_opens == 0);
}

static void durability_failure_case(void)
{
	reset();
	failure = FAIL_FILE_SYNC;
	assert(invoke("/media/sdcard/screenshots") == 1);
	assert(file_syncs == 1 && directory_syncs == 0 && png_unlinks == 1);
}

int main(void)
{
	success_case();
	collision_case();
	invalid_root_cases();
	invalid_path_cases();
	symlink_case();
	snapshot_failure_cases();
	durability_failure_case();
	puts("GKD_APPLICATION_SCREENSHOT_ACTUAL=PASS cases=13 sysfs-only/root-pin/path/symlink/exclusive/png/fsync/cleanup/typed-result");
	return 0;
}
