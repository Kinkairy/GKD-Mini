/* Round64 request-slot gate shared verbatim by production PID1 and fixtures. */
#ifndef GKD_ROUND64_REQUEST_GATE_H
#define GKD_ROUND64_REQUEST_GATE_H

#ifndef R64_OPEN
#define R64_OPEN open
#endif
#ifndef R64_CLOSE
#define R64_CLOSE close
#endif
#ifndef R64_LSEEK
#define R64_LSEEK lseek
#endif
#ifndef R64_READ
#define R64_READ read
#endif
#ifndef R64_WRITE
#define R64_WRITE write
#endif
#ifndef R64_IOCTL
#define R64_IOCTL ioctl
#endif
#ifndef R64_FSYNC
#define R64_FSYNC fsync
#endif

#define R64_SLOT ((off_t)0x13ff000)
#define R64_SLOT_BYTES 4096
#define R64_P1_START_SECTORS 40960U
#define R64_DISK_BYTES 31457280000ULL
#define R64_O_SYNC 0x4010
#define R64_MAGIC "GKD_MINI_ROUND64_RAM_RESCUE_V1\n"

/* Exact bytes 446..509 from the accepted physical MBR
 * (whole-MBR SHA-256 a1dfed84445c9a6bd1eecced9e8af0af97a932fac29766cbe78aea0a62f7b95e).
 * Python tests parse the marked initializer; there is no second model. */
static const unsigned char r64_mbr_partition_bytes[64] = {
/* R64_MBR_64_BEGIN */
	0x80,0x8c,0x0b,0x02,0x83,0x74,0x11,0x64,0x00,0xa0,0x00,0x00,0x01,0x00,0x18,0x00,
	0x00,0x94,0x31,0x64,0x83,0xea,0xbd,0x6d,0x00,0xa8,0x18,0x00,0x00,0xd8,0x70,0x03,
	0x00,0xea,0xbe,0x6d,0x82,0x76,0x86,0xf0,0x00,0x80,0x89,0x03,0x00,0x00,0x20,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
/* R64_MBR_64_END */
};

static int r64_zero(const unsigned char *p)
{
	unsigned long i;
	for (i = 0; i < R64_SLOT_BYTES; i++)
		if (p[i])
			return 0;
	return 1;
}

static int r64_blob(unsigned char *p)
{
	const char m[] = R64_MAGIC;
	const char e[] = "\nGKD_MINI_ROUND64_RAM_RESCUE_V1_END\n";
	memset(p, 0, R64_SLOT_BYTES);
	memcpy(p, m, sizeof(m) - 1);
	memcpy(p + R64_SLOT_BYTES - sizeof(e), e, sizeof(e) - 1);
	return 0;
}

static unsigned int r64_le32(const unsigned char *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
	       ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static int r64_geometry(int fd)
{
	unsigned char m[512];
	unsigned long long bytes = 0;
	if (R64_IOCTL(fd, BLKGETSIZE64, &bytes) < 0 || bytes != R64_DISK_BYTES)
		return -1;
	if (R64_LSEEK(fd, 0, SEEK_SET) != 0 ||
	    R64_READ(fd, m, sizeof(m)) != (long)sizeof(m))
		return -1;
	if (r64_le32(m + 440) != 0x0007130cU || m[444] != 0 || m[445] != 0 ||
	    memcmp(m + 446, r64_mbr_partition_bytes,
	           sizeof(r64_mbr_partition_bytes)) != 0 ||
	    m[510] != 0x55 || m[511] != 0xaa)
		return -1;
	return R64_SLOT + R64_SLOT_BYTES ==
	       (off_t)R64_P1_START_SECTORS * 512 ? 0 : -1;
}

static int r64_slot_read_once(int fd, unsigned char *p)
{
	if (R64_LSEEK(fd, R64_SLOT, SEEK_SET) != R64_SLOT)
		return -1;
	return R64_READ(fd, p, R64_SLOT_BYTES) == R64_SLOT_BYTES ? 0 : -1;
}

static int r64_request(void)
{
	unsigned char actual[R64_SLOT_BYTES], expected[R64_SLOT_BYTES];
	int fd = R64_OPEN("/dev/mmcblk0", O_RDONLY, 0);
	int ok = 0;
	if (fd < 0)
		return 0;
	if (r64_slot_read_once(fd, actual) == 0) {
		r64_blob(expected);
		if (!memcmp(actual, expected, sizeof(actual)) && r64_geometry(fd) == 0)
			ok = 1;
	}
	R64_CLOSE(fd);
	return ok;
}

static int r64_clear(void)
{
	unsigned char actual[R64_SLOT_BYTES], expected[R64_SLOT_BYTES];
	unsigned char zero[R64_SLOT_BYTES];
	int fd = R64_OPEN("/dev/mmcblk0", O_RDWR | R64_O_SYNC, 0);
	if (fd < 0)
		return -1;
	r64_blob(expected);
	memset(zero, 0, sizeof(zero));
	if (r64_geometry(fd) || r64_slot_read_once(fd, actual) ||
	    memcmp(actual, expected, sizeof(actual)) ||
	    R64_LSEEK(fd, R64_SLOT, SEEK_SET) != R64_SLOT ||
	    R64_WRITE(fd, zero, sizeof(zero)) != (long)sizeof(zero) ||
	    R64_FSYNC(fd) || r64_slot_read_once(fd, actual) || !r64_zero(actual)) {
		R64_CLOSE(fd);
		return -1;
	}
	return R64_CLOSE(fd);
}

#endif
