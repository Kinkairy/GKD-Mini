// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define I2C_M_RD 0x0001
#define I2C_RDWR 0x0707
#define I2C_DEVICE "/dev/i2c-1"
#define AXP173_ADDRESS 0x34
#define AXP173_OPERATING_MODE 0x01
#define AXP173_CAPACITY 0xb9
#define AXP173_POWER_OFF_CTL 0x32
#define AXP173_POWER_OFF 0x80
#define SHUTDOWN_MARKER "/run/gkd-power-menu/shutdown-requested"
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

struct i2c_msg {
	uint16_t addr;
	uint16_t flags;
	uint16_t len;
	uint8_t *buf;
};

struct i2c_rdwr_ioctl_data {
	struct i2c_msg *msgs;
	uint32_t nmsgs;
};

#ifdef RC285_PMIC_FIXTURE
static uint8_t fixture_registers[256];
static unsigned fixture_writes;
static int transfer_i2c(int fd, struct i2c_rdwr_ioctl_data *transfer)
{
	struct i2c_msg *messages = transfer->msgs;
	(void)fd;
	if (transfer->nmsgs == 2 && messages[0].addr == AXP173_ADDRESS &&
	    messages[0].flags == 0 && messages[0].len == 1 &&
	    messages[1].addr == AXP173_ADDRESS && messages[1].flags == I2C_M_RD &&
	    messages[1].len == 1) {
		messages[1].buf[0] = fixture_registers[messages[0].buf[0]];
		return 2;
	}
	if (transfer->nmsgs == 1 && messages[0].addr == AXP173_ADDRESS &&
	    messages[0].flags == 0 && messages[0].len == 2) {
		fixture_registers[messages[0].buf[0]] = messages[0].buf[1];
		fixture_writes++;
		return 1;
	}
	errno = EIO;
	return -1;
}
#else
static int transfer_i2c(int fd, struct i2c_rdwr_ioctl_data *transfer)
{
	return ioctl(fd, I2C_RDWR, transfer);
}
#endif

static int i2c_read_reg(int fd, uint8_t reg, uint8_t *value)
{
	struct i2c_msg messages[2];
	struct i2c_rdwr_ioctl_data transfer;
	messages[0].addr = AXP173_ADDRESS;
	messages[0].flags = 0;
	messages[0].len = 1;
	messages[0].buf = &reg;
	messages[1].addr = AXP173_ADDRESS;
	messages[1].flags = I2C_M_RD;
	messages[1].len = 1;
	messages[1].buf = value;
	transfer.msgs = messages;
	transfer.nmsgs = 2;
	return transfer_i2c(fd, &transfer) == 2 ? 0 : -1;
}

static int i2c_write_reg(int fd, uint8_t reg, uint8_t value)
{
	uint8_t payload[2] = { reg, value };
	struct i2c_msg message;
	struct i2c_rdwr_ioctl_data transfer;
	message.addr = AXP173_ADDRESS;
	message.flags = 0;
	message.len = sizeof(payload);
	message.buf = payload;
	transfer.msgs = &message;
	transfer.nmsgs = 1;
	return transfer_i2c(fd, &transfer) == 1 ? 0 : -1;
}

static int probe_axp173(int fd)
{
	uint8_t mode, capacity;
	return i2c_read_reg(fd, AXP173_OPERATING_MODE, &mode) == 0 &&
		i2c_read_reg(fd, AXP173_CAPACITY, &capacity) == 0 &&
		(capacity & 0x7f) <= 100 ? 0 : -1;
}

static int request_axp173_poweroff(int fd)
{
	uint8_t value;
	if (probe_axp173(fd) < 0 ||
	    i2c_read_reg(fd, AXP173_POWER_OFF_CTL, &value) < 0)
		return -1;
	return i2c_write_reg(fd, AXP173_POWER_OFF_CTL,
			     (uint8_t)(value | AXP173_POWER_OFF));
}

#ifndef RC285_PMIC_FIXTURE
static int marker_requests_poweroff(void)
{
	static const char expected[] = "poweroff\n";
	char payload[sizeof(expected)];
	struct stat status;
	ssize_t count;
	int fd = open(SHUTDOWN_MARKER, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) return 0;
	if (fstat(fd, &status) || !S_ISREG(status.st_mode) || status.st_uid != 0 ||
	    status.st_nlink != 1 || (status.st_mode & 0077) ||
	    status.st_size != (off_t)(sizeof(expected) - 1)) {
		close(fd);
		return 0;
	}
	count = read(fd, payload, sizeof(payload));
	close(fd);
	return count == (ssize_t)(sizeof(expected) - 1) &&
		memcmp(payload, expected, sizeof(expected) - 1) == 0;
}

static int run_command(const char *path, char *const arguments[])
{
	int status;
	pid_t child = fork();
	if (child < 0) return -1;
	if (child == 0) { execv(path, arguments); _exit(127); }
	while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return -1;
	return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static void signal_other_processes(int signal_number)
{
	DIR *directory;
	struct dirent *entry;
	char *end;
	long candidate;
	pid_t self = getpid();
	directory = opendir("/proc");
	if (!directory) return;
	while ((entry = readdir(directory)) != NULL) {
		errno = 0;
		candidate = strtol(entry->d_name, &end, 10);
		if (errno || *entry->d_name == '\0' || *end != '\0' ||
		    candidate <= 1 || candidate == (long)self)
			continue;
		(void)kill((pid_t)candidate, signal_number);
	}
	closedir(directory);
}

static void quiesce_for_poweroff(void)
{
	struct timespec graceful_wait = { 1, 0 };
	struct timespec forced_wait = { 0, 100000000 };
	/* BusyBox init runs shutdown actions before its own process sweep. */
	signal_other_processes(SIGTERM);
	nanosleep(&graceful_wait, NULL);
	signal_other_processes(SIGKILL);
	nanosleep(&forced_wait, NULL);
}

static int option_is_rw(const char *options)
{
	const char *cursor = options;
	while (*cursor) {
		const char *end = strchr(cursor, ',');
		size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
		if (length == 2 && !memcmp(cursor, "rw", 2)) return 1;
		if (!end) break;
		cursor = end + 1;
	}
	return 0;
}

static int persistent_mounts_safe(int mount_fd)
{
	char line[768], source[160], target[160], type[48], options[320];
	FILE *mounts;
	int duplicate;
	if (lseek(mount_fd, 0, SEEK_SET) < 0) return 0;
	duplicate = dup(mount_fd);
	if (duplicate < 0) return 0;
	mounts = fdopen(duplicate, "r");
	if (!mounts) { close(duplicate); return 0; }
	while (fgets(line, sizeof(line), mounts)) {
		if (sscanf(line, "%159s %159s %47s %319s", source, target, type,
			   options) != 4) { fclose(mounts); return 0; }
		if ((!strncmp(source, "/dev/mmcblk", 11) || !strcmp(source, "/dev/root")) &&
		    option_is_rw(options)) { fclose(mounts); return 0; }
	}
	if (ferror(mounts)) { fclose(mounts); return 0; }
	fclose(mounts);
	return 1;
}

static int remount_persistent_readonly(int mount_fd)
{
	char line[768], source[160], target[160], type[48], options[320];
	char targets[8][160];
	FILE *mounts;
	unsigned count = 0, index;
	int duplicate;
	if (lseek(mount_fd, 0, SEEK_SET) < 0) return -1;
	duplicate = dup(mount_fd);
	if (duplicate < 0) return -1;
	mounts = fdopen(duplicate, "r");
	if (!mounts) { close(duplicate); return -1; }
	while (fgets(line, sizeof(line), mounts)) {
		if (sscanf(line, "%159s %159s %47s %319s", source, target, type,
			   options) != 4) { fclose(mounts); return -1; }
		if (strncmp(source, "/dev/mmcblk", 11) && strcmp(source, "/dev/root"))
			continue;
		if (!option_is_rw(options)) continue;
		if (count == ARRAY_SIZE(targets) || strchr(target, '\\')) {
			fclose(mounts); return -1;
		}
		strcpy(targets[count++], target);
	}
	if (ferror(mounts)) { fclose(mounts); return -1; }
	fclose(mounts);
	/* Remount the root filesystem last so all preceding lookups stay available. */
	for (index = 0; index < count; ++index)
		if (strcmp(targets[index], "/") &&
		    mount(NULL, targets[index], NULL, MS_REMOUNT | MS_RDONLY, NULL) < 0)
			return -1;
	for (index = 0; index < count; ++index)
		if (!strcmp(targets[index], "/") &&
		    mount(NULL, targets[index], NULL, MS_REMOUNT | MS_RDONLY, NULL) < 0)
			return -1;
	return 0;
}

static void console_message(const char *message)
{
	int fd = open("/dev/console", O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd >= 0) { (void)write(fd, message, strlen(message)); close(fd); }
}

int main(int argc, char **argv)
{
	char *const umount_args[] = { (char *)"umount", (char *)"-a", (char *)"-r", NULL };
	char *const swapoff_args[] = { (char *)"swapoff", (char *)"-a", NULL };
	struct timespec wait = { 2, 0 };
	int requested, mount_fd, i2c_fd = -1;
	(void)argv;
	if (argc != 1) return 64;
	requested = marker_requests_poweroff();
	mount_fd = open("/proc/self/mounts", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (mount_fd < 0) return 65;
	if (requested) {
		i2c_fd = open(I2C_DEVICE, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
		if (i2c_fd < 0 || probe_axp173(i2c_fd) < 0) {
			console_message("gkd-pmic-poweroff: exact AXP173 unavailable\n");
			if (i2c_fd >= 0) close(i2c_fd);
			close(mount_fd);
			return 66;
		}
	}
	if (requested) quiesce_for_poweroff();
	sync();
	if (requested) {
		if (remount_persistent_readonly(mount_fd) < 0) {
			console_message("gkd-pmic-poweroff: persistent remount failed\n");
			close(i2c_fd); close(mount_fd); return 67;
		}
	} else {
		(void)run_command("/bin/umount", umount_args);
	}
	(void)run_command("/sbin/swapoff", swapoff_args);
	sync();
	if (!requested) { close(mount_fd); return 0; }
	close(mount_fd);
	mount_fd = open("/proc/self/mounts", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (mount_fd < 0) { close(i2c_fd); return 67; }
	if (!persistent_mounts_safe(mount_fd)) {
		console_message("gkd-pmic-poweroff: writable media remains; refusing cutoff\n");
		close(i2c_fd); close(mount_fd); return 67;
	}
	close(mount_fd);
	if (request_axp173_poweroff(i2c_fd) < 0) {
		console_message("gkd-pmic-poweroff: AXP173 cutoff write failed\n");
		close(i2c_fd); return 68;
	}
	nanosleep(&wait, NULL);
	console_message("gkd-pmic-poweroff: cutoff did not complete\n");
	close(i2c_fd);
	return 69;
}
#else
int main(void)
{
	fixture_registers[AXP173_OPERATING_MODE] = 0x30;
	fixture_registers[AXP173_CAPACITY] = 0x80 | 50;
	fixture_registers[AXP173_POWER_OFF_CTL] = 0x05;
	if (request_axp173_poweroff(7) < 0 || fixture_writes != 1 ||
	    fixture_registers[AXP173_POWER_OFF_CTL] != 0x85) return 1;
	fixture_writes = 0;
	fixture_registers[AXP173_CAPACITY] = 0x80 | 101;
	if (request_axp173_poweroff(7) == 0 || fixture_writes != 0) return 2;
	puts("RC285_PMIC_POWEROFF_FIXTURE=PASS axp173_reg32_bit7=1 fail_closed=1");
	return 0;
}
#endif
