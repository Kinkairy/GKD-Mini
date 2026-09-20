#define _FILE_OFFSET_BITS 64
#define _XOPEN_SOURCE 700
#include "gkd-update-journal.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int fail(const char *why)
{
	fprintf(stderr, "GKDSU_JOURNAL_TEST=FAIL reason=%s\n", why); return 1;
}

int main(int argc, char **argv)
{
	struct gkdu_journal j, got;
	uint8_t damage[128];
	unsigned copy;
	int fd;
	if (argc != 2) return fail("usage");
	fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0600);
	if (fd < 0 || ftruncate(fd, 16384)) return fail("fixture");
	if (gkdu_journal_read(fd, &got, &copy) != 1) return fail("empty");
	memset(&j, 0, sizeof(j)); j.sequence = 1;
	j.state = GKDU_STATE_BACKUP_WRITING; memset(j.package_sha256, 0x11, 32);
	if (gkdu_journal_write_next(fd, &j, &copy) || copy != 0) return fail("first");
	j.sequence = 2; j.state = GKDU_STATE_BACKUP_READY;
	if (gkdu_journal_write_next(fd, &j, &copy) || copy != 1) return fail("second");
	if (gkdu_journal_read(fd, &got, &copy) || got.sequence != 2 || copy != 1)
		return fail("latest");
	memset(damage, 0xa5, sizeof(damage));
	if (pwrite(fd, damage, sizeof(damage), GKDU_JOURNAL_BYTES + 17) != sizeof(damage))
		return fail("damage-new");
	if (gkdu_journal_read(fd, &got, &copy) || got.sequence != 1 || copy != 0)
		return fail("fallback");
	if (pwrite(fd, damage, sizeof(damage), 23) != sizeof(damage)) return fail("damage-old");
	if (gkdu_journal_read(fd, &got, &copy) != 1) return fail("reject-both");
	close(fd);
	puts("GKDSU_JOURNAL_TEST=PASS latest=1 torn_fallback=1 both_invalid_rejected=1");
	return 0;
}
