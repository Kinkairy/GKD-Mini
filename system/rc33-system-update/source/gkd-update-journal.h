#ifndef GKD_UPDATE_JOURNAL_H
#define GKD_UPDATE_JOURNAL_H

#include <stddef.h>
#include <stdint.h>

#ifndef GKDU_JOURNAL_BYTES
#define GKDU_JOURNAL_BYTES 4096u
#endif
#define GKDU_JOURNAL_COPIES 2u
#ifndef GKDU_KERNEL_BACKUP_OFFSET
#define GKDU_KERNEL_BACKUP_OFFSET 1048576u
#endif
#ifndef GKDU_KERNEL_BACKUP_BYTES
#define GKDU_KERNEL_BACKUP_BYTES 6291456u
#endif
#ifndef GKDU_P1_BACKUP_OFFSET
#define GKDU_P1_BACKUP_OFFSET 8388608u
#endif

enum gkdu_state {
	GKDU_STATE_EMPTY = 0,
	GKDU_STATE_BACKUP_WRITING = 1,
	GKDU_STATE_BACKUP_READY = 2,
	GKDU_STATE_APPLYING_P1 = 3,
	GKDU_STATE_APPLYING_KERNEL = 4,
	GKDU_STATE_TRIAL_PENDING = 5,
	GKDU_STATE_ROLLING_BACK = 6,
	GKDU_STATE_TRIAL_GOOD = 7,
	GKDU_STATE_RESTORED = 8,
	GKDU_STATE_TRIAL_BOOTING = 9
};

struct gkdu_journal {
	uint64_t sequence;
	uint32_t state;
	uint32_t last_error;
	uint64_t backup_p1_gzip_bytes;
	uint8_t package_sha256[32];
	uint8_t source_p1_sha256[32];
	uint8_t source_kernel_sha256[32];
	uint8_t target_p1_sha256[32];
	uint8_t target_kernel_sha256[32];
	uint8_t backup_p1_gzip_sha256[32];
	uint8_t backup_kernel_sha256[32];
};

int gkdu_journal_decode(const uint8_t raw[GKDU_JOURNAL_BYTES],
			struct gkdu_journal *out);
int gkdu_journal_encode(const struct gkdu_journal *journal,
			uint8_t raw[GKDU_JOURNAL_BYTES]);
int gkdu_journal_read(int fd, struct gkdu_journal *out, unsigned *copy);
int gkdu_journal_write_next(int fd, const struct gkdu_journal *journal,
			unsigned *written_copy);
int gkdu_recovery_format_swap(int fd);

#endif
