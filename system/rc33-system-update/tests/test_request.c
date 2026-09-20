#define _FILE_OFFSET_BITS 64
#define _XOPEN_SOURCE 700
#include "gkd-update-request.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define IMAGE_BYTES 31457280

static int fail(const char *message)
{
	fprintf(stderr, "GKDSU_REQUEST_TEST=FAIL reason=%s\n", message);
	return 1;
}

static void fill(struct gkdu_request *request, unsigned seed)
{
	unsigned i;
	uint8_t *bytes = (uint8_t *)request;
	for (i = 0; i < sizeof(*request); ++i)
		bytes[i] = (uint8_t)(seed + i * 17u);
}

int main(int argc, char **argv)
{
	struct gkdu_request first, other, actual;
	uint8_t byte;
	int fd;
	if (argc != 2) return fail("usage");
	fd = open(argv[1], O_CREAT | O_TRUNC | O_RDWR, 0600);
	if (fd < 0 || ftruncate(fd, IMAGE_BYTES)) return fail("image");
	fill(&first, 3); fill(&other, 9);
	if (gkdu_request_read(fd, &actual) != 1) return fail("empty");
	if (gkdu_request_write(fd, &first) ||
	    gkdu_request_read(fd, &actual) || memcmp(&actual, &first, sizeof(first)))
		return fail("roundtrip");
	if (gkdu_request_write(fd, &other) == 0) return fail("replace-accepted");
	if (pread(fd, &byte, 1, (off_t)GKDU_REQUEST_OFFSET + 128) != 1) return fail("read");
	byte ^= 1;
	if (pwrite(fd, &byte, 1, (off_t)GKDU_REQUEST_OFFSET + 128) != 1 || fsync(fd))
		return fail("corrupt");
	if (gkdu_request_read(fd, &actual) != -1) return fail("crc-accepted");
	if (ftruncate(fd, 0) || ftruncate(fd, IMAGE_BYTES) ||
	    gkdu_request_write(fd, &first)) return fail("rewrite");
	if (gkdu_request_clear(fd, &other) == 0) return fail("wrong-clear");
	if (gkdu_request_clear(fd, &first) || gkdu_request_read(fd, &actual) != 1)
		return fail("clear");
	if (close(fd)) return fail("close");
	puts("GKDSU_REQUEST_TEST=PASS empty=1 roundtrip=1 unknown_rejected=1 crc_rejected=1 exact_clear=1");
	return 0;
}
