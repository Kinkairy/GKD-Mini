/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-input-owner.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define INPUT_ROOT "/tmp/gkd-input-owner-events"
#define OWNER_DIR "/tmp/gkd-input-owner-state"
#define TEST_SESSION OWNER_DIR "/test-session"
#define MARKER OWNER_DIR "/virtual-grabbed"

struct device {
	int kind;
	int fd;
	int grabbed;
};

static struct device devices[4];
static unsigned int device_count;
static int fail_physical_grab, fail_virtual_grab;
static unsigned int guard_enters, guard_exits, grabs, releases, closes, sequence;
static unsigned int guard_sequence, first_grab_sequence;

int __real_open(const char *path, int flags, ...);
int __real_close(int fd);
int __real_fstat(int fd, struct stat *state);
int __real_lstat(const char *path, struct stat *state);

#define CHECK(expression) do { \
	if (!(expression)) { \
		(void)fprintf(stderr, "%s:%d: %s errno=%d\\n", \
			__FILE__, __LINE__, #expression, errno); \
		return -1; \
	} \
} while (0)

static struct device *device_for_fd(int fd)
{
	unsigned int index;
	for (index = 0U; index < device_count; ++index)
		if (devices[index].fd == fd) return &devices[index];
	return NULL;
}

static struct device *device_for_path(const char *path)
{
	const char *name = strrchr(path, '/');
	unsigned int index;
	if (!name || strncmp(path, INPUT_ROOT "/", sizeof(INPUT_ROOT)) != 0 ||
	    strncmp(name + 1, "event", 5) != 0) return NULL;
	for (index = 0U; index < device_count; ++index)
		if ((unsigned int)(name[6] - '0') == index) return &devices[index];
	return NULL;
}

int __wrap_open(const char *path, int flags, ...)
{
	struct device *device = device_for_path(path);
	mode_t mode = 0;
	va_list arguments;
	if (device) return device->fd;
	if (flags & O_CREAT) {
		va_start(arguments, flags); mode = (mode_t)va_arg(arguments, int); va_end(arguments);
		return __real_open(path, flags, mode);
	}
	return __real_open(path, flags);
}

int __wrap_close(int fd)
{
	if (device_for_fd(fd)) { ++closes; return 0; }
	return __real_close(fd);
}

int __wrap_fstat(int fd, struct stat *state)
{
	if (device_for_fd(fd)) {
		memset(state, 0, sizeof(*state)); state->st_mode = S_IFCHR | 0600;
		return 0;
	}
	return __real_fstat(fd, state);
}

int __wrap_lstat(const char *path, struct stat *state)
{
	int result = __real_lstat(path, state);
	if (result == 0 && (!strcmp(path, OWNER_DIR) || !strcmp(path, TEST_SESSION) ||
		!strcmp(path, MARKER))) {
		state->st_uid = 0; state->st_gid = 0;
		state->st_mode = (state->st_mode & S_IFMT) | (!strcmp(path, OWNER_DIR) ? 0700 : 0600);
	}
	return result;
}

int __wrap_ioctl(int fd, unsigned long request, ...)
{
	struct device *device = device_for_fd(fd);
	void *argument;
	va_list arguments;
	if (!device) { errno = EBADF; return -1; }
	va_start(arguments, request); argument = va_arg(arguments, void *); va_end(arguments);
	if (request == EVIOCGNAME(128)) {
		strcpy(argument, device->kind == 1 ? "gpio-keys" : "GKD Mini Virtual Controls");
		return 0;
	}
	if (_IOC_TYPE(request) == 'E' && _IOC_NR(request) == 0x20 + 0) {
		unsigned long *bits = argument; bits[0] |= 1UL << EV_KEY; return 0;
	}
	if (_IOC_TYPE(request) == 'E' && _IOC_NR(request) == 0x20 + EV_KEY) {
		unsigned long *bits = argument;
		bits[KEY_UP / (8U * sizeof(unsigned long))] |= 1UL << (KEY_UP % (8U * sizeof(unsigned long)));
		bits[KEY_DOWN / (8U * sizeof(unsigned long))] |= 1UL << (KEY_DOWN % (8U * sizeof(unsigned long)));
		bits[KEY_LEFTCTRL / (8U * sizeof(unsigned long))] |= 1UL << (KEY_LEFTCTRL % (8U * sizeof(unsigned long)));
		bits[KEY_LEFTALT / (8U * sizeof(unsigned long))] |= 1UL << (KEY_LEFTALT % (8U * sizeof(unsigned long)));
		return 0;
	}
	if (request == EVIOCGID) {
		struct input_id *identity = argument;
		memset(identity, 0, sizeof(*identity)); identity->bustype = BUS_VIRTUAL;
		identity->vendor = 0x474b; identity->product = 0x0281; return 0;
	}
	if (request == EVIOCGRAB) {
		int grab = (int)(intptr_t)argument;
		if (grab) {
			++grabs; if (!first_grab_sequence) first_grab_sequence = ++sequence;
			if ((device->kind == 1 && fail_physical_grab) ||
			    (device->kind == 2 && fail_virtual_grab)) { errno = EIO; return -1; }
			device->grabbed = 1;
		} else {
			++releases; ++sequence;
			if (!device->grabbed) { errno = EINVAL; return -1; }
			device->grabbed = 0;
		}
		return 0;
	}
	errno = EINVAL;
	return -1;
}

int __wrap_usleep(useconds_t microseconds) { (void)microseconds; return 0; }

void gkd_menu_guard_owner_init(struct gkd_menu_guard_owner *owner)
{
	if (owner) owner->lease_fd = -1;
}

int gkd_menu_guard_owner_enter(struct gkd_menu_guard_owner *owner)
{
	if (!owner || owner->lease_fd >= 0) { errno = EINVAL; return -1; }
	guard_sequence = ++sequence; ++guard_enters; owner->lease_fd = 77; return 0;
}

void gkd_menu_guard_owner_exit(struct gkd_menu_guard_owner *owner)
{
	if (owner && owner->lease_fd >= 0) { ++guard_exits; ++sequence; owner->lease_fd = -1; }
}

static int reset_fixture(unsigned int count)
{
	unsigned int index;
	char path[128];
	memset(devices, 0, sizeof(devices)); device_count = count;
	fail_physical_grab = fail_virtual_grab = 0;
	guard_enters = guard_exits = grabs = releases = closes = sequence = 0U;
	guard_sequence = first_grab_sequence = 0U;
	(void)unlink(MARKER); (void)unlink(TEST_SESSION);
	for (index = 0U; index < 4U; ++index) {
		(void)snprintf(path, sizeof(path), INPUT_ROOT "/event%u", index); (void)unlink(path);
	}
	CHECK(mkdir(INPUT_ROOT, 0700) == 0 || errno == EEXIST);
	CHECK(mkdir(OWNER_DIR, 0700) == 0 || errno == EEXIST);
	for (index = 0U; index < count; ++index) {
		int fd;
		(void)snprintf(path, sizeof(path), INPUT_ROOT "/event%u", index);
		fd = creat(path, 0600); CHECK(fd >= 0); CHECK(close(fd) == 0);
		devices[index].fd = 100 + (int)index;
	}
	return 0;
}

static int production_without_token_and_guard_order(void)
{
	struct gkd_input_owner owner;
	CHECK(reset_fixture(2U) == 0); devices[0].kind = 1; devices[1].kind = 2;
	gkd_input_owner_init(&owner);
	CHECK(gkd_input_owner_open_menu(&owner) == 0);
	CHECK(owner.marker_published == 0 && guard_enters == 1U && guard_sequence < first_grab_sequence);
	CHECK(access(TEST_SESSION, F_OK) < 0 && errno == ENOENT);
	CHECK(gkd_input_owner_close(&owner) == 0 && owner.physical_fd == -1 && owner.virtual_fd == -1);
	return 0;
}

static int acquisition_failure_and_invalid_reentry(void)
{
	struct gkd_input_owner owner;
	unsigned int close_before_reentry;
	CHECK(reset_fixture(2U) == 0); devices[0].kind = 1; devices[1].kind = 2; fail_virtual_grab = 1;
	gkd_input_owner_init(&owner);
	CHECK(gkd_input_owner_open_menu(&owner) < 0 && errno == EIO &&
		owner.physical_fd == -1 && owner.virtual_fd == -1);
	CHECK(guard_enters == 1U && guard_exits == 1U && closes >= 2U && releases == 1U);
	CHECK(reset_fixture(2U) == 0); devices[0].kind = 1; devices[1].kind = 2;
	gkd_input_owner_init(&owner); CHECK(gkd_input_owner_open_menu(&owner) == 0);
	close_before_reentry = closes;
	CHECK(gkd_input_owner_open_menu(&owner) < 0 && errno == EINVAL);
	CHECK(owner.physical_fd == 100 && owner.virtual_fd == 101 && guard_exits == 0U &&
		closes == close_before_reentry);
	CHECK(gkd_input_owner_close(&owner) == 0);
	return 0;
}

static int discovery_failure_and_duplicates(void)
{
	struct gkd_input_owner owner;
	CHECK(reset_fixture(0U) == 0); gkd_input_owner_init(&owner);
	CHECK(gkd_input_owner_open_menu(&owner) < 0 && guard_enters == 0U && owner.physical_fd == -1);
	CHECK(reset_fixture(3U) == 0); devices[0].kind = 1; devices[1].kind = 1; devices[2].kind = 2;
	gkd_input_owner_init(&owner);
	CHECK(gkd_input_owner_open_menu(&owner) < 0 && guard_enters == 0U && closes > 0U &&
		owner.physical_fd == -1 && owner.virtual_fd == -1);
	return 0;
}

static int reused_guard_transfer_and_failure_unwind(void)
{
	struct gkd_input_owner owner;
	struct gkd_menu_guard_owner guard;
	CHECK(reset_fixture(2U) == 0); devices[0].kind = 1; devices[1].kind = 2;
	gkd_input_owner_init(&owner); gkd_menu_guard_owner_init(&guard);
	CHECK(gkd_menu_guard_owner_enter(&guard) == 0 && guard.lease_fd == 77);
	fail_virtual_grab = 1;
	CHECK(gkd_input_owner_open_menu_reuse(&owner, &guard) < 0 && errno == EIO);
	CHECK(guard.lease_fd == 77 && owner.menu_guard.lease_fd == -1 &&
		owner.physical_fd == -1 && owner.virtual_fd == -1 && releases == 1U &&
		guard_exits == 0U);
	gkd_menu_guard_owner_exit(&guard); CHECK(guard_exits == 1U);
	CHECK(reset_fixture(2U) == 0); devices[0].kind = 1; devices[1].kind = 2;
	gkd_input_owner_init(&owner); gkd_menu_guard_owner_init(&guard);
	CHECK(gkd_menu_guard_owner_enter(&guard) == 0);
	CHECK(gkd_input_owner_open_menu_reuse(&owner, &guard) == 0 && guard.lease_fd == 77);
	CHECK(gkd_input_owner_transfer_menu_guard(&owner, &guard) == 0 &&
		guard.lease_fd == -1 && owner.menu_guard.lease_fd == 77);
	CHECK(gkd_input_owner_close(&owner) == 0 && guard_exits == 1U);
	return 0;
}

static int legacy_marker_contract(void)
{
	struct gkd_input_owner owner;
	int fd;
	CHECK(reset_fixture(2U) == 0); devices[0].kind = 1; devices[1].kind = 2;
	gkd_input_owner_init(&owner);
	CHECK(gkd_input_owner_open(&owner) < 0 && guard_enters == 0U);
	fd = creat(TEST_SESSION, 0600); CHECK(fd >= 0); CHECK(close(fd) == 0);
	CHECK(gkd_input_owner_open(&owner) == 0 && owner.marker_published == 1 && access(MARKER, F_OK) == 0);
	CHECK(gkd_input_owner_close(&owner) == 0 && access(MARKER, F_OK) < 0 && errno == ENOENT);
	return 0;
}

int main(void)
{
	CHECK(production_without_token_and_guard_order() == 0);
	CHECK(acquisition_failure_and_invalid_reentry() == 0);
	CHECK(discovery_failure_and_duplicates() == 0);
	CHECK(reused_guard_transfer_and_failure_unwind() == 0);
	CHECK(legacy_marker_contract() == 0);
	puts("GKD_INPUT_OWNER_MENU_FIXTURE=PASS guard-transfer/failure-unwind/acquired-only-release");
	return 0;
}
