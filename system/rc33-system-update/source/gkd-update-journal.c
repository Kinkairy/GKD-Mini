#define _FILE_OFFSET_BITS 64
#define _XOPEN_SOURCE 700
#include "gkd-update-journal.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#define GKDU_MAGIC "GKDJNL1\0"
#define GKDU_VERSION 1u
#define CRC_OFFSET (GKDU_JOURNAL_BYTES - 4u)

static void put32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
	p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}

static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put64(uint8_t *p, uint64_t value)
{
	put32(p, (uint32_t)value); put32(p + 4, (uint32_t)(value >> 32));
}

static uint64_t get64(const uint8_t *p)
{
	return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32);
}

static uint32_t crc32_bytes(const uint8_t *data, size_t length)
{
	uint32_t crc = 0xffffffffu;
	size_t i;
	for (i = 0; i < length; ++i) {
		unsigned bit;
		crc ^= data[i];
		for (bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

static int valid_state(uint32_t state)
{
	return state >= GKDU_STATE_BACKUP_WRITING && state <= GKDU_STATE_TRIAL_BOOTING;
}

__attribute__((weak)) int gkdu_before_pwrite(void)
{
	return 0;
}

int gkdu_journal_encode(const struct gkdu_journal *j,
			uint8_t raw[GKDU_JOURNAL_BYTES])
{
	size_t at = 32;
	const uint8_t *hashes[7];
	unsigned i;
	if (!j || !raw || !j->sequence || !valid_state(j->state)) return -1;
	hashes[0] = j->package_sha256; hashes[1] = j->source_p1_sha256;
	hashes[2] = j->source_kernel_sha256; hashes[3] = j->target_p1_sha256;
	hashes[4] = j->target_kernel_sha256; hashes[5] = j->backup_p1_gzip_sha256;
	hashes[6] = j->backup_kernel_sha256;
	memset(raw, 0, GKDU_JOURNAL_BYTES);
	memcpy(raw, GKDU_MAGIC, 8); put32(raw + 8, GKDU_VERSION);
	put32(raw + 12, GKDU_JOURNAL_BYTES); put64(raw + 16, j->sequence);
	put32(raw + 24, j->state); put32(raw + 28, j->last_error);
	put64(raw + at, j->backup_p1_gzip_bytes); at += 8;
	for (i = 0; i < sizeof(hashes) / sizeof(hashes[0]); ++i) {
		memcpy(raw + at, hashes[i], 32); at += 32;
	}
	put32(raw + CRC_OFFSET, crc32_bytes(raw, CRC_OFFSET));
	return 0;
}

int gkdu_journal_decode(const uint8_t raw[GKDU_JOURNAL_BYTES],
			struct gkdu_journal *j)
{
	size_t at = 32;
	uint8_t *hashes[7];
	unsigned i;
	if (!raw || !j) return -1;
	if (memcmp(raw, GKDU_MAGIC, 8) || get32(raw + 8) != GKDU_VERSION ||
	    get32(raw + 12) != GKDU_JOURNAL_BYTES ||
	    get32(raw + CRC_OFFSET) != crc32_bytes(raw, CRC_OFFSET)) return -1;
	memset(j, 0, sizeof(*j));
	hashes[0] = j->package_sha256; hashes[1] = j->source_p1_sha256;
	hashes[2] = j->source_kernel_sha256; hashes[3] = j->target_p1_sha256;
	hashes[4] = j->target_kernel_sha256; hashes[5] = j->backup_p1_gzip_sha256;
	hashes[6] = j->backup_kernel_sha256;
	j->sequence = get64(raw + 16); j->state = get32(raw + 24);
	j->last_error = get32(raw + 28);
	j->backup_p1_gzip_bytes = get64(raw + at); at += 8;
	if (!j->sequence || !valid_state(j->state)) return -1;
	for (i = 0; i < sizeof(hashes) / sizeof(hashes[0]); ++i) {
		memcpy(hashes[i], raw + at, 32); at += 32;
	}
	return 0;
}

static int exact_io(int fd, uint8_t *raw, unsigned copy, int writing)
{
	size_t done = 0;
	off_t base = (off_t)copy * GKDU_JOURNAL_BYTES;
	while (done < GKDU_JOURNAL_BYTES) {
		ssize_t count;
		if (writing && gkdu_before_pwrite()) return -1;
		count = writing ? pwrite(fd, raw + done, GKDU_JOURNAL_BYTES - done,
			base + (off_t)done) : pread(fd, raw + done,
			GKDU_JOURNAL_BYTES - done, base + (off_t)done);
		if (count < 0 && errno == EINTR) continue;
		if (count <= 0) return -1;
		done += (size_t)count;
	}
	return 0;
}

int gkdu_journal_read(int fd, struct gkdu_journal *out, unsigned *copy)
{
	struct gkdu_journal candidate, best;
	uint8_t raw[GKDU_JOURNAL_BYTES];
	unsigned i, best_copy = 0;
	int found = 0;
	for (i = 0; i < GKDU_JOURNAL_COPIES; ++i) {
		if (exact_io(fd, raw, i, 0) || gkdu_journal_decode(raw, &candidate)) continue;
		if (!found || candidate.sequence > best.sequence) {
			best = candidate; best_copy = i; found = 1;
		}
	}
	if (!found) return 1;
	*out = best; if (copy) *copy = best_copy;
	return 0;
}

int gkdu_journal_write_next(int fd, const struct gkdu_journal *journal,
			unsigned *written_copy)
{
	struct gkdu_journal current, check;
	uint8_t raw[GKDU_JOURNAL_BYTES], readback[GKDU_JOURNAL_BYTES];
	unsigned active = 1, target;
	int status = gkdu_journal_read(fd, &current, &active);
	if (status < 0 || (status == 0 && journal->sequence <= current.sequence)) return -1;
	target = status == 0 ? active ^ 1u : 0u;
	if (gkdu_journal_encode(journal, raw) || exact_io(fd, raw, target, 1) ||
	    fsync(fd) || exact_io(fd, readback, target, 0) ||
	    memcmp(raw, readback, GKDU_JOURNAL_BYTES) ||
	    gkdu_journal_decode(readback, &check) || check.sequence != journal->sequence)
		return -1;
	if (written_copy) *written_copy = target;
	return 0;
}

int gkdu_recovery_format_swap(int fd)
{
	uint8_t zero[GKDU_JOURNAL_BYTES] = {0};
	uint8_t page[GKDU_JOURNAL_BYTES] = {0};
	uint8_t readback[GKDU_JOURNAL_BYTES];
	uint32_t last = (uint32_t)(1073741824ULL / sizeof(page) - 1ULL);
	unsigned copy;
	for (copy = 0; copy < GKDU_JOURNAL_COPIES; ++copy) {
		if (exact_io(fd, zero, copy, 1)) return -1;
	}
	if (fsync(fd)) return -1;
	page[1024] = 1;
	put32(page + 1028, last);
	memcpy(page + sizeof(page) - 10, "SWAPSPACE2", 10);
	if (pwrite(fd, page, sizeof(page), 0) != (ssize_t)sizeof(page) ||
	    fsync(fd) || pread(fd, readback, sizeof(readback), 0) != (ssize_t)sizeof(readback) ||
	    memcmp(page, readback, sizeof(page)) ||
	    pread(fd, readback, sizeof(readback), GKDU_JOURNAL_BYTES) != (ssize_t)sizeof(readback) ||
	    memcmp(zero, readback, sizeof(zero))) return -1;
	return 0;
}
