#define _FILE_OFFSET_BITS 64
#define _XOPEN_SOURCE 700
#include "gkd-update-journal.h"
#include "gkd-update-request.h"
#include "gkd-update-sha256.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef GKDU_KERNEL_BYTES
#define GKDU_KERNEL_BYTES 6291456ULL
#endif
#ifndef GKDU_RECOVERY_BYTES
#define GKDU_RECOVERY_BYTES 1073741824ULL
#endif

#ifdef GKDU_MINIMAL_RUNTIME
static int fail_minimal(void) { return 2; }
#define fail(why) fail_minimal()
#define reportf(...) do { if (0) printf(__VA_ARGS__); } while (0)
#define reports(message) do { if (0) puts(message); } while (0)
#else
static int fail(const char *why)
{
	fprintf(stderr, "GKDSU_COORDINATOR=BLOCKED reason=%s\n", why);
	return 2;
}
#define reportf(...) printf(__VA_ARGS__)
#define reports(message) puts(message)
#endif

static void hex(const uint8_t bytes[32], char out[65])
{
	static const char digits[] = "0123456789abcdef";
	unsigned i;
	for (i = 0; i < 32; ++i) {
		out[i * 2] = digits[bytes[i] >> 4];
		out[i * 2 + 1] = digits[bytes[i] & 15];
	}
	out[64] = 0;
}

static int file_hash(const char *path, uint64_t expected_bytes, uint8_t out[32])
{
	struct gkdu_sha256 ctx;
	uint8_t buffer[1 << 16];
	struct stat st;
	uint64_t done = 0;
	int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) ||
	    (uint64_t)st.st_size != expected_bytes) goto bad;
	gkdu_sha256_init(&ctx);
	while (done < expected_bytes) {
		size_t wanted = expected_bytes - done > sizeof(buffer) ?
			sizeof(buffer) : (size_t)(expected_bytes - done);
		ssize_t got = read(fd, buffer, wanted);
		if (got < 0 && errno == EINTR) continue;
		if (got <= 0) goto bad;
		gkdu_sha256_update(&ctx, buffer, (size_t)got); done += (uint64_t)got;
	}
	gkdu_sha256_final(&ctx, out);
	return close(fd);
bad:
	if (fd >= 0) close(fd);
	return -1;
}

static int invoke(char *const argv[])
{
	int status;
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (!pid) { execv(argv[0], argv); _exit(127); }
	do { pid = waitpid(pid, &status, 0); } while (pid < 0 && errno == EINTR);
	return pid > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static int engine_backup(const char *engine, char **argv,
		const struct gkdu_request *request)
{
	char package[65], source_p1[65], source_kernel[65], target_p1[65], target_kernel[65];
	char *args[] = {(char *)engine, "backup", argv[2], argv[3], argv[4], package,
		source_p1, source_kernel, target_p1, target_kernel, 0};
	hex(request->package_sha256, package); hex(request->source_p1_sha256, source_p1);
	hex(request->source_kernel_sha256, source_kernel);
	hex(request->target_p1_sha256, target_p1);
	hex(request->target_kernel_sha256, target_kernel);
	return invoke(args);
}

static int engine_apply(const char *engine, char **argv,
		const struct gkdu_request *request)
{
	char p1_offset[24], p1_bytes[24], kernel_offset[24], kernel_bytes[24];
	char *args[] = {(char *)engine, "apply-package", argv[2], argv[3], argv[4],
		argv[1], p1_offset, p1_bytes, kernel_offset, kernel_bytes, 0};
	snprintf(p1_offset, sizeof(p1_offset), "%llu",
		(unsigned long long)request->p1_payload_offset);
	snprintf(p1_bytes, sizeof(p1_bytes), "%llu",
		(unsigned long long)request->p1_payload_bytes);
	snprintf(kernel_offset, sizeof(kernel_offset), "%llu",
		(unsigned long long)request->kernel_payload_offset);
	snprintf(kernel_bytes, sizeof(kernel_bytes), "%llu",
		(unsigned long long)request->kernel_payload_bytes);
	return invoke(args);
}

static int engine_simple(const char *engine, const char *command, char **argv)
{
	char *args[] = {(char *)engine, (char *)command, argv[2], argv[3], argv[4], 0};
	return invoke(args);
}

static int verify_package(const char *path, const struct gkdu_request *request)
{
	uint8_t actual[32];
	if (!request->package_bytes ||
	    request->p1_payload_offset > request->package_bytes ||
	    request->p1_payload_bytes > request->package_bytes - request->p1_payload_offset ||
	    request->kernel_payload_offset != request->p1_payload_offset + request->p1_payload_bytes ||
	    request->kernel_payload_bytes != GKDU_KERNEL_BYTES ||
	    request->kernel_payload_bytes > request->package_bytes - request->kernel_payload_offset ||
	    request->kernel_payload_offset + request->kernel_payload_bytes != request->package_bytes ||
	    file_hash(path, request->package_bytes, actual) ||
	    memcmp(actual, request->package_sha256, 32)) return -1;
	return 0;
}

static int finalize(int recovery_fd, int disk_fd, const struct gkdu_request *request,
		const char *release)
{
	if (gkdu_recovery_format_swap(recovery_fd) || gkdu_request_clear(disk_fd, request))
		return fail("finalize");
	reportf("GKDSU_COORDINATOR=PASS action=finalize release=%s\n", release);
	return 20;
}

int main(int argc, char **argv)
{
	struct gkdu_request request;
	struct gkdu_journal journal;
	int disk_fd, recovery_fd, state_status;
	if (argc != 6) return fail("usage");
	disk_fd = open(argv[4], O_RDWR | O_SYNC | O_NOFOLLOW);
	recovery_fd = open(argv[2], O_RDWR | O_SYNC | O_NOFOLLOW);
	if (disk_fd < 0 || recovery_fd < 0 || gkdu_request_read(disk_fd, &request))
		return fail("request");
	state_status = gkdu_journal_read(recovery_fd, &journal, 0);
	if (state_status < 0) return fail("journal");
	if (!state_status && memcmp(journal.package_sha256, request.package_sha256, 32))
		return fail("transaction-binding");
	if (!state_status && (journal.state == GKDU_STATE_APPLYING_P1 ||
	    journal.state == GKDU_STATE_APPLYING_KERNEL ||
	    journal.state == GKDU_STATE_ROLLING_BACK ||
	    journal.state == GKDU_STATE_TRIAL_BOOTING)) {
		if (engine_simple(argv[5], "restore", argv)) return fail("restore");
		return finalize(recovery_fd, disk_fd, &request, "old");
	}
	if (!state_status && journal.state == GKDU_STATE_RESTORED) {
		return finalize(recovery_fd, disk_fd, &request, "old");
	}
	if (!state_status && journal.state == GKDU_STATE_TRIAL_GOOD) {
		return finalize(recovery_fd, disk_fd, &request, "target");
	}
	if (!state_status && journal.state == GKDU_STATE_TRIAL_PENDING) {
#if GKDU_RECOVERY_HOST
		/* Only the target A boot may consume its trial attempt. Consuming it
		 * here would make A immediately classify that first boot as failed. */
		reports("GKDSU_COORDINATOR=PASS action=reboot-for-trial");
		close(recovery_fd); close(disk_fd); return 10;
#else
		if (engine_simple(argv[5], "begin-trial", argv)) return fail("begin-trial");
		reports("GKDSU_COORDINATOR=PASS action=boot-target");
		close(recovery_fd); close(disk_fd); return 0;
#endif
	}
	if (state_status || journal.state == GKDU_STATE_BACKUP_WRITING ||
	    journal.state == GKDU_STATE_BACKUP_READY) {
		if (verify_package(argv[1], &request)) return fail("package");
		if (state_status || journal.state == GKDU_STATE_BACKUP_WRITING) {
			if (engine_backup(argv[5], argv, &request)) return fail("backup");
		}
		if (engine_apply(argv[5], argv, &request)) return fail("apply");
		reports("GKDSU_COORDINATOR=PASS action=reboot-for-trial");
		close(recovery_fd); close(disk_fd); return 10;
	}
	return fail("state");
}
