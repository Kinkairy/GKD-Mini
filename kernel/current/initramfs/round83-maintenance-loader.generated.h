/*
 * Round64 built-in RAM-maintenance core.
 *
 * This file is included verbatim by the generated nolibc PID1 and by the
 * host/MIPS fault fixture.  The caller supplies the final, hash-pinned payload
 * manifest.  No storage writer, external initramfs, or boot-loader service is
 * used here.
 */
#ifndef GKD_ROUND64_MAINTENANCE_LOADER_H
#define GKD_ROUND64_MAINTENANCE_LOADER_H
/* Generated Round80 derivative: whole-card RW, returning runtime. */
#ifndef R80M_RETURN_TICK
#define R80M_RETURN_TICK() 0
#endif
#ifndef R80M_TRACE
#define R80M_TRACE(marker) ((void)(marker), 0)
#endif
#ifndef R64M_SEALED_RECOVERY
#define R64M_SEALED_RECOVERY 0
#endif
#ifndef R64M_DEDICATED_RECOVERY
#define R64M_DEDICATED_RECOVERY 0
#endif

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 00400000
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 02000000
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY 00200000
#endif
#ifndef MS_NOSUID
#define MS_NOSUID 2
#endif
#ifndef MS_NODEV
#define MS_NODEV 4
#endif
#ifndef MS_NOEXEC
#define MS_NOEXEC 8
#endif
#ifndef MS_RDONLY
#define MS_RDONLY 1
#endif

#ifndef R64M_OPEN
#define R64M_OPEN open
#endif
#ifndef R64M_CLOSE
#define R64M_CLOSE close
#endif
#ifndef R64M_READ
#define R64M_READ read
#endif
#ifndef R64M_WRITE
#define R64M_WRITE write
#endif
#ifndef R64M_FSYNC
#define R64M_FSYNC fsync
#endif
#ifndef R64M_LSEEK
#define R64M_LSEEK lseek
#endif
#ifndef R64M_MKDIR
#define R64M_MKDIR mkdir
#endif
#ifndef R64M_CHMOD
#define R64M_CHMOD chmod
#endif
#ifndef R64M_FCHMOD
#ifdef R64M_HOST_FIXTURE
#define R64M_FCHMOD fchmod
#else
static int r64m_fchmod_fd(int fd, unsigned int mode)
{
	return my_syscall2(__NR_fchmod, fd, mode) < 0 ? -1 : 0;
}
#define R64M_FCHMOD r64m_fchmod_fd
#endif
#endif
#ifndef R64M_CHOWN
#define R64M_CHOWN chown
#endif
#ifndef R64M_FCHOWN
#ifdef R64M_HOST_FIXTURE
#define R64M_FCHOWN fchown
#else
static int r64m_fchown_fd(int fd, unsigned int uid, unsigned int gid)
{
	return my_syscall3(__NR_fchown, fd, uid, gid) < 0 ? -1 : 0;
}
#define R64M_FCHOWN r64m_fchown_fd
#endif
#endif
#ifndef R64M_UNLINK
#define R64M_UNLINK unlink
#endif
#ifndef R64M_SYMLINK
#define R64M_SYMLINK symlink
#endif
#ifndef R64M_READLINK
#ifdef R64M_HOST_FIXTURE
#define R64M_READLINK readlink
#else
static long r64m_readlink_path(const char *path, char *buffer, unsigned long size)
{
	long result = my_syscall3(__NR_readlink, path, buffer, size);
	return result < 0 ? -1 : result;
}
#define R64M_READLINK r64m_readlink_path
#endif
#endif
#ifndef R64M_MOUNT
#define R64M_MOUNT mount
#endif
#ifndef R64M_UMOUNT2
#define R64M_UMOUNT2 umount2
#endif
#ifndef R64M_IOCTL
#define R64M_IOCTL ioctl
#endif
#ifndef R64M_FORK
#define R64M_FORK fork
#endif
#ifndef R64M_EXECVE
#define R64M_EXECVE execve
#endif
#ifndef R64M_WAITPID
#define R64M_WAITPID waitpid
#endif
#ifndef R64M_MSLEEP
#define R64M_MSLEEP msleep
#endif
#ifndef R64M_REBOOT
#define R64M_REBOOT reboot
#endif
#ifndef R64M_EXEC_WAIT
#define R64M_EXEC_WAIT r64m_exec_wait
#endif

#ifndef R64M_P1_MOUNT
#define R64M_P1_MOUNT "/run/gkd-round64-source-p1"
#endif
#ifndef R64M_P2_MOUNT
#define R64M_P2_MOUNT "/run/gkd-round64-source-p2"
#endif
#ifndef R64M_P1_DEVICE
#define R64M_P1_DEVICE "/dev/mmcblk0p1"
#endif
#ifndef R64M_P2_DEVICE
#define R64M_P2_DEVICE "/dev/mmcblk0p2"
#endif
#ifndef R80_P3_DEVICE
#define R80_P3_DEVICE "/dev/mmcblk0p3"
#endif
#ifndef R64M_RAW_DEVICE
#define R64M_RAW_DEVICE "/dev/mmcblk0"
#endif
#ifndef R64M_MOUNTS
#define R64M_MOUNTS "/proc/self/mounts"
#endif
#ifndef R64M_GADGET
#define R64M_GADGET "/sys/kernel/config/usb_gadget/gkd_round64"
#endif

#define R64M_SHA256_BYTES 32U
#define R64M_COPY_CHUNK 4096U
#define R64M_PATH_MAX 256U
#define R64M_HOST_KEY_MIN 64U
#define R64M_HOST_KEY_MAX 1048576U
#define R64M_MOUNT_FLAGS (MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC)
#ifndef R64M_AUTHORIZED_SIZE
#define R64M_AUTHORIZED_SIZE 751U
#endif
#ifndef R64M_SHADOW_SIZE
#define R64M_SHADOW_SIZE 31U
#endif
#ifndef R64M_AUTHORIZED_HASH_INIT
#define R64M_AUTHORIZED_HASH_INIT \
	{0x6d,0xf5,0xdc,0xa6,0x5e,0xfe,0x2d,0xb9,0x9d,0x2e,0xbc,0xc3,0x2b,0xe8,0x43,0x40, \
	 0x71,0xf9,0x68,0xd3,0x6b,0xdd,0x34,0xf6,0x06,0xad,0x13,0x72,0x5b,0x42,0x60,0x1b}
#endif
#ifndef R64M_SHADOW_HASH_INIT
#define R64M_SHADOW_HASH_INIT \
	{0x06,0x12,0x72,0xf4,0x1d,0x56,0x33,0xa4,0x6e,0x81,0xcd,0xcb,0x2f,0xc7,0x93,0x1f, \
	 0x4c,0xae,0x7f,0x72,0xe6,0xc5,0xdc,0xa0,0xa9,0x0c,0x4a,0x98,0x93,0x85,0xb8,0xf0}
#endif

struct r64m_sha256 {
	unsigned int h[8];
	unsigned long long bytes;
	unsigned char block[64];
	unsigned int used;
};

struct r64m_file {
	const char *source;
	const char *destination;
	unsigned long size;
	unsigned int source_mode;
	unsigned int destination_mode;
	unsigned char sha256[R64M_SHA256_BYTES];
};

struct r64m_link {
	const char *source;
	const char *destination;
	const char *target;
	unsigned int source_required;
};

struct r64m_payload {
	const struct r64m_file *p1_files;
	unsigned int p1_file_count;
	const struct r64m_link *p1_links;
	unsigned int p1_link_count;
};

#if R64M_SEALED_RECOVERY
#include "round84-recovery-capsule.generated.h"
#endif

static unsigned int r64m_rotr(unsigned int x, unsigned int n)
{
	return (x >> n) | (x << (32U - n));
}

static unsigned int r64m_be32(const unsigned char *p)
{
	return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
	       ((unsigned int)p[2] << 8) | (unsigned int)p[3];
}

static void r64m_sha256_transform(struct r64m_sha256 *c,
				  const unsigned char block[64])
{
	static const unsigned int k[64] = {
		0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,
		0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
		0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,
		0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
		0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,
		0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
		0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,
		0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
		0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,
		0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
		0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,
		0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
		0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,
		0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
		0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,
		0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
	};
	unsigned int w[64], a, b, d, e, f, g, h, t1, t2, c0;
	unsigned int i;
	for (i = 0; i < 16; ++i)
		w[i] = r64m_be32(block + i * 4U);
	for (; i < 64; ++i) {
		unsigned int s0 = r64m_rotr(w[i - 15], 7) ^
			r64m_rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
		unsigned int s1 = r64m_rotr(w[i - 2], 17) ^
			r64m_rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	a = c->h[0]; b = c->h[1]; c0 = c->h[2]; d = c->h[3];
	e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
	for (i = 0; i < 64; ++i) {
		unsigned int s1 = r64m_rotr(e, 6) ^ r64m_rotr(e, 11) ^
			r64m_rotr(e, 25);
		unsigned int ch = (e & f) ^ ((~e) & g);
		unsigned int s0 = r64m_rotr(a, 2) ^ r64m_rotr(a, 13) ^
			r64m_rotr(a, 22);
		unsigned int maj = (a & b) ^ (a & c0) ^ (b & c0);
		t1 = h + s1 + ch + k[i] + w[i]; t2 = s0 + maj;
		h = g; g = f; f = e; e = d + t1;
		d = c0; c0 = b; b = a; a = t1 + t2;
	}
	c->h[0] += a; c->h[1] += b; c->h[2] += c0; c->h[3] += d;
	c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

static void r64m_sha256_init(struct r64m_sha256 *c)
{
	static const unsigned int initial[8] = {
		0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
		0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U
	};
	memcpy(c->h, initial, sizeof(initial)); c->bytes = 0; c->used = 0;
}

static void r64m_sha256_update(struct r64m_sha256 *c,
				const unsigned char *data, unsigned long length)
{
	while (length) {
		unsigned int room = 64U - c->used;
		unsigned int take = length < room ? (unsigned int)length : room;
		memcpy(c->block + c->used, data, take);
		c->used += take; c->bytes += take; data += take; length -= take;
		if (c->used == 64U) {
			r64m_sha256_transform(c, c->block); c->used = 0;
		}
	}
}

static void r64m_sha256_final(struct r64m_sha256 *c,
			       unsigned char output[R64M_SHA256_BYTES])
{
	unsigned long long bits = c->bytes * 8ULL;
	unsigned int i;
	c->block[c->used++] = 0x80;
	if (c->used > 56U) {
		while (c->used < 64U) c->block[c->used++] = 0;
		r64m_sha256_transform(c, c->block); c->used = 0;
	}
	while (c->used < 56U) c->block[c->used++] = 0;
	for (i = 0; i < 8U; ++i)
		c->block[63U - i] = (unsigned char)(bits >> (i * 8U));
	r64m_sha256_transform(c, c->block);
	for (i = 0; i < 8U; ++i) {
		output[i * 4U] = (unsigned char)(c->h[i] >> 24);
		output[i * 4U + 1U] = (unsigned char)(c->h[i] >> 16);
		output[i * 4U + 2U] = (unsigned char)(c->h[i] >> 8);
		output[i * 4U + 3U] = (unsigned char)c->h[i];
	}
}

static int r64m_bytes_equal(const unsigned char *a, const unsigned char *b,
			     unsigned long size)
{
	unsigned long i;
	unsigned char difference = 0;
	for (i = 0; i < size; ++i) difference |= a[i] ^ b[i];
	return difference == 0;
}

#ifdef R64M_HOST_FIXTURE
#define r64m_fstat_fd(fd, st) R64M_FSTAT((fd), (st))
#define r64m_lstat_path(path, st) R64M_LSTAT((path), (st))
#else
static int r64m_copy_sys_stat(struct stat *out, const struct sys_stat_struct *in,
			      long result)
{
	if (result < 0) return -1;
	out->st_dev = in->st_dev; out->st_ino = in->st_ino;
	out->st_mode = in->st_mode; out->st_nlink = in->st_nlink;
	out->st_uid = in->st_uid; out->st_gid = in->st_gid;
	out->st_rdev = in->st_rdev; out->st_size = in->st_size;
	return 0;
}
static int r64m_fstat_fd(int fd, struct stat *out)
{
	struct sys_stat_struct in;
	return r64m_copy_sys_stat(out, &in, my_syscall2(__NR_fstat, fd, &in));
}
static int r64m_lstat_path(const char *path, struct stat *out)
{
	struct sys_stat_struct in;
#ifdef __NR_newfstatat
	long result = my_syscall4(__NR_newfstatat, AT_FDCWD, path, &in,
				  AT_SYMLINK_NOFOLLOW);
#else
	long result = my_syscall2(__NR_lstat, path, &in);
#endif
	return r64m_copy_sys_stat(out, &in, result);
}
#endif

static int r64m_join(char output[R64M_PATH_MAX], const char *root,
		      const char *path)
{
	unsigned int used = 0, i = 0;
	while (root[used]) {
		if (used + 1U >= R64M_PATH_MAX) return -1;
		output[used] = root[used]; ++used;
	}
	if (used && output[used - 1U] == '/' && path[0] == '/') ++path;
	while (path[i]) {
		if (used + 1U >= R64M_PATH_MAX) return -1;
		output[used++] = path[i++];
	}
	output[used] = 0; return 0;
}

static int r64m_exact_regular(const struct stat *st, unsigned long size,
			       unsigned int mode)
{
	return S_ISREG(st->st_mode) && st->st_uid == 0 && st->st_nlink == 1 &&
	       st->st_gid == 0 &&
	       (st->st_mode & 07777U) == mode &&
	       (unsigned long)st->st_size == size;
}

static int r64m_same_object(const struct stat *a, const struct stat *b)
{
	return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}

static int r64m_secure_parents(const char *path)
{
	char current[R64M_PATH_MAX];
	struct stat st;
	unsigned int i = 0;
	if (!path || path[0] != '/') return -1;
	while (path[i]) {
		if (i + 1U >= sizeof(current)) return -1;
		current[i] = path[i]; ++i;
		if (path[i] != '/' && path[i] != 0) continue;
		/* The final component is checked through its opened descriptor. */
		if (path[i] == 0) break;
		current[i] = 0;
		if (i > 1U && (r64m_lstat_path(current, &st) < 0 ||
		    !S_ISDIR(st.st_mode) || st.st_uid != 0 || st.st_gid != 0 ||
		    (st.st_mode & 0022U))) return -1;
	}
	return 0;
}

static int r64m_hash_fd(int fd, unsigned long expected_size,
			 unsigned char output[R64M_SHA256_BYTES])
{
	struct r64m_sha256 hash;
	unsigned char buffer[R64M_COPY_CHUNK];
	unsigned long total = 0;
	r64m_sha256_init(&hash);
	if (R64M_LSEEK(fd, 0, SEEK_SET) != 0) return -1;
	while (total < expected_size) {
		unsigned long wanted = expected_size - total;
		long count;
		if (wanted > sizeof(buffer)) wanted = sizeof(buffer);
		count = R64M_READ(fd, buffer, wanted);
		if (count != (long)wanted) return -1;
		r64m_sha256_update(&hash, buffer, wanted); total += wanted;
	}
	if (R64M_READ(fd, buffer, 1) != 0) return -1;
	r64m_sha256_final(&hash, output); return 0;
}

static int r64m_sync_parent(const char *path)
{
	char parent[R64M_PATH_MAX];
	unsigned int i = 0, slash = 0;
	int fd, result;
	while (path[i]) {
		if (i + 1U >= sizeof(parent)) return -1;
		parent[i] = path[i]; if (path[i] == '/') slash = i; ++i;
	}
	if (!slash) return -1;
	parent[slash ? slash : 1U] = 0;
	fd = R64M_OPEN(parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW, 0);
	if (fd < 0) return -1;
	result = R64M_FSYNC(fd); if (R64M_CLOSE(fd) < 0) result = -1;
	return result;
}

static int r64m_copy_file(const char *root, const struct r64m_file *file)
{
	char source[R64M_PATH_MAX];
	unsigned char source_hash[R64M_SHA256_BYTES], destination_hash[R64M_SHA256_BYTES];
	unsigned char buffer[R64M_COPY_CHUNK];
	struct stat source_before, source_after, destination_fd_stat, destination_path_stat;
	unsigned long total = 0;
	int source_fd = -1, destination_fd = -1, result = -1;
	if (file->destination_mode & 07000U) return -1;
	if (r64m_join(source, root, file->source) < 0) return -1;
	if (r64m_secure_parents(source) < 0 ||
	    r64m_secure_parents(file->destination) < 0) return -1;
	source_fd = R64M_OPEN(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
	if (source_fd < 0 || r64m_fstat_fd(source_fd, &source_before) < 0 ||
	    !r64m_exact_regular(&source_before, file->size,
				 file->source_mode)) goto out;
	if (r64m_hash_fd(source_fd, file->size, source_hash) < 0 ||
	    !r64m_bytes_equal(source_hash, file->sha256, sizeof(source_hash)) ||
	    r64m_fstat_fd(source_fd, &source_after) < 0 ||
	    !r64m_same_object(&source_before, &source_after) ||
	    !r64m_exact_regular(&source_after, file->size,
				 file->source_mode)) goto out;
	if (R64M_LSEEK(source_fd, 0, SEEK_SET) != 0) goto out;
	destination_fd = R64M_OPEN(file->destination,
		O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (destination_fd < 0) goto out;
	while (total < file->size) {
		unsigned long wanted = file->size - total;
		long count;
		if (wanted > sizeof(buffer)) wanted = sizeof(buffer);
		count = R64M_READ(source_fd, buffer, wanted);
		if (count != (long)wanted ||
		    R64M_WRITE(destination_fd, buffer, wanted) != (long)wanted) goto out;
		total += wanted;
	}
	if (R64M_READ(source_fd, buffer, 1) != 0 ||
	    r64m_fstat_fd(source_fd, &source_after) < 0 ||
	    !r64m_same_object(&source_before, &source_after) ||
	    !r64m_exact_regular(&source_after, file->size, file->source_mode) ||
	    R64M_FSYNC(destination_fd) < 0 ||
	    R64M_FCHOWN(destination_fd, 0, 0) < 0 ||
	    R64M_FCHMOD(destination_fd, file->destination_mode) < 0 ||
	    R64M_FSYNC(destination_fd) < 0 ||
	    r64m_fstat_fd(destination_fd, &destination_fd_stat) < 0 ||
	    !r64m_exact_regular(&destination_fd_stat, file->size,
				 file->destination_mode) ||
	    r64m_lstat_path(file->destination, &destination_path_stat) < 0 ||
	    !r64m_same_object(&destination_fd_stat, &destination_path_stat) ||
	    !r64m_exact_regular(&destination_path_stat, file->size,
				 file->destination_mode) ||
	    r64m_hash_fd(destination_fd, file->size, destination_hash) < 0 ||
	    !r64m_bytes_equal(destination_hash, file->sha256,
			      sizeof(destination_hash)) ||
	    r64m_fstat_fd(destination_fd, &destination_fd_stat) < 0 ||
	    !r64m_exact_regular(&destination_fd_stat, file->size,
				 file->destination_mode)) goto out;
	if (R64M_CLOSE(destination_fd) < 0) {
		destination_fd = -1; goto out;
	}
	destination_fd = -1;
	if (r64m_sync_parent(file->destination) < 0) goto out;
	result = 0;
out:
	if (source_fd >= 0) (void)R64M_CLOSE(source_fd);
	if (destination_fd >= 0) (void)R64M_CLOSE(destination_fd);
	if (result < 0) (void)R64M_UNLINK(file->destination);
	return result;
}

static int r64m_dynamic_file(const char *source, const char *destination,
			      unsigned long minimum, unsigned long maximum,
			      unsigned int mode)
{
	struct r64m_file file;
	struct stat st;
	unsigned char source_hash[R64M_SHA256_BYTES];
	if (r64m_secure_parents(source) < 0 ||
	    r64m_secure_parents(destination) < 0) return -1;
	int fd = R64M_OPEN(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
	if (fd < 0 || r64m_fstat_fd(fd, &st) < 0 || !S_ISREG(st.st_mode) ||
	    st.st_uid != 0 || st.st_nlink != 1 || (st.st_mode & 07777U) != mode ||
	    st.st_gid != 0 ||
	    st.st_size < (long)minimum || st.st_size > (long)maximum ||
	    r64m_hash_fd(fd, (unsigned long)st.st_size, source_hash) < 0) {
		if (fd >= 0) (void)R64M_CLOSE(fd);
		return -1;
	}
	if (R64M_CLOSE(fd) < 0) return -1;
	file.source = source; file.destination = destination;
	file.size = (unsigned long)st.st_size;
	file.source_mode = mode; file.destination_mode = mode;
	memcpy(file.sha256, source_hash, sizeof(source_hash));
	return r64m_copy_file("", &file);
}

static int r64m_copy_link(const char *root, const struct r64m_link *link)
{
	char source[R64M_PATH_MAX], value[R64M_PATH_MAX];
	struct stat st;
	long length;
	if (link->source_required) {
		if (r64m_join(source, root, link->source) < 0 ||
		    r64m_secure_parents(source) < 0 ||
		    r64m_lstat_path(source, &st) < 0 || !S_ISLNK(st.st_mode) ||
		    st.st_uid != 0 || st.st_gid != 0 ||
		    (st.st_mode & 07777U) != 0777U) return -1;
		length = R64M_READLINK(source, value, sizeof(value) - 1U);
		if (length < 0 || (unsigned long)length >= sizeof(value)) return -1;
		value[length] = 0;
		if (strcmp(value, link->target)) return -1;
	}
	if (r64m_secure_parents(link->destination) < 0 ||
	    R64M_SYMLINK(link->target, link->destination) < 0 ||
	    r64m_lstat_path(link->destination, &st) < 0 || !S_ISLNK(st.st_mode) ||
	    st.st_uid != 0 || st.st_gid != 0 ||
	    (st.st_mode & 07777U) != 0777U) goto fail;
	length = R64M_READLINK(link->destination, value, sizeof(value) - 1U);
	if (length < 0 || (unsigned long)length >= sizeof(value)) goto fail;
	value[length] = 0;
	if (strcmp(value, link->target) || r64m_sync_parent(link->destination) < 0)
		goto fail;
	return 0;
fail:
	(void)R64M_UNLINK(link->destination); return -1;
}

static int r64m_exact_file(const char *path, const char *value,
			    unsigned int mode)
{
	struct stat st;
	char check[192];
	unsigned long length = strlen(value);
	int fd;
	if (length >= sizeof(check)) return -1;
	fd = R64M_OPEN(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
		       0600);
	if (fd < 0 || R64M_WRITE(fd, value, length) != (long)length ||
	    R64M_FSYNC(fd) < 0 || R64M_CLOSE(fd) < 0) {
		if (fd >= 0) (void)R64M_CLOSE(fd);
		(void)R64M_UNLINK(path);
		return -1;
	}
	if (R64M_CHOWN(path, 0, 0) < 0 || R64M_CHMOD(path, mode) < 0 ||
	    r64m_lstat_path(path, &st) < 0 ||
	    !r64m_exact_regular(&st, length, mode) || r64m_sync_parent(path) < 0)
		goto fail;
	fd = R64M_OPEN(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
	if (fd < 0 || R64M_READ(fd, check, length) != (long)length ||
	    R64M_READ(fd, check + length, 1) != 0 || R64M_CLOSE(fd) < 0)
		goto fail_closed;
	check[length] = 0;
	if (strcmp(check, value)) goto fail;
	return 0;
fail_closed:
	if (fd >= 0) (void)R64M_CLOSE(fd);
fail:
	(void)R64M_UNLINK(path); return -1;
}

static int r64m_secure_directory(const char *path, unsigned int mode)
{
	struct stat st;
	(void)R64M_MKDIR(path, mode);
	if (r64m_lstat_path(path, &st) < 0 || !S_ISDIR(st.st_mode) ||
	    st.st_uid != 0 || st.st_gid != 0 || R64M_CHOWN(path, 0, 0) < 0 ||
	    R64M_CHMOD(path, mode) < 0 || r64m_lstat_path(path, &st) < 0 ||
	    !S_ISDIR(st.st_mode) || st.st_uid != 0 || st.st_gid != 0 ||
	    (st.st_mode & 07777U) != mode) return -1;
	return 0;
}

static int r64m_prepare_directories(void)
{
	static const struct { const char *path; unsigned int mode; } directories[] = {
		{"/run",0755},{R64M_P1_MOUNT,0700},
		{"/run/gkd-recovery-capsule",0700},
		{"/run/gkd-ui",0700},
		{"/run/gkd-menu-owner",0700},
		{R64M_P2_MOUNT,0700},{"/run/gkd-ram-rescue",0700},
		{"/run/gkd-round64-ssh",0700},{"/run/gkd-usb-round64",0700},
		{"/var",0755},{"/var/run",0755},{"/var/lib",0755},{"/var/log",0755},
		{"/media",0755},{"/media/gkd-r-screenshots",0700},
		{"/usr",0755},{"/usr/bin",0755},{"/usr/sbin",0755},
		{"/usr/lib",0755},{"/usr/lib/gkd-usb-round64",0755},
		{"/usr/local",0755},{"/usr/local/home",0700},
		{"/usr/local/home/.ssh",0700},{"/etc",0755},
		{"/etc/gkd-mini",0755},{"/etc/gkd-mini/fonts",0755},
		{"/etc/init.d",0755},{"/bin",0755},{"/sbin",0755},{"/lib",0755},{"/tmp",01777}
	};
	static const char hexadecimal[] = "0123456789ABCDEF";
	unsigned int i;
	for (i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i) {
		if (r64m_secure_directory(directories[i].path,
					 directories[i].mode) < 0) {
			char marker[4] = {'Q', '0', '0', '0'};
			marker[2] = hexadecimal[(i >> 4) & 15U];
			marker[3] = hexadecimal[i & 15U];
			(void)R80M_TRACE(marker);
			return -1;
		}
	}
	return 0;
}

static int r64m_mount_pseudo(void)
{
	if (R64M_MOUNT("devtmpfs", "/dev", "devtmpfs", MS_NOSUID, 0) < 0 ||
	    R64M_MOUNT("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, 0) < 0 ||
	    R64M_MOUNT("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, 0) < 0)
		return -1;
	return r64m_secure_directory("/dev/pts", 0755);
}

/* A child mount namespace may have exited before its final superblock
 * reference is released.  Retry only the production EBUSY result, within the
 * same five-second bound already used by the P2 rescue mount. */
static int r64m_mount_readonly_wait(const char *device, const char *path,
				     const char *filesystem)
{
#ifdef R64M_HOST_FIXTURE
	return R64M_MOUNT(device, path, filesystem, R64M_MOUNT_FLAGS, "noload");
#else
	unsigned int step;
	int result = -1;
#define R64M_READONLY_MOUNT_STEPS 50U
	for (step = 0; step < R64M_READONLY_MOUNT_STEPS; ++step) {
		result = (int)my_syscall5(__NR_mount, device, path, filesystem,
					  R64M_MOUNT_FLAGS, "noload");
		if (result == 0 || result != -16) break;
		(void)R64M_MSLEEP(100);
	}
#undef R64M_READONLY_MOUNT_STEPS
	return result;
#endif
}

static int r64m_copy_p1(const struct r64m_payload *payload)
{
	unsigned int i;
	if (r64m_mount_readonly_wait(R64M_P1_DEVICE, R64M_P1_MOUNT,
				    "ext4") < 0) return -1;
	for (i = 0; i < payload->p1_file_count; ++i) {
		(void)R64M_UNLINK(payload->p1_files[i].destination);
		if (r64m_copy_file(R64M_P1_MOUNT, &payload->p1_files[i]) < 0) goto fail;
	}
	for (i = 0; i < payload->p1_link_count; ++i) {
		(void)R64M_UNLINK(payload->p1_links[i].destination);
		if (r64m_copy_link(R64M_P1_MOUNT, &payload->p1_links[i]) < 0) goto fail;
	}
	if (R64M_UMOUNT2(R64M_P1_MOUNT, 0) < 0) return -1;
	return 0;
fail:
	(void)R64M_UMOUNT2(R64M_P1_MOUNT, 0); return -1;
}

#if R64M_SEALED_RECOVERY
#define R84_LOOP_SET_FD 0x4c00
#define R84_LOOP_CLR_FD 0x4c01
#define R84_LOOP_GET_STATUS64 0x4c05
#define R84_LOOP_SET_STATUS64 0x4c04
#define R84_LO_FLAGS_READ_ONLY 1U
#define R84_RECOVERY_MOUNT "/run/gkd-recovery-capsule"

struct r84_loop_info64 {
	unsigned long long device;
	unsigned long long inode;
	unsigned long long rdevice;
	unsigned long long offset;
	unsigned long long sizelimit;
	unsigned int number;
	unsigned int encrypt_type;
	unsigned int encrypt_key_size;
	unsigned int flags;
	unsigned char file_name[64];
	unsigned char crypt_name[64];
	unsigned char encrypt_key[32];
	unsigned long long init[2];
};
typedef char r84_loop_info64_size_must_be_232[
	sizeof(struct r84_loop_info64) == 232U ? 1 : -1];

static int r84_hash_region(int fd, off_t offset, unsigned long bytes,
			   unsigned char output[R64M_SHA256_BYTES])
{
	struct r64m_sha256 hash;
	unsigned char buffer[R64M_COPY_CHUNK];
	unsigned long total = 0;
	r64m_sha256_init(&hash);
	if (R64M_LSEEK(fd, offset, SEEK_SET) != offset) return -1;
	while (total < bytes) {
		unsigned long wanted = bytes - total;
		long count;
		if (wanted > sizeof(buffer)) wanted = sizeof(buffer);
		count = R64M_READ(fd, buffer, wanted);
		if (count != (long)wanted) return -1;
		r64m_sha256_update(&hash, buffer, wanted);
		total += wanted;
	}
	r64m_sha256_final(&hash, output);
	return 0;
}

static int r84_loop_path(char path[32], unsigned int index)
{
	static const char prefix[] = "/dev/loop";
	unsigned int used = sizeof(prefix) - 1U;
	if (index >= 16U) return -1;
	memcpy(path, prefix, used);
	if (index >= 10U) path[used++] = (char)('0' + index / 10U);
	path[used++] = (char)('0' + index % 10U);
	path[used] = 0;
	return 0;
}

static int r84_copy_payload(const char *root, const struct r64m_payload *payload)
{
	unsigned int i;
	for (i = 0; i < payload->p1_file_count; ++i) {
		(void)R64M_UNLINK(payload->p1_files[i].destination);
		if (r64m_copy_file(root, &payload->p1_files[i]) < 0) return -1;
	}
	for (i = 0; i < payload->p1_link_count; ++i) {
		(void)R64M_UNLINK(payload->p1_links[i].destination);
		if (r64m_copy_link(root, &payload->p1_links[i]) < 0) return -1;
	}
	return 0;
}

static int r84_copy_sealed_recovery(const struct r64m_payload *payload)
{
	unsigned char actual[R64M_SHA256_BYTES];
	struct r84_loop_info64 info;
	char loop_path[32];
	unsigned int index;
	int raw_fd = -1, loop_fd = -1, attached = 0, mounted = 0;
	int result = -1;

	raw_fd = R64M_OPEN(R64M_RAW_DEVICE,
		O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
	if (raw_fd < 0 ||
	    r84_hash_region(raw_fd, R84_RECOVERY_CAPSULE_OFFSET,
		R84_RECOVERY_CAPSULE_BYTES, actual) < 0 ||
	    !r64m_bytes_equal(actual, r84_recovery_capsule_sha256,
			       sizeof(actual))) goto out;
	for (index = 0; index < 16U; ++index) {
		if (r84_loop_path(loop_path, index) < 0) goto out;
		loop_fd = R64M_OPEN(loop_path,
			O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
		if (loop_fd < 0) continue;
		memset(&info, 0, sizeof(info));
		if (R64M_IOCTL(loop_fd, R84_LOOP_GET_STATUS64, &info) < 0)
			break;
		(void)R64M_CLOSE(loop_fd);
		loop_fd = -1;
	}
	if (loop_fd < 0 || index == 16U ||
	    R64M_IOCTL(loop_fd, R84_LOOP_SET_FD,
		       (void *)(unsigned long)raw_fd) < 0) goto out;
	attached = 1;
	memset(&info, 0, sizeof(info));
	info.offset = (unsigned long long)R84_RECOVERY_CAPSULE_OFFSET;
	info.sizelimit = (unsigned long long)R84_RECOVERY_CAPSULE_BYTES;
	info.flags = R84_LO_FLAGS_READ_ONLY;
	memcpy(info.file_name, "gkd-recovery-capsule",
	       sizeof("gkd-recovery-capsule"));
	if (R64M_IOCTL(loop_fd, R84_LOOP_SET_STATUS64, &info) < 0) goto out;
	if (R64M_CLOSE(raw_fd) < 0) goto out;
	raw_fd = -1;
	if (R64M_MOUNT(loop_path, R84_RECOVERY_MOUNT, "squashfs",
		R64M_MOUNT_FLAGS, 0) < 0) goto out;
	mounted = 1;
	if (r84_copy_payload(R84_RECOVERY_MOUNT, payload) < 0 ||
	    r84_copy_payload(R84_RECOVERY_MOUNT,
			     &r84_recovery_extra_payload) < 0) goto out;
	result = 0;
out:
	if (mounted && R64M_UMOUNT2(R84_RECOVERY_MOUNT, 0) < 0) result = -1;
	if (attached && R64M_IOCTL(loop_fd, R84_LOOP_CLR_FD, 0) < 0) result = -1;
	if (loop_fd >= 0 && R64M_CLOSE(loop_fd) < 0) result = -1;
	if (raw_fd >= 0 && R64M_CLOSE(raw_fd) < 0) result = -1;
	memset(actual, 0, sizeof(actual));
	return result;
}
#endif

static int r64m_verify_embedded(const struct r64m_payload *payload)
{
	unsigned int i;
	for (i = 0; i < payload->p1_file_count; ++i) {
		const struct r64m_file *file = &payload->p1_files[i];
		struct stat st;
		unsigned char actual[R64M_SHA256_BYTES];
		int fd = R64M_OPEN(file->destination,
			O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
		if (fd < 0 || r64m_fstat_fd(fd, &st) < 0 ||
		    !r64m_exact_regular(&st, file->size, file->destination_mode) ||
		    r64m_hash_fd(fd, file->size, actual) < 0 ||
		    !r64m_bytes_equal(actual, file->sha256, sizeof(actual)) ||
		    R64M_CLOSE(fd) < 0) {
			if (fd >= 0) (void)R64M_CLOSE(fd);
			return -1;
		}
	}
	for (i = 0; i < payload->p1_link_count; ++i) {
		const struct r64m_link *link = &payload->p1_links[i];
		char value[R64M_PATH_MAX];
		struct stat st;
		long length = R64M_READLINK(link->destination, value, sizeof(value) - 1U);
		if (length < 0 || (unsigned long)length >= sizeof(value) ||
		    r64m_lstat_path(link->destination, &st) < 0 || !S_ISLNK(st.st_mode))
			return -1;
		value[length] = 0;
		if (strcmp(value, link->target)) return -1;
	}
	return 0;
}

static int r64m_copy_p2(void)
{
	char source[R64M_PATH_MAX];
	static const char hexadecimal[] = "0123456789ABCDEF";
	char mount_marker[4] = {'E', '0', '0', '0'};
	int mount_result;
	int result = -1;
	mount_result = r64m_mount_readonly_wait(R64M_P2_DEVICE, R64M_P2_MOUNT,
					       "ext3");
	if (mount_result < 0) {
		unsigned int error = (unsigned int)(-mount_result);
		mount_marker[1] = hexadecimal[(error >> 8) & 15U];
		mount_marker[2] = hexadecimal[(error >> 4) & 15U];
		mount_marker[3] = hexadecimal[error & 15U];
		(void)R80M_TRACE(mount_marker); return -1;
	}
	(void)R64M_UNLINK("/run/gkd-round64-ssh/authorized_keys");
	(void)R64M_UNLINK("/etc/shadow");
	(void)R64M_UNLINK("/run/gkd-round64-ssh/dropbear_rsa_host_key");
	(void)R64M_UNLINK("/run/gkd-round64-ssh/dropbear_ed25519_host_key");
	if (r64m_join(source, R64M_P2_MOUNT,
		      "/local/home/.ssh/authorized_keys") < 0 ||
	    r64m_dynamic_file(source,
		      "/run/gkd-round64-ssh/authorized_keys",
		      1U, 1048576U, 0600) < 0) {
		(void)R80M_TRACE("8C02"); goto out;
	}
	if (r64m_join(source, R64M_P2_MOUNT, "/local/etc/shadow") < 0 ||
	    r64m_dynamic_file(source, "/etc/shadow", 1U, 4096U, 0600) < 0) {
		(void)R80M_TRACE("8C03"); goto out;
	}
	if (r64m_join(source, R64M_P2_MOUNT,
		      "/local/etc/gkd-mini/dropbear_rsa_host_key") < 0) {
		(void)R80M_TRACE("8C04"); goto out;
	}
	if (r64m_dynamic_file(source,
		      "/run/gkd-round64-ssh/dropbear_rsa_host_key",
		      R64M_HOST_KEY_MIN, R64M_HOST_KEY_MAX, 0600) < 0) {
		if (r64m_join(source, R64M_P2_MOUNT,
			      "/local/etc/gkd-mini/dropbear_ed25519_host_key") < 0 ||
		    r64m_dynamic_file(source,
			      "/run/gkd-round64-ssh/dropbear_ed25519_host_key",
			      R64M_HOST_KEY_MIN, R64M_HOST_KEY_MAX, 0600) < 0)
			{ (void)R80M_TRACE("8C05"); goto out; }
	}
	result = 0;
out:
	if (R64M_UMOUNT2(R64M_P2_MOUNT, 0) < 0) {
		(void)R80M_TRACE("8C06"); result = -1;
	}
	return result;
}

#if R64M_SEALED_RECOVERY
/* P2 customisation is optional in R.  Missing or rejected files preserve the
 * sealed English configuration and fallback font from the recovery capsule. */
static int r84_copy_ui_assets(void)
{
	char source[R64M_PATH_MAX];
	int mounted;
	mounted = r64m_mount_readonly_wait(R64M_P2_DEVICE, R64M_P2_MOUNT,
					    "ext3") == 0;
	if (!mounted) return 0;
	(void)R64M_UNLINK("/run/gkd-ui/gdkmini.override.conf");
	(void)R64M_UNLINK("/run/gkd-ui/ui.psf");
	if (r64m_join(source, R64M_P2_MOUNT,
		      "/local/etc/gkd-mini/gdkmini.ui.conf") == 0)
		(void)r64m_dynamic_file(source,
			"/run/gkd-ui/gdkmini.override.conf", 1U, 65536U, 0644);
	if (r64m_join(source, R64M_P2_MOUNT,
		      "/local/etc/gkd-mini/ui.psf") == 0)
		(void)r64m_dynamic_file(source, "/run/gkd-ui/ui.psf",
			32U, 8U * 1024U * 1024U, 0644);
	if (R64M_UMOUNT2(R64M_P2_MOUNT, 0) < 0) {
		(void)R80M_TRACE("8C06");
		return -1;
	}
	return 0;
}
#endif

static int r64m_mounts_offline(void)
{
	char buffer[16384];
	long count;
	unsigned long i = 0;
	int fd = R64M_OPEN(R64M_MOUNTS, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
	if (fd < 0) return -1;
	count = R64M_READ(fd, buffer, sizeof(buffer) - 1U);
	if (count < 0 || R64M_READ(fd, buffer, 1) != 0 || R64M_CLOSE(fd) < 0)
		return -1;
	buffer[count] = 0;
	while (i < (unsigned long)count) {
		unsigned long start = i;
		while (i < (unsigned long)count && buffer[i] != ' ' && buffer[i] != '\n') ++i;
		if (i - start >= strlen(R64M_RAW_DEVICE) &&
		    !memcmp(buffer + start, R64M_RAW_DEVICE,
		            strlen(R64M_RAW_DEVICE))) {
			char marker[4] = {'M', '0', '0', 'F'};
			if (i - start == strlen(R64M_RAW_DEVICE)) marker[3] = '0';
			else if (i - start == strlen(R64M_P1_DEVICE) &&
			         !memcmp(buffer + start, R64M_P1_DEVICE, i - start)) marker[3] = '1';
			else if (i - start == strlen(R64M_P2_DEVICE) &&
			         !memcmp(buffer + start, R64M_P2_DEVICE, i - start)) marker[3] = '2';
			else if (i - start == strlen(R80_P3_DEVICE) &&
			         !memcmp(buffer + start, R80_P3_DEVICE, i - start)) marker[3] = '3';
			(void)R80M_TRACE(marker);
			return -1;
		}
		while (i < (unsigned long)count && buffer[i] != '\n') ++i;
		if (i < (unsigned long)count) ++i;
	}
	return 0;
}

static int r80_swap_offline(void)
{
	char buffer[4096];
	long count, i;
	int fd = R64M_OPEN("/proc/swaps", O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
	if (fd < 0) return -1;
	count = R64M_READ(fd, buffer, sizeof(buffer) - 1U);
	if (count < 0 || R64M_READ(fd, buffer + count, 1) != 0 ||
	    R64M_CLOSE(fd) < 0) return -1;
	buffer[count] = 0;
	/* A header plus any second line means at least one active swap. */
	for (i = 0; i < count; ++i)
		if (buffer[i] == '\n' && i + 1 < count) return -1;
	return 0;
}

static int r80_set_writable(const char *device)
{
	int zero = 0, value = 1;
	int fd = R64M_OPEN(device, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
	if (fd < 0 || R64M_IOCTL(fd, BLKROSET, &zero) < 0 ||
	    R64M_IOCTL(fd, BLKROGET, &value) < 0 || value != 0 ||
	    R64M_CLOSE(fd) < 0) {
		if (fd >= 0) (void)R64M_CLOSE(fd);
		return -1;
	}
	return 0;
}

static int r80_all_writable(void)
{
	return r80_set_writable(R64M_RAW_DEVICE) < 0 ||
	       r80_set_writable(R64M_P1_DEVICE) < 0 ||
	       r80_set_writable(R64M_P2_DEVICE) < 0 ||
	       r80_set_writable(R80_P3_DEVICE) < 0 ? -1 : 0;
}

static int r64m_exec_wait(const char *program, char *const argv[],
			   char *const envp[])
{
	int status = 0;
	int child = R64M_FORK();
	if (child < 0) return -1;
	if (child == 0) {
		R64M_EXECVE(program, argv, envp); exit(127);
	}
	if (R64M_WAITPID(child, &status, 0) != child || status != 0) return -1;
	return 0;
}

static int r64m_composer_enter(void)
{
	char *argv[] = { "/usr/sbin/gkd-usb-mode", "enter-debug", 0 };
	char *envp[] = { "HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin",
		"TERM=linux", "ROUND64_RESCUE=p1-root-offline",
		"ROUND64_SSH_IDENTITY_DIR=/run/gkd-round64-ssh", 0 };
	return R64M_EXEC_WAIT(argv[0], argv, envp);
}

static int r64m_composer_teardown(void)
{
	char *argv[] = { "/usr/sbin/gkd-usb-mode", "teardown", "maintenance", 0 };
	char *envp[] = { "HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin",
		"TERM=linux", "ROUND64_RESCUE=p1-root-offline",
		"ROUND64_SSH_IDENTITY_DIR=/run/gkd-round64-ssh", 0 };
	return R64M_EXEC_WAIT(argv[0], argv, envp);
}

#if R64M_SEALED_RECOVERY
static int r84_recovery_ui_enter(void)
{
	char *argv[] = { "/usr/sbin/gkd-recovery-ui", 0 };
	char *envp[] = { "HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin",
		"TERM=linux", 0 };
	return R64M_EXEC_WAIT(argv[0], argv, envp);
}

#if R64M_DEDICATED_RECOVERY
static int r84_recovery_usb(const char *action)
{
	char *argv[] = { "/usr/sbin/gkd-recovery-usb",
		(char *)action, 0 };
	char *envp[] = { "HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin",
		"TERM=linux", 0 };
	return R64M_EXEC_WAIT(argv[0], argv, envp);
}

static int r84_recovery_input_start(void)
{
	char *argv[] = { "/etc/init.d/S95gkd-input", "start", 0 };
	char *envp[] = { "HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin",
		"TERM=linux", "GKD_INPUT_REQUIRE_OWNER=1", 0 };
	if (r64m_exact_file("/run/gkd-menu-owner/test-session", "", 0600) < 0)
		return -1;
	return R64M_EXEC_WAIT(argv[0], argv, envp);
}

#endif
#endif

#if !R64M_DEDICATED_RECOVERY
static int r64m_mass_storage_enter(void)
{
	char *argv[] = { "/usr/sbin/gkd-recovery-mass-storage", "start", 0 };
	char *envp[] = { "HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin",
		"TERM=linux", 0 };
	return R64M_EXEC_WAIT(argv[0], argv, envp);
}

static int r64m_render(const char *status)
{
	char *argv[] = { "/usr/lib/gkd-usb-round64/gkd-round64-rescue-ui", 0 };
	char *envp[] = { "HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin",
		"TERM=linux", 0 };
	(void)R64M_UNLINK("/run/gkd-ram-rescue/ui.status");
	if (r64m_exact_file("/run/gkd-ram-rescue/ui.status", status, 0600) < 0)
		return -1;
	return R64M_EXEC_WAIT(argv[0], argv, envp);
}

#ifndef R64M_RENDER_ACTIVE
#define R64M_RENDER_ACTIVE(status) r64m_render(status)
#endif
#endif

static int r64m_gadget_absent(void)
{
	struct stat st;
	return r64m_lstat_path(R64M_GADGET, &st) < 0;
}

static int r64m_read_small(const char *path, char *buffer, unsigned int size)
{
	long count;
	int fd = R64M_OPEN(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
	if (fd < 0 || size < 2U) return -1;
	count = R64M_READ(fd, buffer, size - 1U);
	if (count < 0 || R64M_READ(fd, buffer + count, 1) != 0 ||
	    R64M_CLOSE(fd) < 0) return -1;
	buffer[count] = 0; return (int)count;
}

static int r64m_wait_release(void)
{
	char udc[96], path[R64M_PATH_MAX], state[64], lun[128];
	int configured = 0, link_seen = 0;
	if (r64m_read_small(R64M_GADGET "/UDC", udc, sizeof(udc)) <= 0) return -1;
	while (udc[strlen(udc) - 1U] == '\n') udc[strlen(udc) - 1U] = 0;
	if (r64m_join(path, "/sys/class/udc/", udc) < 0 ||
	    r64m_join(path, path, "/state") < 0) return -1;
	for (;;) {
		/* Confirmed A performs a device-side eject: stop the gadget first,
		 * then flush the now-exclusive raw block device before restore. */
		if (R80M_RETURN_TICK()) {
			int raw;
			if (r64m_composer_teardown() < 0 || !r64m_gadget_absent()) return -1;
			raw = R64M_OPEN(R64M_RAW_DEVICE, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
			if (raw < 0) return -1;
			if (R64M_IOCTL(raw, BLKFLSBUF, 0) < 0) {
				(void)R64M_CLOSE(raw); return -1;
			}
			if (R64M_CLOSE(raw) < 0) return -1;
			return 0;
		}
		if (r64m_read_small(path, state, sizeof(state)) < 0) return -1;
		if (strncmp(state, "not attached", 12)) link_seen = 1;
		if (!strncmp(state, "configured", 10)) configured = 1;
		if (!strncmp(state, "not attached", 12) && link_seen) return 0;
		if (configured) {
			if (r64m_read_small(R64M_GADGET
				"/functions/mass_storage.0/lun.0/file", lun,
				sizeof(lun)) == 0) return 0;
		}
		(void)R64M_MSLEEP(250);
	}
}

static int r64m_markers(int credentials_ready)
{
	/* The permanent RAM supervisor survives a DEBUG/RETURN cycle. */
	(void)R64M_UNLINK("/etc/shells");
	(void)R64M_UNLINK("/run/gkd-ram-rescue/p1-root-offline");
	(void)R64M_UNLINK("/run/gkd-round64-ssh/shadow-verified");
	(void)R64M_UNLINK("/run/gkd-round64-ssh/verified");
	if (r64m_exact_file("/etc/shells", "/bin/sh\n", 0644) < 0 ||
	    r64m_exact_file("/run/gkd-ram-rescue/p1-root-offline",
			     "P1_ROOT_OFFLINE=1\n", 0600) < 0) return -1;
	if (!credentials_ready) return 0;
	return r64m_exact_file("/run/gkd-round64-ssh/shadow-verified",
			      "SHADOW_IN_TMPFS=1\n", 0600) < 0 ||
	       r64m_exact_file("/run/gkd-round64-ssh/verified",
			      "IDENTITY_IN_TMPFS=1\n", 0600) < 0 ? -1 : 0;
}

static int r80_sysfs_write(const char *path, const char *value)
{
	unsigned long length = strlen(value);
	int fd = R64M_OPEN(path, O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0);
	int result = -1;
	if (fd < 0) return -1;
	if (R64M_WRITE(fd, value, length) == (long)length) result = 0;
	if (R64M_CLOSE(fd) < 0) result = -1;
	return result;
}

#define R80_EV_KEY 0x01U
#define R80_EV_MAX 0x1fU
#define R80_KEY_LEFTCTRL 29U
#define R80_KEY_LEFTALT 56U
#define R80_KEY_KPMINUS 74U
#define R80_KEY_KPPLUS 78U
#define R80_KEY_UP 103U
#define R80_KEY_DOWN 108U
#define R80_KEY_HOME 102U
#define R80_KEY_MAX 0x2ffU
#define R80_RETURN_POLL_MS 20U
#define R80_RETURN_POLL_STEPS 250U
#define R80_EVIOCGNAME(length) _IOC(_IOC_READ, 'E', 0x06, (length))
#define R80_EVIOCGBIT(event, length) \
	_IOC(_IOC_READ, 'E', 0x20 + (event), (length))
#define R80_EVIOCGKEY(length) _IOC(_IOC_READ, 'E', 0x18, (length))

static int r80_input_bit(const unsigned long *bits, unsigned int bit)
{
	unsigned int width = sizeof(unsigned long) * 8U;
	return !!(bits[bit / width] & (1UL << (bit % width)));
}

static int r80_event_path(char path[32], unsigned int index)
{
	static const char prefix[] = "/dev/input/event";
	unsigned int used = sizeof(prefix) - 1U;
	if (index >= 100U)
		return -1;
	memcpy(path, prefix, used);
	if (index >= 10U)
		path[used++] = (char)('0' + index / 10U);
	path[used++] = (char)('0' + index % 10U);
	path[used] = 0;
	return 0;
}

static int r80_gpio_keys_ready(void)
{
	unsigned long events[(R80_EV_MAX + sizeof(unsigned long) * 8U) /
			     (sizeof(unsigned long) * 8U)];
	unsigned long keys[(R80_KEY_MAX + sizeof(unsigned long) * 8U) /
			   (sizeof(unsigned long) * 8U)];
	char name[64];
	char path[32];
	unsigned int index;
	int fd;
	int match = 0;

	for (index = 0; index < 32U; ++index) {
		if (r80_event_path(path, index) < 0)
			return 0;
		fd = R64M_OPEN(path,
			O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW, 0);
		if (fd < 0)
			continue;
		memset(name, 0, sizeof(name));
		memset(events, 0, sizeof(events));
		memset(keys, 0, sizeof(keys));
		if (R64M_IOCTL(fd, R80_EVIOCGNAME(sizeof(name)), name) >= 0 &&
		    !strcmp(name, "gpio-keys") &&
		    R64M_IOCTL(fd, R80_EVIOCGBIT(0, sizeof(events)), events) >= 0 &&
		    r80_input_bit(events, R80_EV_KEY) &&
		    R64M_IOCTL(fd, R80_EVIOCGBIT(R80_EV_KEY, sizeof(keys)), keys) >= 0 &&
		    r80_input_bit(keys, R80_KEY_UP) &&
		    r80_input_bit(keys, R80_KEY_DOWN) &&
		    r80_input_bit(keys, R80_KEY_LEFTCTRL) &&
		    r80_input_bit(keys, R80_KEY_LEFTALT) &&
		    r80_input_bit(keys, R80_KEY_KPPLUS) &&
		    r80_input_bit(keys, R80_KEY_KPMINUS))
			++match;
		(void)R64M_CLOSE(fd);
	}
	return match == 1;
}

static int r80_recovery_menu_held(void)
{
	unsigned long supported[(R80_KEY_MAX + sizeof(unsigned long) * 8U) /
				(sizeof(unsigned long) * 8U)];
	unsigned long pressed[(R80_KEY_MAX + sizeof(unsigned long) * 8U) /
			       (sizeof(unsigned long) * 8U)];
	char name[64], path[32];
	unsigned int index, step;
	int fd = -1;
	for (index = 0; index < 32U; ++index) {
		if (r80_event_path(path, index) < 0) return -1;
		fd = R64M_OPEN(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW, 0);
		if (fd < 0) continue;
		memset(name, 0, sizeof(name)); memset(supported, 0, sizeof(supported));
		if (R64M_IOCTL(fd, R80_EVIOCGNAME(sizeof(name)), name) >= 0 &&
		    !strcmp(name, "gpio-keys") &&
		    R64M_IOCTL(fd, R80_EVIOCGBIT(R80_EV_KEY, sizeof(supported)), supported) >= 0 &&
		    r80_input_bit(supported, R80_KEY_HOME)) break;
		(void)R64M_CLOSE(fd); fd = -1;
	}
	if (fd < 0) return -1;
	for (step = 0; step < 20U; ++step) {
		memset(pressed, 0, sizeof(pressed));
		if (R64M_IOCTL(fd, R80_EVIOCGKEY(sizeof(pressed)), pressed) < 0) {
			(void)R64M_CLOSE(fd); return -1;
		}
		if (!r80_input_bit(pressed, R80_KEY_HOME)) {
			(void)R64M_CLOSE(fd); return 0;
		}
		(void)R64M_MSLEEP(50);
	}
	(void)R64M_CLOSE(fd);
	return 1;
}

static int r80_gpio_keys_rebind(void)
{
	unsigned int step;
	if (r80_sysfs_write("/sys/bus/platform/drivers/gpio-keys/unbind",
		"gpio-keys") < 0)
		return -1;
	if (r80_sysfs_write("/sys/bus/platform/drivers/gpio-keys/bind",
		"gpio-keys") < 0)
		return -1;
	for (step = 0; step < R80_RETURN_POLL_STEPS; ++step) {
		if (r80_gpio_keys_ready())
			return 0;
		(void)R64M_MSLEEP(R80_RETURN_POLL_MS);
	}
	return -1;
}

static int r80_return_agent_start(void)
{
	char *argv[] = { "/usr/sbin/gkd-round80-return-agent", "daemon", 0 };
	char *envp[] = { "HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin",
		"TERM=linux", 0 };
	int child = R64M_FORK();
	if (child < 0) return -1;
	if (child == 0) {
		R64M_EXECVE(argv[0], argv, envp);
		exit(127);
	}
	return child;
}

static int r80_return_agent_stop(int child)
{
	unsigned int step;
	int status;
	if (child <= 0 || kill(child, SIGTERM) < 0) return -1;
	for (step = 0; step < R80_RETURN_POLL_STEPS; ++step) {
		int waited = R64M_WAITPID(child, &status, WNOHANG);
		if (waited == child) return 0;
		if (waited < 0) return -1;
		(void)R64M_MSLEEP(R80_RETURN_POLL_MS);
	}
	(void)kill(child, SIGKILL);
	return R64M_WAITPID(child, &status, 0) == child ? 0 : -1;
}

static int r64m_runtime(const struct r64m_payload *payload)
{
#if !R64M_SEALED_RECOVERY
	int return_agent = -1;
	int credentials_ready;
	int result = -1;
#elif R64M_DEDICATED_RECOVERY
	int credentials_ready;
#endif
	/* devtmpfs/proc/sysfs are permanent supervisor mounts. */
	(void)R80M_TRACE("P000");
	if (r64m_prepare_directories() < 0) return -1;
	(void)R80M_TRACE("P100");
#if R64M_SEALED_RECOVERY
	if (r84_copy_sealed_recovery(payload) < 0) {
#else
	if (r64m_copy_p1(payload) < 0) {
#endif
		(void)R80M_TRACE("8P02"); return -1;
	}
	(void)R80M_TRACE("P200");
#if R64M_SEALED_RECOVERY
	#if R64M_DEDICATED_RECOVERY
	credentials_ready = r64m_copy_p2() == 0;
	#endif
	if (r84_copy_ui_assets() < 0) {
		(void)R80M_TRACE("8P03"); return -1;
	}
#else
	credentials_ready = r64m_copy_p2() == 0;
#endif
	(void)R80M_TRACE("P300");
	/* r64m_mounts_offline emits the exact M00x source marker. */
	if (r64m_mounts_offline() < 0) return -1;
	(void)R80M_TRACE("P400");
	if (r80_swap_offline() < 0) {
		(void)R80M_TRACE("8P05"); return -1;
	}
	(void)R80M_TRACE("P500");
	if (r80_all_writable() < 0) {
		(void)R80M_TRACE("8P06"); return -1;
	}
	(void)R80M_TRACE("P600");
#if R64M_SEALED_RECOVERY
	#if R64M_DEDICATED_RECOVERY
	/* R starts as a local menu with network-only SSH when credentials are
	 * available. Raw system-card export exists only while its menu action
	 * owns the LUN. */
	if (r64m_markers(credentials_ready) < 0) {
		(void)R80M_TRACE("8P07"); return -1;
	}
	if (credentials_ready && r84_recovery_usb("network-start") < 0)
		(void)R80M_TRACE("8P0B");
	/* No earlier userspace input owner exists on a cold dedicated-R boot.  The
	 * A DEBUG return path below remains responsible for unbind/rebind. */
	if (!r80_gpio_keys_ready()) {
		(void)R80M_TRACE("8P09"); return -1;
	}
	if (r84_recovery_input_start() < 0) {
		(void)R80M_TRACE("8P0D"); return -1;
	}
	#else
	/* Keep the existing sealed A/DEBUG input reset outside dedicated R. */
	if (r80_gpio_keys_rebind() < 0) {
		(void)R80M_TRACE("8P09"); return -1;
	}
	#endif
	(void)R80M_TRACE("P700");
	if (r84_recovery_ui_enter() < 0) {
		(void)R80M_TRACE("8P08"); return -1;
	}
	return -1;
#else
	if (r64m_markers(credentials_ready) < 0) {
		(void)R80M_TRACE("8P07"); return -1;
	}
	(void)R80M_TRACE("P700");
	(void)R64M_RENDER_ACTIVE("status=active\np1_root_offline=1\nblkroget=1\nlun_readonly=1\nssh_ready=1\n");
	if ((credentials_ready ? r64m_composer_enter() :
	     r64m_mass_storage_enter()) < 0) {
		(void)R80M_TRACE("8P08"); return -1;
	}
	(void)R80M_TRACE("P800");
	/* Preserve the chooser framebuffer; only the kernel Layer1 status is used. */
	if (r80_gpio_keys_rebind() < 0) {
		(void)R80M_TRACE("8P09"); return -1;
	}
	(void)R80M_TRACE("P850");
	return_agent = r80_return_agent_start();
	if (return_agent < 0) {
		(void)R80M_TRACE("8P0A"); return -1;
	}
	(void)R80M_TRACE("P900");
	if (r64m_wait_release() < 0) {
		(void)R80M_TRACE("8P10"); goto out;
	}
	(void)R80M_TRACE("PA00");
	if (r64m_composer_teardown() < 0 || !r64m_gadget_absent()) {
		(void)R80M_TRACE("8P11"); goto out;
	}
	(void)R80M_TRACE("PB00");
	result = 0;
out:
	if (r80_return_agent_stop(return_agent) < 0) {
		(void)R80M_TRACE("8P12"); result = -1;
	}
	/* No process may retain the old evdev object across DEBUG return. */
	if (r80_gpio_keys_rebind() < 0) {
		(void)R80M_TRACE("8P13"); result = -1;
	}
	return result;
#endif
}

static __attribute__((noreturn)) void r64m_reboot_only(void)
{
	(void)R64M_REBOOT(LINUX_REBOOT_CMD_RESTART);
	for (;;) (void)R64M_MSLEEP(1000);
}

#if !R64M_DEDICATED_RECOVERY
static int r80_debug_runtime(const struct r64m_payload *payload)
{
	static const char failed[] = "status=failed\nssh_ready=0\n";
	if (r64m_runtime(payload) == 0) return 0;
	/* Failure may never intentionally retain an SSH listener or gadget. */
	(void)r64m_composer_teardown();
	if (r64m_gadget_absent()) (void)r64m_render(failed);
	return -1;
}
#endif

#endif
