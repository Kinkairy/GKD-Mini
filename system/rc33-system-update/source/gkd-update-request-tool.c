#define _FILE_OFFSET_BITS 64
#define _XOPEN_SOURCE 700
#include "gkd-update-request.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void print_hash(const unsigned char value[32])
{
	unsigned i;
	for (i = 0; i < 32; ++i) printf("%02x", value[i]);
}

int main(int argc, char **argv)
{
	struct gkdu_request request;
	int fd, status;
	if (argc != 3 || (strcmp(argv[1], "show") && strcmp(argv[1], "clear-current")))
		return 2;
	fd = open(argv[2], !strcmp(argv[1], "show") ? O_RDONLY : O_RDWR | O_SYNC);
	if (fd < 0) return 2;
	status = gkdu_request_read(fd, &request);
	if (status) { close(fd); return status > 0 ? 3 : 2; }
	if (!strcmp(argv[1], "clear-current")) {
		status = gkdu_request_clear(fd, &request);
		if (!status) puts("GKDSU_REQUEST_TOOL=PASS action=clear");
	} else {
		printf("package_sha256="); print_hash(request.package_sha256);
		printf("\nsource_runtime_id="); print_hash(request.source_runtime_id);
		printf("\ntarget_runtime_id="); print_hash(request.target_runtime_id);
		printf("\npackage_bytes=%llu\np1_payload_offset=%llu\np1_payload_bytes=%llu\n"
		       "kernel_payload_offset=%llu\nkernel_payload_bytes=%llu\n",
			(unsigned long long)request.package_bytes,
			(unsigned long long)request.p1_payload_offset,
			(unsigned long long)request.p1_payload_bytes,
			(unsigned long long)request.kernel_payload_offset,
			(unsigned long long)request.kernel_payload_bytes);
	}
	if (close(fd)) status = -1;
	return status ? 2 : 0;
}
