#define _FILE_OFFSET_BITS 64
#define _XOPEN_SOURCE 700
#include "gkd-update-request.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

#define GKDU_REQUEST_MAGIC "GKDRQ1\0\0"
#define GKDU_REQUEST_VERSION 1u
#define GKDU_REQUEST_CRC_OFFSET (GKDU_REQUEST_BYTES - 4u)

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
	unsigned i;
	for (i = 0; i < 8; ++i) p[i] = (uint8_t)(value >> (i * 8));
}

static uint64_t get64(const uint8_t *p)
{
	uint64_t value = 0;
	int i;
	for (i = 7; i >= 0; --i) value = (value << 8) | p[i];
	return value;
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

static int zero(const uint8_t raw[GKDU_REQUEST_BYTES])
{
	unsigned i;
	for (i = 0; i < GKDU_REQUEST_BYTES; ++i)
		if (raw[i]) return 0;
	return 1;
}

static int request_equal(const struct gkdu_request *a,
		const struct gkdu_request *b)
{
	return !memcmp(a, b, sizeof(*a));
}

__attribute__((weak)) int gkdu_request_before_pwrite(void)
{
	return 0;
}

static int exact_io(int fd, uint8_t raw[GKDU_REQUEST_BYTES], int writing)
{
	size_t done = 0;
	while (done < GKDU_REQUEST_BYTES) {
		ssize_t count;
		if (writing && gkdu_request_before_pwrite()) return -1;
		count = writing ? pwrite(fd, raw + done, GKDU_REQUEST_BYTES - done,
			(off_t)(GKDU_REQUEST_OFFSET + done)) :
			pread(fd, raw + done, GKDU_REQUEST_BYTES - done,
			(off_t)(GKDU_REQUEST_OFFSET + done));
		if (count < 0 && errno == EINTR) continue;
		if (count <= 0) return -1;
		done += (size_t)count;
	}
	return 0;
}

int gkdu_request_encode(const struct gkdu_request *request,
		uint8_t raw[GKDU_REQUEST_BYTES])
{
	const uint8_t *hashes[7];
	unsigned i;
	size_t at = 16;
	if (!request || !raw) return -1;
	hashes[0] = request->package_sha256;
	hashes[1] = request->source_runtime_id;
	hashes[2] = request->target_runtime_id;
	hashes[3] = request->source_p1_sha256;
	hashes[4] = request->source_kernel_sha256;
	hashes[5] = request->target_p1_sha256;
	hashes[6] = request->target_kernel_sha256;
	memset(raw, 0, GKDU_REQUEST_BYTES);
	memcpy(raw, GKDU_REQUEST_MAGIC, 8);
	put32(raw + 8, GKDU_REQUEST_VERSION);
	put32(raw + 12, GKDU_REQUEST_BYTES);
	for (i = 0; i < sizeof(hashes) / sizeof(hashes[0]); ++i) {
		memcpy(raw + at, hashes[i], 32); at += 32;
	}
	put64(raw + at, request->package_bytes); at += 8;
	put64(raw + at, request->p1_payload_offset); at += 8;
	put64(raw + at, request->p1_payload_bytes); at += 8;
	put64(raw + at, request->kernel_payload_offset); at += 8;
	put64(raw + at, request->kernel_payload_bytes);
	put32(raw + GKDU_REQUEST_CRC_OFFSET,
		crc32_bytes(raw, GKDU_REQUEST_CRC_OFFSET));
	return 0;
}

int gkdu_request_decode(const uint8_t raw[GKDU_REQUEST_BYTES],
		struct gkdu_request *request)
{
	uint8_t *hashes[7];
	unsigned i;
	size_t at = 16;
	if (!raw || !request || memcmp(raw, GKDU_REQUEST_MAGIC, 8) ||
	    get32(raw + 8) != GKDU_REQUEST_VERSION ||
	    get32(raw + 12) != GKDU_REQUEST_BYTES ||
	    get32(raw + GKDU_REQUEST_CRC_OFFSET) !=
		crc32_bytes(raw, GKDU_REQUEST_CRC_OFFSET)) return -1;
	memset(request, 0, sizeof(*request));
	hashes[0] = request->package_sha256;
	hashes[1] = request->source_runtime_id;
	hashes[2] = request->target_runtime_id;
	hashes[3] = request->source_p1_sha256;
	hashes[4] = request->source_kernel_sha256;
	hashes[5] = request->target_p1_sha256;
	hashes[6] = request->target_kernel_sha256;
	for (i = 0; i < sizeof(hashes) / sizeof(hashes[0]); ++i) {
		memcpy(hashes[i], raw + at, 32); at += 32;
	}
	request->package_bytes = get64(raw + at); at += 8;
	request->p1_payload_offset = get64(raw + at); at += 8;
	request->p1_payload_bytes = get64(raw + at); at += 8;
	request->kernel_payload_offset = get64(raw + at); at += 8;
	request->kernel_payload_bytes = get64(raw + at);
	return 0;
}

int gkdu_request_read(int fd, struct gkdu_request *request)
{
	uint8_t raw[GKDU_REQUEST_BYTES];
	if (exact_io(fd, raw, 0)) return -1;
	if (zero(raw)) return 1;
	return gkdu_request_decode(raw, request);
}

int gkdu_request_write(int fd, const struct gkdu_request *request)
{
	struct gkdu_request current;
	uint8_t raw[GKDU_REQUEST_BYTES], readback[GKDU_REQUEST_BYTES];
	int status = gkdu_request_read(fd, &current);
	if (status < 0 || (status == 0 && !request_equal(&current, request)) ||
	    gkdu_request_encode(request, raw) || exact_io(fd, raw, 1) || fsync(fd) ||
	    exact_io(fd, readback, 0) || memcmp(raw, readback, sizeof(raw))) return -1;
	return 0;
}

int gkdu_request_clear(int fd, const struct gkdu_request *expected)
{
	struct gkdu_request current;
	uint8_t raw[GKDU_REQUEST_BYTES], readback[GKDU_REQUEST_BYTES];
	if (gkdu_request_read(fd, &current) ||
	    !request_equal(&current, expected)) return -1;
	memset(raw, 0, sizeof(raw));
	if (exact_io(fd, raw, 1) || fsync(fd) || exact_io(fd, readback, 0) ||
	    !zero(readback)) return -1;
	return 0;
}
