// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* Generated into each independent action producer. Invalid state is open. */
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

#ifndef GKD_AF_DIR
#define GKD_AF_DIR "/run/gkd-action-freeze"
#endif
#ifndef GKD_AF_LEASE
#define GKD_AF_LEASE GKD_AF_DIR "/lease"
#endif

static unsigned long long gkd_af_now_ms(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return 0;
	return (unsigned long long)ts.tv_sec * 1000ULL +
		(unsigned long long)ts.tv_nsec / 1000000ULL;
}

static int gkd_af_proc_start(pid_t pid, unsigned long long *out)
{
	char path[64], body[1024], *cursor, *next, *parsed_end = NULL;
	int fd, field = 3;
	ssize_t count;
	if (snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid) >= (int)sizeof(path)) return -1;
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) return -1;
	count = read(fd, body, sizeof(body) - 1U);
	if (close(fd) < 0 || count <= 0 || count >= (ssize_t)sizeof(body)) return -1;
	body[count] = '\0'; cursor = strrchr(body, ')');
	if (!cursor || cursor[1] != ' ') return -1;
	cursor += 2;
	while (*cursor) {
		next = strchr(cursor, ' '); if (next) *next = '\0';
		if (field == 22) {
			errno = 0; *out = strtoull(cursor, &parsed_end, 10);
			return !errno && parsed_end != cursor && !*parsed_end && *out ? 0 : -1;
		}
		if (!next) break;
		cursor = next + 1; ++field;
	}
	return -1;
}

static int gkd_action_freeze_state(const char *expected_kind,
				   unsigned long expected_requester,
				   unsigned long long expected_requester_start,
				   int require_ready, unsigned long *owner_out,
				   unsigned long long *owner_start_out)
{
	struct stat st;
	char body[256], kind[24], phase[24];
	unsigned long owner = 0, requester = 0;
	unsigned long long owner_start = 0, deadline = 0, requester_start = 0,
		actual_owner = 0, actual_requester = 0;
	int fd;
	ssize_t count;
	if (lstat(GKD_AF_DIR, &st) < 0 || !S_ISDIR(st.st_mode) || st.st_uid != 0 ||
	    (st.st_mode & 07777) != 0700) return 0;
	if (lstat(GKD_AF_LEASE, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != 0 ||
	    st.st_nlink != 1 || (st.st_mode & 07777) != 0600 || st.st_size <= 0 ||
	    st.st_size >= (off_t)sizeof(body)) return 0;
	fd = open(GKD_AF_LEASE, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) return 0;
	count = read(fd, body, sizeof(body) - 1U);
	if (close(fd) < 0 || count <= 0 || count >= (ssize_t)sizeof(body)) return 0;
	body[count] = '\0';
	if (sscanf(body,
		"v=2\nowner=%lu\nowner-start=%llu\ndeadline=%llu\nkind=%23[a-z-]\nphase=%23[a-z-]\nrequester=%lu\nrequester-start=%llu\n",
		&owner, &owner_start, &deadline, kind, phase, &requester, &requester_start) != 7) return 0;
	if (!owner || !owner_start || !deadline || !requester || !requester_start ||
	    gkd_af_now_ms() >= deadline) return 0;
	if (strcmp(phase, "preparing") && strcmp(phase, "ready") &&
	    strcmp(phase, "committed-terminal")) return 0;
	if (gkd_af_proc_start((pid_t)owner, &actual_owner) < 0 || actual_owner != owner_start ||
	    gkd_af_proc_start((pid_t)requester, &actual_requester) < 0 ||
	    actual_requester != requester_start) return 0;
	if (require_ready && (strcmp(phase, "ready") || !expected_kind ||
	    strcmp(kind, expected_kind) || requester != expected_requester ||
	    requester_start != expected_requester_start)) return 0;
	if (owner_out) *owner_out = owner;
	if (owner_start_out) *owner_start_out = owner_start;
	return 1;
}

static int gkd_action_freeze_active(void)
{
	return gkd_action_freeze_state(NULL, 0, 0, 0, NULL, NULL);
}

/* Successful ready proves screenshot stop and the first Loading frame completed. */
static int gkd_action_freeze_ready(const char *kind, unsigned long requester,
				   unsigned long long requester_start,
				   unsigned long *owner, unsigned long long *owner_start)
{
	return gkd_action_freeze_state(kind, requester, requester_start, 1, owner, owner_start);
}

struct gkd_af_token {
	int valid;
	char kind[24];
	unsigned long owner;
	unsigned long long owner_start;
	unsigned long long requester_start;
};

static struct gkd_af_token gkd_af_token;

static int __attribute__((unused)) gkd_af_arm(const char *kind)
{
	pid_t child, waited;
	unsigned long long start;
	int status = 0;
	char pid[24], begun[32];
	if (gkd_af_token.valid || gkd_af_proc_start(getpid(), &start) < 0 ||
		snprintf(pid, sizeof(pid), "%ld", (long)getpid()) <= 0 ||
		snprintf(begun, sizeof(begun), "%llu", start) <= 0) return -1;
	child = fork();
	if (child < 0) return -1;
	if (child == 0) { execl("/usr/sbin/gkd-action-freeze", "gkd-action-freeze", "acquire", kind, pid, begun, (char *)NULL); _exit(127); }
	do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
	if (waited != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
	    !gkd_action_freeze_ready(kind, (unsigned long)getpid(), start,
		&gkd_af_token.owner, &gkd_af_token.owner_start)) return -1;
	gkd_af_token.valid = 1;
	gkd_af_token.requester_start = start;
	(void)snprintf(gkd_af_token.kind, sizeof(gkd_af_token.kind), "%s", kind);
	return 0;
}

static int gkd_af_transition(const char *command, const char *kind)
{
	pid_t child, waited;
	unsigned long long start;
	int status = 0, result;
	char pid[24], begun[32], owner[24], owner_begun[32];
	if (!gkd_af_token.valid || strcmp(kind, gkd_af_token.kind) ||
	    gkd_af_proc_start(getpid(), &start) < 0 || start != gkd_af_token.requester_start) return -1;
	(void)snprintf(pid, sizeof(pid), "%ld", (long)getpid());
	(void)snprintf(begun, sizeof(begun), "%llu", start);
	(void)snprintf(owner, sizeof(owner), "%lu", gkd_af_token.owner);
	(void)snprintf(owner_begun, sizeof(owner_begun), "%llu", gkd_af_token.owner_start);
	child = fork();
	if (child < 0) return -1;
	if (child == 0) { execl("/usr/sbin/gkd-action-freeze", "gkd-action-freeze", command,
		kind, pid, begun, owner, owner_begun, (char *)NULL); _exit(127); }
	do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
	result = waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
	if (!strcmp(command, "release") && (result == 0 || !gkd_action_freeze_active()))
		memset(&gkd_af_token, 0, sizeof(gkd_af_token));
	return result;
}

static int __attribute__((unused)) gkd_af_release(const char *kind)
{
	return gkd_af_transition("release", kind);
}

static int __attribute__((unused)) gkd_af_commit(const char *kind)
{
	return gkd_af_transition("commit", kind);
}


#ifndef UI_DEV_SETUP
struct uinput_setup {
	struct input_id id;
	char name[UINPUT_MAX_NAME_SIZE];
	unsigned int ff_effects_max;
};
#define UI_DEV_SETUP _IOW(UINPUT_IOCTL_BASE, 3, struct uinput_setup)
#endif
#ifndef BUS_VIRTUAL
#define BUS_VIRTUAL 0x06
#endif
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 02000000
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifdef GKD_APPLICATION_UI
#define GKD_INPUT_CONFIG "/run/gkd-config/current/effective.conf"
#else
#define GKD_INPUT_CONFIG "/etc/gkd-mini/gdkmini.conf"
#endif
#define GKD_INPUT_SOCKET "gkdmini-input-v1"
#define GKD_INPUT_NAME "GKD Mini Virtual Controls"
#define GKD_INPUT_MAX_CONFIG 65536U
#define GKD_INPUT_MAX_PACKET 64U
#define GKD_INPUT_BACKLOG 4
#define GKD_CHOOSER_PIDFILE "/run/gkd-usb-chooser/supervisor.pid"
#define GKD_CHOOSER_EXE "/usr/sbin/gkd-usb-chooser"

#define GKD_MENU_OWNER_DIR "/run/gkd-menu-owner"
#define GKD_MENU_OWNER_LEASE GKD_MENU_OWNER_DIR "/lease"
#define GKD_MENU_TEST_TOKEN GKD_MENU_OWNER_DIR "/test-session"
#define GKD_MENU_VIRTUAL_GRAB GKD_MENU_OWNER_DIR "/virtual-grabbed"
#define GKD_MENU_SUPPRESS_NEXT GKD_MENU_OWNER_DIR "/suppress-next-session"
#define GKD_MENU_VIRTUAL_NAME "GKD Mini Virtual Controls"


static int gkd_coherence_secure_empty(const char *path)
{
	struct stat st;
	return lstat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == 0 &&
		st.st_nlink == 1 && (st.st_mode & 07777) == 0600 && st.st_size == 0;
}

static int gkd_coherence_virtual_marker(pid_t pid, unsigned long long start)
{
	char expected[96], actual[96];
	struct stat st;
	ssize_t count;
	int length, fd;
	if (!gkd_coherence_secure_empty(GKD_MENU_TEST_TOKEN)) return 0;
	if (lstat(GKD_MENU_VIRTUAL_GRAB, &st) < 0 || !S_ISREG(st.st_mode) ||
	    st.st_uid != 0 || st.st_nlink != 1 || (st.st_mode & 07777) != 0600 ||
	    st.st_size <= 0 || st.st_size >= (off_t)sizeof(actual)) return 0;
	length = snprintf(expected, sizeof(expected), "%ld %llu %s\n", (long)pid,
		start, GKD_MENU_VIRTUAL_NAME);
	fd = open(GKD_MENU_VIRTUAL_GRAB, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) return 0;
	count = read(fd, actual, sizeof(actual));
	(void)close(fd);
	return length > 0 && count == length && !memcmp(actual, expected, (size_t)length);
}

struct key_map {
	const char *logical;
	const char *setting;
	unsigned short code;
	int seen;
};

struct input_config {
	struct key_map keys[20];
	unsigned int press_ms;
	int press_seen;
	int require_owner;
};

static volatile sig_atomic_t stopping;

static void initialize_map(struct input_config *config)
{
	static const struct key_map defaults[] = {
		{"up", "input_map_dpad_up", 0, 0},
		{"down", "input_map_dpad_down", 0, 0},
		{"left", "input_map_dpad_left", 0, 0},
		{"right", "input_map_dpad_right", 0, 0},
		{"a", "input_map_a", 0, 0}, {"b", "input_map_b", 0, 0},
		{"x", "input_map_x", 0, 0}, {"y", "input_map_y", 0, 0},
		{"start", "input_map_start", 0, 0},
		{"select", "input_map_select", 0, 0},
		{"l1", "input_map_l1", 0, 0}, {"r1", "input_map_r1", 0, 0},
		{"l2", "input_map_l2", 0, 0}, {"r2", "input_map_r2", 0, 0},
		{"volume_up", "input_map_volume_up", 0, 0},
		{"volume_down", "input_map_volume_down", 0, 0},
		{"brightness", "input_map_brightness", 0, 0},
		{"menu", "input_map_menu", 0, 0},
		{"side_dot", "input_map_side_dot", 0, 0},
		{"side_double_dot", "input_map_side_double_dot", 0, 0},
	};

	memset(config, 0, sizeof(*config));
	memcpy(config->keys, defaults, sizeof(defaults));
}

#include "gkd-input-keys.h"

static int parse_number(const char *value, unsigned int *number)
{
	char *end = NULL;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(value, &end, 10);
	if (errno || end == value || *end || parsed < 20UL || parsed > 1000UL)
		return -1;
	*number = (unsigned int)parsed;
	return 0;
}

static int parse_config_text(char *text, struct input_config *config)
{
	char *cursor = text;
	unsigned int i;

	initialize_map(config);
	while (*cursor) {
		char *line = cursor;
		char *newline = strchr(cursor, '\n');
		char *equals;
		if (newline) {
			*newline = 0;
			cursor = newline + 1;
		} else {
			cursor += strlen(cursor);
		}
		if (!*line || *line == '#')
			continue;
		equals = strchr(line, '=');
		if (!equals || equals == line || !equals[1])
			return -1;
		*equals++ = 0;
		if (!strcmp(line, "input_inject_press_ms")) {
			if (config->press_seen || parse_number(equals, &config->press_ms) < 0)
				return -1;
			config->press_seen = 1;
			continue;
		}
		for (i = 0; i < sizeof(config->keys) / sizeof(config->keys[0]); ++i) {
			if (!strcmp(line, config->keys[i].setting)) {
				if (config->keys[i].seen ||
				    gkd_input_key_code(equals, &config->keys[i].code) < 0)
					return -1;
				config->keys[i].seen = 1;
				break;
			}
		}
	}
	if (!config->press_seen)
		return -1;
	for (i = 0; i < sizeof(config->keys) / sizeof(config->keys[0]); ++i)
		if (!config->keys[i].seen)
			return -1;
	return 0;
}

static int load_config(struct input_config *config)
{
	struct stat st;
	char *text;
	ssize_t got;
	int fd = open(GKD_INPUT_CONFIG, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	int result = -1;

	if (fd < 0 || fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != 0 ||
	    st.st_nlink != 1 || (st.st_mode & 0022) || st.st_size <= 0 ||
	    st.st_size > (off_t)GKD_INPUT_MAX_CONFIG)
		goto out;
	text = malloc((size_t)st.st_size + 1U);
	if (!text)
		goto out;
	got = read(fd, text, (size_t)st.st_size);
	if (got == st.st_size && !memchr(text, 0, (size_t)st.st_size)) {
		text[st.st_size] = 0;
		result = parse_config_text(text, config);
	}
	free(text);
out:
	if (fd >= 0)
		(void)close(fd);
	return result;
}

static struct key_map *find_key(struct input_config *config, const char *name)
{
	unsigned int i;
	for (i = 0; i < sizeof(config->keys) / sizeof(config->keys[0]); ++i)
		if (!strcmp(name, config->keys[i].logical))
			return &config->keys[i];
	return NULL;
}

static int emit_event(int fd, unsigned short type, unsigned short code, int value)
{
	struct input_event event;
	ssize_t written;
	memset(&event, 0, sizeof(event));
	event.type = type;
	event.code = code;
	event.value = value;
	do {
		written = write(fd, &event, sizeof(event));
	} while (written < 0 && errno == EINTR);
	return written == (ssize_t)sizeof(event) ? 0 : -1;
}

static int emit_key(int fd, unsigned short code, int value)
{
	return emit_event(fd, EV_KEY, code, value) < 0 ||
	       emit_event(fd, EV_SYN, SYN_REPORT, 0) < 0 ? -1 : 0;
}

static unsigned int gkd_dev_major(dev_t device)
{
	return (unsigned int)(((unsigned long)device >> 8) & 0xfffUL);
}

static unsigned int gkd_dev_minor(dev_t device)
{
	return (unsigned int)(((unsigned long)device & 0xffUL) |
		(((unsigned long)device >> 12) & 0xfff00UL));
}

static int open_uinput(struct input_config *config)
{
	struct uinput_setup setup;
	struct stat st;
	const char *step = "open";
	unsigned int i;
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		fd = open("/dev/input/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		goto fail;
	step = "identity";
	if (fstat(fd, &st) < 0 || !S_ISCHR(st.st_mode) ||
	    gkd_dev_major(st.st_rdev) != 10U || gkd_dev_minor(st.st_rdev) != 223U) {
		errno = ENODEV;
		goto fail;
	}
	step = "event-bit";
	if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0)
		goto fail;
	step = "key-bits";
	for (i = 0; i < sizeof(config->keys) / sizeof(config->keys[0]); ++i)
		if (ioctl(fd, UI_SET_KEYBIT, config->keys[i].code) < 0)
			goto fail;
	memset(&setup, 0, sizeof(setup));
	setup.id.bustype = BUS_VIRTUAL;
	setup.id.vendor = 0x474b;
	setup.id.product = 0x0281;
	setup.id.version = 1;
	strncpy(setup.name, GKD_INPUT_NAME, sizeof(setup.name) - 1U);
	step = "create";
	if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0)
		goto fail;
	return fd;
fail:
	fprintf(stderr, "gkd-input: uinput %s failed: %s\n", step, strerror(errno));
	if (fd >= 0)
		(void)close(fd);
	return -1;
}

static int make_address(struct sockaddr_un *address, socklen_t *length)
{
	size_t size = sizeof(GKD_INPUT_SOCKET) - 1U;
	memset(address, 0, sizeof(*address));
	address->sun_family = AF_UNIX;
	address->sun_path[0] = 0;
	memcpy(address->sun_path + 1, GKD_INPUT_SOCKET, size);
	*length = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1U + size);
	return 0;
}

static int listen_socket(void)
{
	struct sockaddr_un address;
	socklen_t length;
	int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;
	make_address(&address, &length);
	if (bind(fd, (struct sockaddr *)&address, length) < 0 ||
	    listen(fd, GKD_INPUT_BACKLOG) < 0) {
		(void)close(fd);
		return -1;
	}
	return fd;
}

static int peer_is_root(int fd)
{
	struct ucred credentials;
	socklen_t length = sizeof(credentials);
	return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) == 0 &&
	       length == sizeof(credentials) && credentials.uid == 0;
}

static int send_reply(int fd, int okay)
{
	const char *reply = okay ? "OK\n" : "ERR\n";
	return send(fd, reply, strlen(reply), MSG_NOSIGNAL) == (ssize_t)strlen(reply)
	       ? 0 : -1;
}

static int release_all(int uinput, unsigned char held[KEY_MAX + 1])
{
	unsigned int code;
	int result = 0;
	for (code = 0; code <= KEY_MAX; ++code) {
		if (held[code]) {
			if (emit_key(uinput, (unsigned short)code, 0) < 0)
				result = -1;
			held[code] = 0;
		}
	}
	return result;
}

#define GKD_PRIORITY_INPUT_USB_EXE "/usr/sbin/gkd-usb-chooser"
#define GKD_PRIORITY_INPUT_POWER_EXE "/usr/sbin/gkd-power-menu"
#define GKD_PRIORITY_INPUT_RETURN_EXE "/usr/sbin/gkd-round80-return-agent"
#define GKD_PRIORITY_INPUT_RECOVERY_EXE "/usr/sbin/gkd-recovery-ui"
static int gkd_priority_physical_chooser_grabs_input(void);

static int gkd_priority_input_test_session(void)
{
	struct stat st;
	if (lstat(GKD_MENU_OWNER_DIR, &st) < 0) return errno == ENOENT ? 0 : -1;
	if (!S_ISDIR(st.st_mode) || st.st_uid != 0 || (st.st_mode & 07777) != 0700 || st.st_nlink < 2) return -1;
	if (lstat(GKD_MENU_TEST_TOKEN, &st) < 0) return errno == ENOENT ? 0 : -1;
	return S_ISREG(st.st_mode) && st.st_uid == 0 && st.st_nlink == 1 &&
		(st.st_mode & 07777) == 0600 && st.st_size == 0 ? 1 : -1;
}

static int gkd_priority_input_proc_start(pid_t pid, unsigned long long *start)
{
	char path[64], value[1024], *cursor, *end;
	ssize_t count; int fd, field;
	if (snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid) >= (int)sizeof(path)) return -1;
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) return -1;
	count = read(fd, value, sizeof(value) - 1U);
	if (close(fd) < 0 || count <= 0 || count >= (ssize_t)sizeof(value)) return -1;
	value[count] = '\0'; cursor = strrchr(value, ')');
	if (!cursor || cursor[1] != ' ') return -1;
	cursor += 2;
	for (field = 3; field <= 22; ++field) {
		end = cursor; while (*end && *end != ' ') ++end;
		if (!*cursor) return -1;
		if (field == 22) {
			char saved = *end, *parsed_end = NULL; unsigned long long parsed;
			*end = '\0'; errno = 0; parsed = strtoull(cursor, &parsed_end, 10);
			if (errno || !parsed || parsed_end != end) { *end = saved; return -1; }
			*end = saved; *start = parsed; return 0;
		}
		if (!*end) return -1;
		cursor = end + 1;
	}
	return -1;
}

static int gkd_priority_input_owner_exe(pid_t pid)
{
	char path[64], target[160]; ssize_t length;
	if (snprintf(path, sizeof(path), "/proc/%ld/exe", (long)pid) >= (int)sizeof(path)) return 0;
	length = readlink(path, target, sizeof(target) - 1U);
	if (length <= 0 || length >= (ssize_t)sizeof(target)) return 0;
	target[length] = '\0';
	return !strcmp(target, GKD_PRIORITY_INPUT_USB_EXE) ||
	       !strcmp(target, GKD_PRIORITY_INPUT_POWER_EXE) ||
	       !strcmp(target, GKD_PRIORITY_INPUT_RETURN_EXE) ||
	       !strcmp(target, GKD_PRIORITY_INPUT_RECOVERY_EXE);
}

static int gkd_priority_input_virtual_fd(pid_t pid)
{
	char path[64], target[160], name[128]; struct dirent *entry; DIR *directory; ssize_t length;
	if (snprintf(path, sizeof(path), "/proc/%ld/fd", (long)pid) >= (int)sizeof(path)) return 0;
	directory = opendir(path); if (!directory) return 0;
	while ((entry = readdir(directory)) != NULL) {
		int fd;
		if (*entry->d_name < '0' || *entry->d_name > '9') continue;
		length = readlinkat(dirfd(directory), entry->d_name, target, sizeof(target) - 1U);
		if (length <= 0 || length >= (ssize_t)sizeof(target)) continue;
		target[length] = '\0'; if (strncmp(target, "/dev/input/event", 16U)) continue;
		fd = open(target, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW); if (fd < 0) continue;
		memset(name, 0, sizeof(name));
		if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0 && !strcmp(name, GKD_MENU_VIRTUAL_NAME)) { close(fd); closedir(directory); return 1; }
		close(fd);
	}
	closedir(directory); return 0;
}

static int gkd_priority_input_marker_owner(void)
{
	char text[160], *end, *start_end; struct stat st; ssize_t count;
	long parsed_pid; unsigned long long expected_start, actual_start; int fd, test_session;
	if (lstat(GKD_MENU_VIRTUAL_GRAB, &st) < 0) return errno == ENOENT ? 0 : -1;
	test_session = gkd_priority_input_test_session();
	if (test_session != 1 || !S_ISREG(st.st_mode) || st.st_uid != 0 || st.st_nlink != 1 ||
	    (st.st_mode & 07777) != 0600 || st.st_size <= 0 || st.st_size >= (off_t)sizeof(text)) return -1;
	fd = open(GKD_MENU_VIRTUAL_GRAB, O_RDONLY | O_CLOEXEC | O_NOFOLLOW); if (fd < 0) return -1;
	count = read(fd, text, sizeof(text) - 1U);
	if (close(fd) < 0 || count != st.st_size) return -1;
	text[count] = '\0'; errno = 0; parsed_pid = strtol(text, &end, 10);
	if (errno || parsed_pid <= 1 || parsed_pid > 4194304 || end == text || *end != ' ') return -1;
	errno = 0; expected_start = strtoull(end + 1, &start_end, 10);
	if (errno || !expected_start || start_end == end + 1 || *start_end != ' ' || strcmp(start_end + 1, GKD_MENU_VIRTUAL_NAME "\n")) return -1;
	if (gkd_priority_input_proc_start((pid_t)parsed_pid, &actual_start) < 0 || actual_start != expected_start ||
	    !gkd_priority_input_owner_exe((pid_t)parsed_pid) || !gkd_priority_input_virtual_fd((pid_t)parsed_pid)) return -1;
	return 1;
}

static int chooser_grabs_input(int require_owner)
{
	int marker = gkd_priority_input_marker_owner();
	if (marker > 0) return 0;
	if (marker < 0 || require_owner) return 1;
	return gkd_priority_physical_chooser_grabs_input();
}

static int gkd_priority_physical_chooser_grabs_input(void)
{
	char text[64], path[96], target[160], name[128];
	struct stat st;
	struct dirent *entry;
	DIR *directory = NULL;
	char *end, *start_end;
	ssize_t length;
	long pid = -1;
	unsigned long long start = 0;
	int virtual_fd = 0;
	int fd = open(GKD_CHOOSER_PIDFILE, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);

	if (fd < 0) return 0;
	if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != 0 ||
	    st.st_nlink != 1 || (st.st_mode & 0022) || st.st_size <= 0 ||
	    st.st_size >= (off_t)sizeof(text)) goto out;
	length = read(fd, text, sizeof(text) - 1U);
	if (length <= 0) goto out;
	text[length] = '\0'; errno = 0;
	pid = strtol(text, &end, 10);
	if (errno || pid <= 1 || pid > 4194304 || end == text || *end != ' ') goto out;
	errno = 0;
	start = strtoull(end + 1, &start_end, 10);
	if (errno || !start || start_end == end + 1 ||
	    (*start_end != '\n' && *start_end != '\0')) goto out;
	if (snprintf(path, sizeof(path), "/proc/%ld/exe", pid) >= (int)sizeof(path)) goto out;
	length = readlink(path, target, sizeof(target) - 1U);
	if (length <= 0 || length >= (ssize_t)sizeof(target)) goto out;
	target[length] = '\0';
	if (strcmp(target, GKD_CHOOSER_EXE)) goto out;
	if (snprintf(path, sizeof(path), "/proc/%ld/fd", pid) >= (int)sizeof(path)) goto out;
	directory = opendir(path);
	if (!directory) goto out;
	while ((entry = readdir(directory)) != NULL) {
		int event_fd;
		if (*entry->d_name < '0' || *entry->d_name > '9') continue;
		length = readlinkat(dirfd(directory), entry->d_name, target, sizeof(target) - 1U);
		if (length <= 0 || length >= (ssize_t)sizeof(target)) continue;
		target[length] = '\0';
		if (strncmp(target, "/dev/input/event", 16U)) continue;
		event_fd = open(target, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
		if (event_fd < 0) continue;
		memset(name, 0, sizeof(name));
		if (ioctl(event_fd, EVIOCGNAME(sizeof(name)), name) >= 0 &&
		    !strcmp(name, GKD_MENU_VIRTUAL_NAME)) virtual_fd = 1;
		close(event_fd);
	}
out:
	if (directory) (void)closedir(directory);
	(void)close(fd);
	/* Only the explicit root test session may inject into the chooser's exact
	 * virtual input after its post-grab identity marker is validated. */
	return virtual_fd && gkd_coherence_virtual_marker((pid_t)pid, start) ? 0 :
		(directory != NULL ? 1 : 0);
}

static int execute_command(char *packet, struct input_config *config, int uinput,
			   unsigned char held[KEY_MAX + 1])
{
	struct key_map *key;
	char *space;
	size_t length;
	if (!strcmp(packet, "STATUS\n"))
		return 0;
	if (!strcmp(packet, "RELEASE_ALL\n"))
		return release_all(uinput, held);
	if (gkd_action_freeze_active() && (!strncmp(packet, "DOWN ", 5) || !strncmp(packet, "PRESS ", 6))) return -1;
	if (chooser_grabs_input(config->require_owner)) {
		errno = EBUSY;
		return -1;
	}
	length = strlen(packet);
	if (!length || packet[length - 1U] != '\n')
		return -1;
	packet[length - 1U] = 0;
	space = strchr(packet, ' ');
	if (!space)
		return -1;
	*space++ = 0;
	key = find_key(config, space);
	if (!key)
		return -1;
	if (!strcmp(packet, "DOWN")) {
		if (held[key->code])
			return 0;
		if (emit_key(uinput, key->code, 1) < 0)
			return -1;
		held[key->code] = 1;
		return 0;
	}
	if (!strcmp(packet, "UP")) {
		if (!held[key->code])
			return 0;
		if (emit_key(uinput, key->code, 0) < 0)
			return -1;
		held[key->code] = 0;
		return 0;
	}
	if (!strcmp(packet, "PRESS")) {
		struct timespec delay;
		if (emit_key(uinput, key->code, 1) < 0)
			return -1;
		delay.tv_sec = config->press_ms / 1000U;
		delay.tv_nsec = (long)(config->press_ms % 1000U) * 1000000L;
		while (nanosleep(&delay, &delay) < 0 && errno == EINTR)
			;
		return emit_key(uinput, key->code, 0);
	}
	return -1;
}

static int serve_client(int client, struct input_config *config, int uinput,
			unsigned char held[KEY_MAX + 1])
{
	char packet[GKD_INPUT_MAX_PACKET + 1U];
	ssize_t size = recv(client, packet, GKD_INPUT_MAX_PACKET, MSG_TRUNC);
	int okay = 0;
	if (peer_is_root(client) && size > 0 &&
	    size <= (ssize_t)GKD_INPUT_MAX_PACKET) {
		packet[size] = 0;
		okay = execute_command(packet, config, uinput, held) == 0;
	}
	(void)send_reply(client, okay);
	return okay ? 0 : -1;
}

static void stop_handler(int signal_number)
{
	(void)signal_number;
	stopping = 1;
}

static int daemon_main(int require_owner)
{
	struct input_config config;
	struct sigaction action;
	unsigned char held[KEY_MAX + 1];
	int uinput = -1;
	int listener = -1;
	int result = 1;
	memset(held, 0, sizeof(held));
	if (load_config(&config) < 0)
		goto out;
	config.require_owner = require_owner;
	if ((uinput = open_uinput(&config)) < 0 ||
	    (listener = listen_socket()) < 0)
		goto out;
	memset(&action, 0, sizeof(action));
	action.sa_handler = stop_handler;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGTERM, &action, NULL) < 0 ||
	    sigaction(SIGINT, &action, NULL) < 0)
		goto out;
	action.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &action, NULL) < 0)
		goto out;
	while (!stopping) {
		int client = accept4(listener, NULL, NULL, SOCK_CLOEXEC);
		if (client < 0) {
			if (errno == EINTR)
				continue;
			goto out;
		}
		(void)serve_client(client, &config, uinput, held);
		(void)close(client);
	}
	result = 0;
out:
	if (uinput >= 0) {
		(void)release_all(uinput, held);
		(void)ioctl(uinput, UI_DEV_DESTROY);
		(void)close(uinput);
	}
	if (listener >= 0)
		(void)close(listener);
	return result;
}

static int send_command(const char *command, const char *logical)
{
	struct sockaddr_un address;
	socklen_t length;
	char packet[GKD_INPUT_MAX_PACKET + 1U];
	char reply[8];
	int fd;
	int size;
	ssize_t got;
	make_address(&address, &length);
	if (logical)
		size = snprintf(packet, sizeof(packet), "%s %s\n", command, logical);
	else
		size = snprintf(packet, sizeof(packet), "%s\n", command);
	if (size <= 0 || (size_t)size > GKD_INPUT_MAX_PACKET)
		return -1;
	fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
	if (fd < 0 || connect(fd, (struct sockaddr *)&address, length) < 0)
		goto fail;
	if (send(fd, packet, (size_t)size, MSG_NOSIGNAL) != size)
		goto fail;
	got = recv(fd, reply, sizeof(reply), MSG_TRUNC);
	if (got != 3 || memcmp(reply, "OK\n", 3))
		goto fail;
	(void)close(fd);
	return 0;
fail:
	if (fd >= 0)
		(void)close(fd);
	return -1;
}

static int self_test(void)
{
	char fixture[] =
		"input_inject_press_ms=80\ninput_map_dpad_up=KEY_UP\n"
		"input_map_dpad_down=KEY_DOWN\ninput_map_dpad_left=KEY_LEFT\n"
		"input_map_dpad_right=KEY_RIGHT\ninput_map_a=KEY_LEFTCTRL\n"
		"input_map_b=KEY_LEFTALT\ninput_map_x=KEY_SPACE\n"
		"input_map_y=KEY_LEFTSHIFT\ninput_map_start=KEY_ENTER\n"
		"input_map_select=KEY_ESC\ninput_map_l1=KEY_TAB\n"
		"input_map_r1=KEY_BACKSPACE\ninput_map_l2=KEY_PAGEUP\n"
		"input_map_r2=KEY_PAGEDOWN\ninput_map_volume_up=KEY_KPPLUS\n"
		"input_map_volume_down=KEY_KPMINUS\ninput_map_brightness=KEY_END\n"
		"input_map_menu=KEY_HOME\ninput_map_side_dot=KEY_LEFTCTRL\n"
		"input_map_side_double_dot=KEY_LEFTALT\n";
	struct input_config config;
	struct key_map *menu;
	if (parse_config_text(fixture, &config) < 0 || config.press_ms != 80)
		return 1;
	if (chooser_grabs_input(1) != 1)
		return 3;
	menu = find_key(&config, "menu");
	if (!menu || menu->code != KEY_HOME || find_key(&config, "missing"))
		return 2;
	puts("GKD_INPUT_SELF_TEST=PASS owner-required=fail-closed");
	return 0;
}

int main(int argc, char **argv)
{
	const char *command = NULL;
	const char *logical = NULL;
	if (argc == 2 && !strcmp(argv[1], "daemon"))
		return daemon_main(0);
	if (argc == 3 && !strcmp(argv[1], "daemon") &&
	    !strcmp(argv[2], "--require-owner"))
		return daemon_main(1);
	if (argc == 2 && !strcmp(argv[1], "--self-test"))
		return self_test();
	if (argc == 2 && !strcmp(argv[1], "status"))
		command = "STATUS";
	else if (argc == 2 && !strcmp(argv[1], "release-all"))
		command = "RELEASE_ALL";
	else if (argc == 3 && (!strcmp(argv[1], "press") ||
		 !strcmp(argv[1], "down") || !strcmp(argv[1], "up"))) {
		command = !strcmp(argv[1], "press") ? "PRESS" :
			  (!strcmp(argv[1], "down") ? "DOWN" : "UP");
		logical = argv[2];
	}
	if (!command) {
		fprintf(stderr, "usage: %s daemon [--require-owner]|status|press KEY|down KEY|up KEY|release-all|--self-test\n", argv[0]);
		return 64;
	}
	if (send_command(command, logical) < 0) {
		fprintf(stderr, "gkd-input: request failed\n");
		return 1;
	}
	return 0;
}
