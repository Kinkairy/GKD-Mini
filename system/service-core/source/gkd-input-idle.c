#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define LONG_BITS (sizeof(unsigned long) * 8U)
#define LONGS_FOR(bits) (((bits) + LONG_BITS - 1U) / LONG_BITS)

static int any_pressed(const unsigned long *keys)
{
	unsigned int index;
	for (index = 0; index < LONGS_FOR(KEY_MAX + 1U); ++index)
		if (keys[index] != 0UL)
			return 1;
	return 0;
}

static int check_device(const char *path, int *matched)
{
	unsigned long keys[LONGS_FOR(KEY_MAX + 1U)];
	char name[64];
	int fd;

	fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return errno == ENOENT || errno == ENODEV ? 0 : -1;
	memset(name, 0, sizeof(name));
	if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
		close(fd);
		return -1;
	}
	if (strcmp(name, "gpio-keys") != 0) {
		close(fd);
		return 0;
	}
	if (*matched != 0) {
		close(fd);
		return -1;
	}
	*matched = 1;
	memset(keys, 0, sizeof(keys));
	if (ioctl(fd, EVIOCGKEY(sizeof(keys)), keys) < 0 || close(fd) < 0)
		return -1;
	return any_pressed(keys) ? 1 : 0;
}

int main(int argc, char **argv)
{
	char path[32];
	int matched = 0;
	int pressed = 0;
	int index;

	if (argc == 2 && strcmp(argv[1], "--self-test") == 0) {
		unsigned long keys[LONGS_FOR(KEY_MAX + 1U)];
		memset(keys, 0, sizeof(keys));
		if (any_pressed(keys))
			return 1;
		keys[KEY_A / LONG_BITS] |= 1UL << (KEY_A % LONG_BITS);
		return any_pressed(keys) ? 0 : 1;
	}
	if (argc != 1)
		return 2;
	for (index = 0; index < 32; ++index) {
		int result;
		snprintf(path, sizeof(path), "/dev/input/event%d", index);
		result = check_device(path, &matched);
		if (result < 0)
			return 2;
		if (result > 0)
			pressed = 1;
	}
	if (matched != 1)
		return 2;
	return pressed ? 1 : 0;
}
