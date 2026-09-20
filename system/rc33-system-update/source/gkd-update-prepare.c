#define _FILE_OFFSET_BITS 64
#define _XOPEN_SOURCE 700
#include "gkd-update-journal.h"
#include "gkd-update-request.h"

#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MANIFEST_MAX (1u << 20)
#define SIGNATURE_BYTES 256u
#define P1_RAW_BYTES 805306880ULL
#define KERNEL_RAW_BYTES 6291456ULL
#if GKDU_RECOVERY_HOST
#define SOURCE_KERNEL_OFFSET 9437184ULL
#endif

static int fail(const char *why)
{
	fprintf(stderr, "GKDSU_PREPARE=BLOCKED reason=%s\n", why);
	return 2;
}

static uint32_t get32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int exact_read(int fd, void *data, size_t bytes)
{
	unsigned char *p = data;
	while (bytes) {
		ssize_t got = read(fd, p, bytes);
		if (got < 0 && errno == EINTR) continue;
		if (got <= 0) return -1;
		p += got; bytes -= (size_t)got;
	}
	return 0;
}

static int sha_region(int fd, uint64_t offset, uint64_t bytes,
		unsigned char out[32])
{
	SHA256_CTX ctx;
	unsigned char buffer[1 << 16];
	if (lseek(fd, (off_t)offset, SEEK_SET) != (off_t)offset) return -1;
	SHA256_Init(&ctx);
	while (bytes) {
		size_t wanted = bytes > sizeof(buffer) ? sizeof(buffer) : (size_t)bytes;
		ssize_t got = read(fd, buffer, wanted);
		if (got < 0 && errno == EINTR) continue;
		if (got <= 0) return -1;
		SHA256_Update(&ctx, buffer, (size_t)got); bytes -= (uint64_t)got;
	}
	SHA256_Final(out, &ctx);
	return 0;
}

static int hex32(const char *text, unsigned char out[32])
{
	unsigned i;
	for (i = 0; i < 32; ++i) {
		unsigned hi, lo;
		char a = text[i * 2], b = text[i * 2 + 1];
		hi = a >= '0' && a <= '9' ? (unsigned)(a - '0') :
		     a >= 'a' && a <= 'f' ? (unsigned)(a - 'a' + 10) : 99;
		lo = b >= '0' && b <= '9' ? (unsigned)(b - '0') :
		     b >= 'a' && b <= 'f' ? (unsigned)(b - 'a' + 10) : 99;
		if (hi > 15 || lo > 15) return -1;
		out[i] = (unsigned char)((hi << 4) | lo);
	}
	return text[64] ? -1 : 0;
}

static const char *object(const char *json, const char *key)
{
	char pattern[96];
	if (snprintf(pattern, sizeof(pattern), "\"%s\":{", key) <= 0) return 0;
	return strstr(json, pattern);
}

static int field_string(const char *start, const char *limit, const char *key,
		char *out, size_t bytes)
{
	char pattern[96];
	const char *p, *end;
	int count = snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
	if (count <= 0 || (size_t)count >= sizeof(pattern)) return -1;
	p = strstr(start, pattern);
	if (!p || (limit && p >= limit)) return -1;
	p += count; end = strchr(p, '"');
	if (!end || (limit && end >= limit) || (size_t)(end - p) + 1 > bytes) return -1;
	memcpy(out, p, (size_t)(end - p)); out[end - p] = 0;
	return 0;
}

static int decimal64(const char *text, uint64_t *value)
{
	uint64_t n = 0;
	const char *p = text;
	if (!*p || (*p == '0' && p[1])) return -1;
	for (; *p; ++p) {
		uint64_t next;
		if (*p < '0' || *p > '9') return -1;
		next = n * 10 + (unsigned)(*p - '0');
		if (next < n) return -1;
		n = next;
	}
	*value = n; return 0;
}

static int field_u64(const char *start, const char *limit, const char *key,
		uint64_t *value)
{
	char text[32];
	return field_string(start, limit, key, text, sizeof(text)) || decimal64(text, value);
}

static int field_hash(const char *start, const char *limit, const char *key,
		unsigned char out[32])
{
	char text[65];
	return field_string(start, limit, key, text, sizeof(text)) || hex32(text, out);
}

static int signature_ok(const char *key_path, const unsigned char *manifest,
		size_t manifest_bytes, const unsigned char signature[SIGNATURE_BYTES])
{
	FILE *file = fopen(key_path, "rb");
	EVP_PKEY *key;
	EVP_MD_CTX *ctx;
	int ok = 0;
	if (!file) return 0;
	key = PEM_read_PUBKEY(file, 0, 0, 0); fclose(file);
	if (!key) return 0;
	ctx = EVP_MD_CTX_create();
	if (ctx && EVP_VerifyInit(ctx, EVP_sha256()) == 1 &&
	    EVP_VerifyUpdate(ctx, manifest, manifest_bytes) == 1 &&
	    EVP_VerifyFinal(ctx, signature, SIGNATURE_BYTES, key) == 1) ok = 1;
	if (ctx) EVP_MD_CTX_destroy(ctx);
	EVP_PKEY_free(key);
	return ok;
}

static int parse_manifest(char *json, struct gkdu_request *request,
		uint64_t *payload_offset, unsigned char p1_payload_sha256[32])
{
	const char *release, *disk, *p1, *kernel, *payloads, *p1_payload, *kernel_payload;
	const char *end;
	uint64_t p1_raw, kernel_raw;
	unsigned char payload_raw[32];
	if (!strstr(json, "\"board\":\"x1830-gkd-mini\"") ||
	    !strstr(json, "\"device\":\"gkd-mini\"") ||
	    !strstr(json, "\"schema\":\"gkd-mini-system-update-v1\"") ||
	    !strstr(json, "\"version\":1") ||
	    !strstr(json, "\"disk_bytes\":\"31457280000\"") ||
	    !strstr(json, "\"logical_block_bytes\":\"512\"") ||
	    !strstr(json, "\"request_slot_bytes\":\"4096\"") ||
	    !strstr(json, "\"request_slot_offset\":\"20967424\"") ||
	    !strstr(json, "\"min_battery_percent\":30") ||
	    !strstr(json, "\"apply_order\":[\"p1\",\"kernel\"]") ||
	    !strstr(json, "\"commit_last\":\"kernel\"")) return -1;
	release = object(json, "release"); disk = object(json, "disk");
	if (!release || !disk || release < disk) return -1;
	end = strchr(release, '}'); if (!end) return -1;
	if (field_hash(release, end, "source_runtime_id", request->source_runtime_id) ||
	    field_hash(release, end, "target_runtime_id", request->target_runtime_id)) return -1;
	p1 = object(disk, "p1"); kernel = object(disk, "kernel");
	payloads = object(json, "payloads");
	if (!p1 || !kernel || !payloads || !(kernel < p1 && p1 < payloads)) return -1;
	end = strchr(p1, '}');
	if (!end || field_hash(p1, end, "source_sha256", request->source_p1_sha256) ||
	    field_hash(p1, end, "target_sha256", request->target_p1_sha256)) return -1;
	end = strchr(kernel, '}');
	if (!end || field_hash(kernel, end, "source_sha256", request->source_kernel_sha256) ||
	    field_hash(kernel, end, "target_sha256", request->target_kernel_sha256)) return -1;
	p1_payload = object(payloads, "p1"); kernel_payload = object(payloads, "kernel");
	if (!p1_payload || !kernel_payload || kernel_payload > p1_payload) return -1;
	end = strchr(p1_payload, '}');
	if (!end || !strstr(p1_payload, "\"encoding\":\"raw\"") ||
	    field_u64(p1_payload, end, "bytes", &request->p1_payload_bytes) ||
	    field_hash(p1_payload, end, "sha256", p1_payload_sha256) ||
	    field_u64(p1_payload, end, "raw_bytes", &p1_raw) ||
	    field_hash(p1_payload, end, "raw_sha256", payload_raw) ||
	    p1_raw != P1_RAW_BYTES || memcmp(payload_raw, request->target_p1_sha256, 32)) return -1;
	end = strchr(kernel_payload, '}');
	if (!end || !strstr(kernel_payload, "\"encoding\":\"raw\"") ||
	    field_u64(kernel_payload, end, "bytes", &request->kernel_payload_bytes) ||
	    field_u64(kernel_payload, end, "raw_bytes", &kernel_raw) ||
	    field_hash(kernel_payload, end, "sha256", payload_raw) ||
	    kernel_raw != KERNEL_RAW_BYTES || request->kernel_payload_bytes != KERNEL_RAW_BYTES ||
	    memcmp(payload_raw, request->target_kernel_sha256, 32)) return -1;
	request->p1_payload_offset = *payload_offset;
	request->kernel_payload_offset = *payload_offset + request->p1_payload_bytes;
	return 0;
}

/* Versions are signed display data, never shell or renderer markup. */
static int version_string(const char *json,const char *key,char out[32])
{
 const char *release=object(json,"release"),*end;
 if(!release||!(end=strchr(release,'}'))||field_string(release,end,key,out,32)||!out[0]||strlen(out)>15U)return -1;
 for(const unsigned char *p=(const unsigned char *)out;*p;p++)
  if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||*p=='.'||*p=='-'||*p=='_'))return -1;
 return 0;
}

int main(int argc, char **argv)
{
	unsigned char header[12], signature[SIGNATURE_BYTES], payload_hash[32], declared_hash[32];
	struct gkdu_request request;
	struct stat st;
	char *manifest;
	uint32_t manifest_bytes;
	uint64_t payload_offset;
	int package_fd, disk_fd, recovery_fd, journal_status;
	struct gkdu_journal prior;
 int inspect=argc==7&&!strcmp(argv[6],"--inspect");
 int confirmed=argc==8&&!strcmp(argv[6],"--confirmed");
 char from_version[32]={0},to_version[32]={0};
 if(argc!=6&&!inspect&&!confirmed)return fail("usage");
 if(confirmed&&hex32(argv[7],declared_hash))return fail("confirmation-argument");
	memset(&request, 0, sizeof(request));
#if GKDU_RECOVERY_HOST
	/* R is not the installed A release. Bind its signed source to A's actual
	 * immutable kernel, not to an ID frozen into R's own capsule. P1 is mutable
	 * ext4; the existing engine snapshots and verifies its exact offline bytes. */
	if (strcmp(argv[4], "--source-kernel")) return fail("source-kernel-argument");
#else
	if (hex32(argv[4], declared_hash)) return fail("source-runtime-argument");
#endif
	package_fd = open(argv[1], O_RDONLY | O_NOFOLLOW);
	if (package_fd < 0 || fstat(package_fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0)
		return fail("package-file");
	request.package_bytes = (uint64_t)st.st_size;
	if (exact_read(package_fd, header, sizeof(header)) || memcmp(header, "GKDSU1\0\0", 8))
		return fail("container-header");
	manifest_bytes = get32(header + 8);
	if (manifest_bytes < 2 || manifest_bytes > MANIFEST_MAX) return fail("manifest-size");
	manifest = malloc((size_t)manifest_bytes + 1);
	if (!manifest || exact_read(package_fd, manifest, manifest_bytes) ||
	    exact_read(package_fd, signature, sizeof(signature))) return fail("container-short");
	manifest[manifest_bytes] = 0;
	if (!signature_ok(argv[2], (unsigned char *)manifest, manifest_bytes, signature))
		return fail("signature");
	payload_offset = 12u + manifest_bytes + SIGNATURE_BYTES;
	if (parse_manifest(manifest, &request, &payload_offset, declared_hash)) return fail("manifest");
 if((inspect||confirmed)&&(version_string(manifest,"from_version",from_version)||
    version_string(manifest,"to_version",to_version)))return fail("version");
 free(manifest);
	if (request.kernel_payload_offset + request.kernel_payload_bytes != request.package_bytes)
		return fail("container-length");
	if (sha_region(package_fd, 0, request.package_bytes, request.package_sha256) ||
	    sha_region(package_fd, request.p1_payload_offset, request.p1_payload_bytes, payload_hash))
		return fail("package-read");
 if (memcmp(payload_hash, declared_hash, 32)) return fail("p1-payload-hash");
 if(confirmed&&(hex32(argv[7],declared_hash)||memcmp(declared_hash,request.package_sha256,32)))
  return fail("confirmation-changed");
	if (sha_region(package_fd, request.kernel_payload_offset,
	    request.kernel_payload_bytes, payload_hash) ||
	    memcmp(payload_hash, request.target_kernel_sha256, 32))
		return fail("kernel-payload-hash");
#if GKDU_RECOVERY_HOST
	disk_fd = open(argv[3], O_RDONLY | O_NOFOLLOW);
	if (disk_fd < 0) return fail("source-kernel-open");
	if (sha_region(disk_fd, SOURCE_KERNEL_OFFSET, KERNEL_RAW_BYTES, declared_hash) ||
	    memcmp(request.source_kernel_sha256, declared_hash, 32)) {
		close(disk_fd);
		return fail("source-kernel");
	}
	if (close(disk_fd)) return fail("source-kernel-close");
#else
	if (hex32(argv[4], declared_hash) ||
	    memcmp(request.source_runtime_id, declared_hash, 32)) return fail("source-runtime");
#endif
	if (close(package_fd)) return fail("package-close");
	recovery_fd = open(argv[5], O_RDONLY | O_NOFOLLOW);
	if (recovery_fd < 0) return fail("recovery-open");
	journal_status = gkdu_journal_read(recovery_fd, &prior, 0);
	if (close(recovery_fd)) return fail("recovery-close");
	if (journal_status < 0) return fail("recovery-read");
 if (journal_status == 0) return fail("prior-journal-present");
 if(inspect){
  fputs("GKDSU_INSPECT=PASS package=",stdout);
  for(unsigned i=0;i<32;i++)printf("%02x",request.package_sha256[i]);
  printf(" from=%s to=%s\n",from_version,to_version);
  return 0;
 }
	disk_fd = open(argv[3], O_RDWR | O_SYNC | O_NOFOLLOW);
	if (disk_fd < 0) return fail("system-disk");
	if (gkdu_request_write(disk_fd, &request) || close(disk_fd)) return fail("request-write");
	printf("GKDSU_PREPARE=PASS package_bytes=%llu p1_offset=%llu kernel_offset=%llu\n",
		(unsigned long long)request.package_bytes,
		(unsigned long long)request.p1_payload_offset,
		(unsigned long long)request.kernel_payload_offset);
	return 0;
}
