#ifndef GKD_UPDATE_SHA256_H
#define GKD_UPDATE_SHA256_H

#include <stddef.h>
#include <stdint.h>

struct gkdu_sha256 {
	uint32_t h[8];
	uint64_t bytes;
	uint8_t block[64];
	unsigned used;
};

void gkdu_sha256_init(struct gkdu_sha256 *context);
void gkdu_sha256_update(struct gkdu_sha256 *context, const void *data, size_t bytes);
void gkdu_sha256_final(struct gkdu_sha256 *context, uint8_t output[32]);

#endif
