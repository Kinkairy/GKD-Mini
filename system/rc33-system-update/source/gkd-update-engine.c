#define _FILE_OFFSET_BITS 64
#define _XOPEN_SOURCE 700
#include "gkd-update-journal.h"
#include "gkd-update-sha256.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/fs.h>
#include <sys/ioctl.h>
#endif

#ifndef GKDU_P1_BYTES
#define GKDU_P1_BYTES 805306880ULL
#endif
#ifndef GKDU_KERNEL_BYTES
#define GKDU_KERNEL_BYTES 6291456ULL
#endif
#ifndef GKDU_KERNEL_SLOT_OFFSET
#define GKDU_KERNEL_SLOT_OFFSET 9437184ULL
#endif
#ifndef GKDU_RECOVERY_BYTES
#define GKDU_RECOVERY_BYTES 1073741824ULL
#endif
#define IO_BYTES 65536u

static unsigned long long write_count;

#ifdef GKDU_ENABLE_FAULT_INJECTION
int gkdu_before_pwrite(void)
{
	const char *value = getenv("GKDU_FAIL_AFTER");
	char *end = 0;
	unsigned long long limit;
	if (value && *value) {
		errno = 0; limit = strtoull(value, &end, 10);
		if (errno || !end || *end) _exit(98);
		if (write_count == limit) _exit(99);
	}
	++write_count;
	return 0;
}
#else
extern int gkdu_before_pwrite(void);
#endif

#ifdef GKDU_MINIMAL_RUNTIME
static int fail_minimal(void) { return 1; }
#define fail(message) fail_minimal()
#define reportf(...) do { if (0) printf(__VA_ARGS__); } while (0)
#define reports(message) do { if (0) puts(message); } while (0)
#else
static int fail(const char *message)
{
	fprintf(stderr, "GKDSU_ENGINE=FAIL reason=%s writes=%llu\n", message,
		write_count);
	return 1;
}
#define reportf(...) printf(__VA_ARGS__)
#define reports(message) puts(message)
#endif

static int open_ro(const char *path)
{
	return open(path, O_RDONLY | O_CLOEXEC);
}

static int open_rw(const char *path)
{
	return open(path, O_RDWR | O_CLOEXEC | O_SYNC);
}

static int fd_bytes(int fd, uint64_t *bytes)
{
	struct stat st;
	if (fstat(fd, &st)) return -1;
	if (S_ISREG(st.st_mode)) { *bytes = (uint64_t)st.st_size; return 0; }
#ifdef BLKGETSIZE64
	if (S_ISBLK(st.st_mode)) {
		unsigned long long value;
		if (ioctl(fd, BLKGETSIZE64, &value)) return -1;
		*bytes = (uint64_t)value; return 0;
	}
#endif
	return -1;
}

static int exact_read(int fd, uint8_t *data, size_t bytes, uint64_t offset)
{
	size_t done = 0;
	while (done < bytes) {
		ssize_t count = pread(fd, data + done, bytes - done,
			(off_t)(offset + done));
		if (count < 0 && errno == EINTR) continue;
		if (count <= 0) return -1;
		done += (size_t)count;
	}
	return 0;
}

static int exact_write(int fd, const uint8_t *data, size_t bytes, uint64_t offset)
{
	size_t done = 0;
	while (done < bytes) {
		ssize_t count;
		if (gkdu_before_pwrite()) return -1;
		count = pwrite(fd, data + done, bytes - done, (off_t)(offset + done));
		if (count < 0 && errno == EINTR) continue;
		if (count <= 0) return -1;
		done += (size_t)count;
	}
	return 0;
}

static int parse_hash(const char *text, uint8_t out[32])
{
	unsigned i;
	if (!text || strlen(text) != 64) return -1;
	for (i = 0; i < 32; ++i) {
		unsigned value;
		if (sscanf(text + i * 2, "%2x", &value) != 1) return -1;
		out[i] = (uint8_t)value;
	}
	return 0;
}

#ifndef GKDU_MINIMAL_RUNTIME
static void print_hash(const uint8_t hash[32])
{
	unsigned i;
	for (i = 0; i < 32; ++i) printf("%02x", hash[i]);
}
#endif

static int hash_region(int fd, uint64_t offset, uint64_t bytes, uint8_t out[32])
{
	struct gkdu_sha256 context;
	uint8_t buffer[IO_BYTES];
	uint64_t done = 0;
	gkdu_sha256_init(&context);
	while (done < bytes) {
		size_t wanted = (size_t)((bytes - done) < sizeof(buffer) ?
			(bytes - done) : sizeof(buffer));
		if (exact_read(fd, buffer, wanted, offset + done)) return -1;
		gkdu_sha256_update(&context, buffer, wanted); done += wanted;
	}
	gkdu_sha256_final(&context, out);
	return 0;
}

static int copy_region(int source, uint64_t source_offset, int target,
		uint64_t target_offset, uint64_t bytes, uint8_t out[32])
{
	struct gkdu_sha256 context;
	uint8_t buffer[IO_BYTES];
	uint64_t done = 0;
	gkdu_sha256_init(&context);
	while (done < bytes) {
		size_t wanted = (size_t)((bytes - done) < sizeof(buffer) ?
			(bytes - done) : sizeof(buffer));
		if (exact_read(source, buffer, wanted, source_offset + done) ||
		    exact_write(target, buffer, wanted, target_offset + done)) return -1;
		gkdu_sha256_update(&context, buffer, wanted); done += wanted;
	}
	gkdu_sha256_final(&context, out);
	return 0;
}

static int geometry(int recovery, int p1, int disk)
{
	uint64_t recovery_bytes, p1_bytes, disk_bytes;
	if (fd_bytes(recovery, &recovery_bytes) || fd_bytes(p1, &p1_bytes) ||
	    fd_bytes(disk, &disk_bytes)) return -1;
	if (recovery_bytes != GKDU_RECOVERY_BYTES || p1_bytes != GKDU_P1_BYTES ||
	    disk_bytes < GKDU_KERNEL_SLOT_OFFSET + GKDU_KERNEL_BYTES ||
	    GKDU_P1_BACKUP_OFFSET >= GKDU_RECOVERY_BYTES ||
	    GKDU_KERNEL_BACKUP_OFFSET + GKDU_KERNEL_BYTES > GKDU_P1_BACKUP_OFFSET)
		return -1;
	return 0;
}

static int set_state(int recovery, struct gkdu_journal *journal, uint32_t state)
{
	journal->sequence++; journal->state = state;
	return gkdu_journal_write_next(recovery, journal, 0);
}

static int backup(int argc, char **argv)
{
	struct gkdu_journal journal, prior;
	uint8_t actual[32], readback[32];
	int recovery = -1, p1 = -1, disk = -1, status;
	if (argc != 10) return fail("backup-usage");
	recovery = open_rw(argv[2]); p1 = open_ro(argv[3]); disk = open_ro(argv[4]);
	if (recovery < 0 || p1 < 0 || disk < 0 || geometry(recovery, p1, disk))
		goto geometry_fail;
	memset(&journal, 0, sizeof(journal));
	if (parse_hash(argv[5], journal.package_sha256) ||
	    parse_hash(argv[6], journal.source_p1_sha256) ||
	    parse_hash(argv[7], journal.source_kernel_sha256) ||
	    parse_hash(argv[8], journal.target_p1_sha256) ||
	    parse_hash(argv[9], journal.target_kernel_sha256)) goto hash_fail;
	if (hash_region(p1, 0, GKDU_P1_BYTES, actual)) goto source_fail;
	/* ext4 metadata changes during normal read-write use; bind rollback to the
	 * exact bytes seen after entering RAM instead of a stale package-time hash. */
	memcpy(journal.source_p1_sha256, actual, 32);
	if (hash_region(disk, GKDU_KERNEL_SLOT_OFFSET, GKDU_KERNEL_BYTES, actual) ||
	    memcmp(actual, journal.source_kernel_sha256, 32)) goto source_fail;
	status = gkdu_journal_read(recovery, &prior, 0);
	if (status < 0) goto journal_fail;
	if (!status) {
		if (prior.state != GKDU_STATE_BACKUP_WRITING ||
		    memcmp(prior.package_sha256, journal.package_sha256, 32) ||
		    memcmp(prior.source_p1_sha256, journal.source_p1_sha256, 32)) goto state_fail;
		journal.sequence = prior.sequence;
	}
	if (set_state(recovery, &journal, GKDU_STATE_BACKUP_WRITING)) goto journal_fail;
	if (copy_region(disk, GKDU_KERNEL_SLOT_OFFSET, recovery,
		GKDU_KERNEL_BACKUP_OFFSET, GKDU_KERNEL_BYTES,
		journal.backup_kernel_sha256) || fsync(recovery)) goto backup_fail;
	if (hash_region(recovery, GKDU_KERNEL_BACKUP_OFFSET, GKDU_KERNEL_BYTES,
		readback) || memcmp(readback, journal.source_kernel_sha256, 32) ||
		memcmp(journal.backup_kernel_sha256, readback, 32)) goto verify_fail;
	if (copy_region(p1, 0, recovery, GKDU_P1_BACKUP_OFFSET, GKDU_P1_BYTES,
		journal.backup_p1_gzip_sha256) || fsync(recovery) ||
		memcmp(journal.backup_p1_gzip_sha256, journal.source_p1_sha256, 32))
		goto backup_fail;
	journal.backup_p1_gzip_bytes = GKDU_P1_BYTES;
	if (hash_region(recovery, GKDU_P1_BACKUP_OFFSET, GKDU_P1_BYTES, readback) ||
	    memcmp(readback, journal.backup_p1_gzip_sha256, 32)) goto verify_fail;
	if (set_state(recovery, &journal, GKDU_STATE_BACKUP_READY)) goto journal_fail;
	reportf("GKDSU_BACKUP=PASS raw_bytes=%" PRIu64 " writes=%llu\n",
		(uint64_t)GKDU_P1_BYTES, write_count);
	close(recovery); close(p1); close(disk); return 0;
geometry_fail: status = fail("geometry"); goto done;
hash_fail: status = fail("hash-argument"); goto done;
source_fail: status = fail("source-identity"); goto done;
state_fail: status = fail("backup-state"); goto done;
journal_fail: status = fail("journal"); goto done;
backup_fail: status = fail("backup-write"); goto done;
verify_fail: status = fail("backup-readback");
done:
	if (recovery >= 0) close(recovery);
	if (p1 >= 0) close(p1);
	if (disk >= 0) close(disk);
	return status;
}

static int apply_update(int argc, char **argv)
{
	struct gkdu_journal journal;
	uint8_t hash[32], readback[32];
	uint64_t target_p1_bytes, target_kernel_bytes, p1_offset = 0, kernel_offset = 0;
	int recovery = -1, p1 = -1, disk = -1, target_p1 = -1, target_kernel = -1;
	int package_mode = argc == 10 && !strcmp(argv[1], "apply-package");
	int status;
	if (argc != 7 && !package_mode) return fail("apply-usage");
	recovery = open_rw(argv[2]); p1 = open_rw(argv[3]); disk = open_rw(argv[4]);
	target_p1 = open_ro(argv[5]);
	if (package_mode) {
		char *end;
		target_kernel = target_p1;
		errno = 0; p1_offset = strtoull(argv[6], &end, 10);
		if (errno || !*argv[6] || *end) goto geometry_fail;
		errno = 0; target_p1_bytes = strtoull(argv[7], &end, 10);
		if (errno || !*argv[7] || *end) goto geometry_fail;
		errno = 0; kernel_offset = strtoull(argv[8], &end, 10);
		if (errno || !*argv[8] || *end) goto geometry_fail;
		errno = 0; target_kernel_bytes = strtoull(argv[9], &end, 10);
		if (errno || !*argv[9] || *end) goto geometry_fail;
	} else {
		target_kernel = open_ro(argv[6]);
		if (fd_bytes(target_p1, &target_p1_bytes) ||
		    fd_bytes(target_kernel, &target_kernel_bytes)) goto geometry_fail;
	}
	if (recovery < 0 || p1 < 0 || disk < 0 || target_p1 < 0 || target_kernel < 0 ||
	    geometry(recovery, p1, disk) || target_p1_bytes != GKDU_P1_BYTES ||
	    target_kernel_bytes != GKDU_KERNEL_BYTES) goto geometry_fail;
	if (package_mode) {
		uint64_t package_bytes;
		if (fd_bytes(target_p1, &package_bytes) || p1_offset > package_bytes ||
		    target_p1_bytes > package_bytes - p1_offset ||
		    kernel_offset != p1_offset + target_p1_bytes ||
		    target_kernel_bytes > package_bytes - kernel_offset ||
		    kernel_offset + target_kernel_bytes != package_bytes) goto geometry_fail;
	}
	if (gkdu_journal_read(recovery, &journal, 0) ||
	    journal.state != GKDU_STATE_BACKUP_READY) goto state_fail;
	if (journal.backup_p1_gzip_bytes != GKDU_P1_BYTES ||
	    hash_region(recovery, GKDU_KERNEL_BACKUP_OFFSET, GKDU_KERNEL_BYTES, hash) ||
	    memcmp(hash, journal.backup_kernel_sha256, 32) ||
	    hash_region(recovery, GKDU_P1_BACKUP_OFFSET,
		journal.backup_p1_gzip_bytes, hash) ||
	    memcmp(hash, journal.backup_p1_gzip_sha256, 32)) goto backup_fail;
	if (hash_region(target_kernel, kernel_offset, GKDU_KERNEL_BYTES, hash) ||
	    memcmp(hash, journal.target_kernel_sha256, 32)) goto target_fail;
	if (set_state(recovery, &journal, GKDU_STATE_APPLYING_P1)) goto journal_fail;
	if (copy_region(target_p1, p1_offset, p1, 0, GKDU_P1_BYTES, hash) ||
	    fsync(p1) || memcmp(hash, journal.target_p1_sha256, 32) ||
	    hash_region(p1, 0, GKDU_P1_BYTES, readback) ||
	    memcmp(readback, journal.target_p1_sha256, 32)) goto p1_fail;
	if (set_state(recovery, &journal, GKDU_STATE_APPLYING_KERNEL)) goto journal_fail;
	if (copy_region(target_kernel, kernel_offset, disk, GKDU_KERNEL_SLOT_OFFSET,
		GKDU_KERNEL_BYTES, hash) || fsync(disk) ||
		memcmp(hash, journal.target_kernel_sha256, 32) ||
		hash_region(disk, GKDU_KERNEL_SLOT_OFFSET, GKDU_KERNEL_BYTES, readback) ||
		memcmp(readback, journal.target_kernel_sha256, 32)) goto kernel_fail;
	if (set_state(recovery, &journal, GKDU_STATE_TRIAL_PENDING)) goto journal_fail;
	reportf("GKDSU_APPLY=PASS state=trial-pending writes=%llu\n", write_count);
	status = 0; goto done;
geometry_fail: status = fail("geometry"); goto done;
state_fail: status = fail("apply-state"); goto done;
target_fail: status = fail("target-identity"); goto done;
backup_fail: status = fail("backup-identity"); goto done;
journal_fail: status = fail("journal"); goto done;
p1_fail: status = fail("p1-apply"); goto done;
kernel_fail: status = fail("kernel-apply");
done:
	if (recovery >= 0) close(recovery);
	if (p1 >= 0) close(p1);
	if (disk >= 0) close(disk);
	if (target_p1 >= 0) close(target_p1);
	if (target_kernel >= 0 && target_kernel != target_p1) close(target_kernel);
	return status;
}

static int restore(int argc, char **argv)
{
	struct gkdu_journal journal;
	uint8_t hash[32], readback[32];
	int recovery = -1, p1 = -1, disk = -1, status;
	if (argc != 5) return fail("restore-usage");
	recovery = open_rw(argv[2]); p1 = open_rw(argv[3]); disk = open_rw(argv[4]);
	if (recovery < 0 || p1 < 0 || disk < 0 || geometry(recovery, p1, disk))
		goto geometry_fail;
	if (gkdu_journal_read(recovery, &journal, 0)) goto state_fail;
	if (journal.state == GKDU_STATE_BACKUP_WRITING ||
	    journal.state == GKDU_STATE_BACKUP_READY) {
		reports("GKDSU_RESTORE=SAFE_ABORT system_untouched=1"); status = 0; goto done;
	}
	if (journal.state == GKDU_STATE_RESTORED) {
		if (hash_region(p1, 0, GKDU_P1_BYTES, hash) ||
		    memcmp(hash, journal.source_p1_sha256, 32) ||
		    hash_region(disk, GKDU_KERNEL_SLOT_OFFSET, GKDU_KERNEL_BYTES, hash) ||
		    memcmp(hash, journal.source_kernel_sha256, 32)) goto verify_fail;
		reports("GKDSU_RESTORE=PASS already-restored=1"); status = 0; goto done;
	}
	if (journal.state == GKDU_STATE_TRIAL_GOOD) goto state_fail;
	if (hash_region(recovery, GKDU_KERNEL_BACKUP_OFFSET, GKDU_KERNEL_BYTES, hash) ||
	    memcmp(hash, journal.backup_kernel_sha256, 32) ||
	    memcmp(hash, journal.source_kernel_sha256, 32) ||
	    journal.backup_p1_gzip_bytes != GKDU_P1_BYTES ||
	    hash_region(recovery, GKDU_P1_BACKUP_OFFSET,
		journal.backup_p1_gzip_bytes, hash) ||
	    memcmp(hash, journal.backup_p1_gzip_sha256, 32)) goto backup_fail;
	if (journal.state != GKDU_STATE_ROLLING_BACK &&
	    set_state(recovery, &journal, GKDU_STATE_ROLLING_BACK)) goto journal_fail;
	/* P1 is restored first. The recovery-aware kernel stays installed until the
	 * old root is exact, then the old kernel is the rollback commit marker. */
	if (copy_region(recovery, GKDU_P1_BACKUP_OFFSET, p1, 0, GKDU_P1_BYTES, hash) ||
	    fsync(p1) || memcmp(hash, journal.source_p1_sha256, 32) ||
	    hash_region(p1, 0, GKDU_P1_BYTES, readback) ||
	    memcmp(readback, journal.source_p1_sha256, 32)) goto restore_fail;
	if (copy_region(recovery, GKDU_KERNEL_BACKUP_OFFSET, disk,
		GKDU_KERNEL_SLOT_OFFSET, GKDU_KERNEL_BYTES, hash) || fsync(disk) ||
	    memcmp(hash, journal.source_kernel_sha256, 32) ||
	    hash_region(disk, GKDU_KERNEL_SLOT_OFFSET, GKDU_KERNEL_BYTES, readback) ||
	    memcmp(readback, journal.source_kernel_sha256, 32)) goto restore_fail;
	if (set_state(recovery, &journal, GKDU_STATE_RESTORED)) goto journal_fail;
	reportf("GKDSU_RESTORE=PASS state=restored writes=%llu\n", write_count);
	status = 0; goto done;
geometry_fail: status = fail("geometry"); goto done;
state_fail: status = fail("restore-state"); goto done;
backup_fail: status = fail("backup-identity"); goto done;
journal_fail: status = fail("journal"); goto done;
restore_fail: status = fail("restore-write"); goto done;
verify_fail: status = fail("restored-readback");
done:
	if (recovery >= 0) close(recovery);
	if (p1 >= 0) close(p1);
	if (disk >= 0) close(disk);
	return status;
}

static int transition_trial(int argc, char **argv, uint32_t required,
		uint32_t target, const char *label)
{
	struct gkdu_journal journal;
	uint8_t hash[32];
	int recovery = -1, p1 = -1, disk = -1, status;
	if (argc != 5) return fail("trial-usage");
	recovery = open_rw(argv[2]); p1 = open_ro(argv[3]); disk = open_ro(argv[4]);
	if (recovery < 0 || p1 < 0 || disk < 0 || geometry(recovery, p1, disk)) {
		status = fail("geometry"); goto done;
	}
	if (gkdu_journal_read(recovery, &journal, 0) || journal.state != required) {
		status = fail("trial-state"); goto done;
	}
	if (hash_region(p1, 0, GKDU_P1_BYTES, hash) ||
	    memcmp(hash, journal.target_p1_sha256, 32) ||
	    hash_region(disk, GKDU_KERNEL_SLOT_OFFSET, GKDU_KERNEL_BYTES, hash) ||
	    memcmp(hash, journal.target_kernel_sha256, 32)) {
		status = fail("trial-identity"); goto done;
	}
	if (set_state(recovery, &journal, target)) {
		status = fail("journal"); goto done;
	}
	reportf("GKDSU_TRIAL=PASS action=%s state=%u writes=%llu\n",
		label, target, write_count);
	status = 0;
done:
	if (recovery >= 0) close(recovery);
	if (p1 >= 0) close(p1);
	if (disk >= 0) close(disk);
	return status;
}

static int format_swap(int argc, char **argv)
{
	uint64_t bytes;
	int fd, result;
	if (argc != 3) return fail("format-swap-usage");
	fd = open_rw(argv[2]);
	if (fd < 0 || fd_bytes(fd, &bytes) || bytes != GKDU_RECOVERY_BYTES) {
		if (fd >= 0) close(fd);
		return fail("format-swap-geometry");
	}
	result = gkdu_recovery_format_swap(fd) || close(fd);
	if (result) return fail("format-swap-write");
	reports("GKDSU_FORMAT_SWAP=PASS journal_copies=2");
	return 0;
}

#ifndef GKDU_MINIMAL_RUNTIME
static int inspect_journal(int argc, char **argv)
{
	struct gkdu_journal journal;
	int fd;
	if (argc != 3) return fail("inspect-usage");
	fd = open_ro(argv[2]); if (fd < 0) return fail("inspect-open");
	if (gkdu_journal_read(fd, &journal, 0)) { close(fd); return fail("inspect-state"); }
	printf("GKDSU_INSPECT=PASS sequence=%" PRIu64 " state=%u package=",
		journal.sequence, journal.state);
	print_hash(journal.package_sha256); putchar('\n'); close(fd); return 0;
}
#endif

int main(int argc, char **argv)
{
	if (argc < 2) return fail("usage");
	if (!strcmp(argv[1], "backup")) return backup(argc, argv);
	if (!strcmp(argv[1], "apply")) return apply_update(argc, argv);
	if (!strcmp(argv[1], "apply-package")) return apply_update(argc, argv);
	if (!strcmp(argv[1], "restore")) return restore(argc, argv);
	if (!strcmp(argv[1], "begin-trial"))
		return transition_trial(argc, argv, GKDU_STATE_TRIAL_PENDING,
			GKDU_STATE_TRIAL_BOOTING, "begin");
	if (!strcmp(argv[1], "trial-good"))
		return transition_trial(argc, argv, GKDU_STATE_TRIAL_BOOTING,
			GKDU_STATE_TRIAL_GOOD, "good");
	if (!strcmp(argv[1], "format-swap")) return format_swap(argc, argv);
#ifndef GKDU_MINIMAL_RUNTIME
	if (!strcmp(argv[1], "inspect")) return inspect_journal(argc, argv);
#endif
	return fail("command");
}
