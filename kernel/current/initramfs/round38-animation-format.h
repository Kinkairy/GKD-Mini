#ifndef GKD_ROUND38_ANIMATION_FORMAT_H
#define GKD_ROUND38_ANIMATION_FORMAT_H

/* Shared by the nolibc PID1 and the native host contract harness. */
#define GKD_ROUND38_ANIMATION_PATH \
	"/newroot/boot/gkd-mini/startup-animation.rgb565"
#define GKD_ROUND38_HEADER_BYTES 32UL
#define GKD_ROUND38_WIDTH 320U
#define GKD_ROUND38_HEIGHT 240U
#define GKD_ROUND38_FRAME_COUNT 31U
#define GKD_ROUND38_FRAME_MILLISECONDS 100U
#define GKD_ROUND38_FRAME_BYTES \
	(GKD_ROUND38_WIDTH * GKD_ROUND38_HEIGHT * 2UL)
#define GKD_ROUND38_PAYLOAD_BYTES \
	(GKD_ROUND38_FRAME_COUNT * GKD_ROUND38_FRAME_BYTES)
#define GKD_ROUND38_TOTAL_BYTES \
	(GKD_ROUND38_HEADER_BYTES + GKD_ROUND38_PAYLOAD_BYTES)

typedef long (*gkd_round38_read_callback)(void *context,
					 unsigned char *buffer,
					 unsigned long count);

static __inline__ unsigned int gkd_round38_le16(const unsigned char *value)
{
	return value[0] | ((unsigned int)value[1] << 8);
}

static __inline__ unsigned long gkd_round38_le32(const unsigned char *value)
{
	return value[0] | ((unsigned long)value[1] << 8) |
		((unsigned long)value[2] << 16) |
		((unsigned long)value[3] << 24);
}

/* Accumulate legal positive short reads; EOF, errors, and over-reads fail. */
static __inline__ int gkd_round38_read_exact(
		gkd_round38_read_callback reader, void *context,
		unsigned char *buffer, unsigned long count)
{
	long received;

	while (count > 0) {
		received = reader(context, buffer, count);
		if (received <= 0 || (unsigned long)received > count)
			return -1;
		buffer += received;
		count -= (unsigned long)received;
	}
	return 0;
}

static __inline__ int gkd_round38_header_valid(
		const unsigned char header[GKD_ROUND38_HEADER_BYTES],
		unsigned long total_bytes)
{
	static const unsigned char magic[8] = {
		'G', 'K', 'D', 'A', 'N', 'I', 'M', '1',
	};
	static const unsigned char pixel_format[8] = {
		'R', 'G', 'B', '5', '6', '5', 'L', 'E',
	};
	unsigned int index;

	if (total_bytes != GKD_ROUND38_TOTAL_BYTES)
		return 0;
	for (index = 0; index < 8; index++) {
		if (header[index] != magic[index] ||
		    header[16 + index] != pixel_format[index])
			return 0;
	}
	if (gkd_round38_le16(header + 8) != GKD_ROUND38_WIDTH ||
	    gkd_round38_le16(header + 10) != GKD_ROUND38_HEIGHT ||
	    gkd_round38_le16(header + 12) != GKD_ROUND38_FRAME_COUNT ||
	    gkd_round38_le16(header + 14) !=
		GKD_ROUND38_FRAME_MILLISECONDS ||
	    gkd_round38_le32(header + 24) != GKD_ROUND38_PAYLOAD_BYTES ||
	    gkd_round38_le32(header + 28) != 0)
		return 0;
	return 1;
}

#endif
