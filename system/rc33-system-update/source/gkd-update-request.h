#ifndef GKD_UPDATE_REQUEST_H
#define GKD_UPDATE_REQUEST_H

#include <stdint.h>

#define GKDU_REQUEST_BYTES 4096u
#define GKDU_REQUEST_OFFSET 20967424ULL

struct gkdu_request {
	uint8_t package_sha256[32];
	uint8_t source_runtime_id[32];
	uint8_t target_runtime_id[32];
	uint8_t source_p1_sha256[32];
	uint8_t source_kernel_sha256[32];
	uint8_t target_p1_sha256[32];
	uint8_t target_kernel_sha256[32];
	uint64_t package_bytes;
	uint64_t p1_payload_offset;
	uint64_t p1_payload_bytes;
	uint64_t kernel_payload_offset;
	uint64_t kernel_payload_bytes;
};

int gkdu_request_encode(const struct gkdu_request *request,
		uint8_t raw[GKDU_REQUEST_BYTES]);
int gkdu_request_decode(const uint8_t raw[GKDU_REQUEST_BYTES],
		struct gkdu_request *request);
int gkdu_request_read(int fd, struct gkdu_request *request);
int gkdu_request_write(int fd, const struct gkdu_request *request);
int gkdu_request_clear(int fd, const struct gkdu_request *expected);

#endif
