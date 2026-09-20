// SPDX-License-Identifier: GPL-2.0
#define _FILE_OFFSET_BITS 64
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/input.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define WIDTH 320U
#define HEIGHT 240U
#define PIXELS (WIDTH * HEIGHT)
#define PRIMARY_BYTES (PIXELS * 2U)
#define LAYER1_BYTES (PIXELS * 4U)
#define RGB_BYTES (PIXELS * 3U)
#define DPU_FRAME_CFG_ADDR 0x13050000UL
#define RAM_LIMIT 0x07f00000UL
#define FRAME_SIZE ((HEIGHT << 16) | WIDTH)
#define RGB565_CFG 0x000240ffU
#define ARGB8888_CFG 0x000540ffU
#define LAYER_ENABLE 0x00000c30U
#define SINGLE_LAYER_ENABLE 0x00000110U
#define DEFAULT_CONFIG_DIR "/etc/gkd-mini"
#define DEFAULT_CONFIG DEFAULT_CONFIG_DIR "/gdkmini.conf"
#define OVERRIDE_CONFIG_DIR "/usr/local/etc/gkd-mini"
#define OVERRIDE_CONFIG OVERRIDE_CONFIG_DIR "/gdkmini.conf"
#define CONFIG_KEY "screenshot_output_dir"
#define CONFIG_PREFIX CONFIG_KEY "="
#define CONFIG_MAX_BYTES 16384U
#define NOTIFICATION_KEY "screenshot_osd_timeout_ms"
#define NOTIFICATION_PREFIX NOTIFICATION_KEY "="
#define NOTIFICATION_DEFAULT_MS 5000U
#define HOTKEY_KEY "screenshot_hotkey"
#define HOTKEY_PREFIX HOTKEY_KEY "="
#define HOTKEY_DEFAULT "L1+L2"
#define NOTIFICATION_SYSFS "/sys/class/graphics/fb0/gkd_notification"
#define SNAPSHOT_SYSFS "/sys/class/graphics/fb0/gkd_snapshot_rgb565"
#define RUN_DIR "/run/gkd-screenshot"
#define TRACE_FILE RUN_DIR "/trace.log"
#define LOCK_DIR RUN_DIR "/capture.lock"

struct frame_desc {
	uint32_t next, size, control, writeback_addr, writeback_stride;
	uint32_t layer_cfg_addr[4], layer_cfg_enable, interrupt_control, reserved;
};

struct layer_desc {
	uint32_t size, config, buffer_addr, scale, rotation, scratch, position;
	uint32_t resize_coef_x, resize_coef_y, stride, buffer_addr_uv, stride_uv;
};

struct desc_block {
	struct frame_desc frame;
	struct layer_desc layer[2];
};

struct screenshot_hotkey {
	unsigned short first;
	unsigned short second;
	char label[16];
};

#if !defined(GKD_APPLICATION_UI)
static volatile sig_atomic_t running = 1;

static void on_signal(int signum)
{
	(void)signum;
	running = 0;
}
#endif

static int write_all(int fd, const void *buffer, size_t length)
{
	const uint8_t *p = buffer;
	while (length) {
		ssize_t done = write(fd, p, length);
		if (done < 0 && errno == EINTR)
			continue;
		if (done <= 0)
			return -1;
		p += done;
		length -= (size_t)done;
	}
	return 0;
}

static int pread_all(int fd, void *buffer, size_t length, off_t offset)
{
	uint8_t *p = buffer;
	while (length) {
		ssize_t done = pread(fd, p, length, offset);
		if (done < 0 && errno == EINTR)
			continue;
		if (done <= 0)
			return -1;
		p += done;
		offset += done;
		length -= (size_t)done;
	}
	return 0;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
	unsigned int bit;
	crc = ~crc;
	while (length--) {
		crc ^= *data++;
		for (bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
	}
	return ~crc;
}

static uint32_t adler32_bytes(const uint8_t *data, size_t length)
{
	uint32_t a = 1, b = 0;
	while (length--) {
		a = (a + *data++) % 65521U;
		b = (b + a) % 65521U;
	}
	return (b << 16) | a;
}

static void put_be32(uint8_t *out, uint32_t value)
{
	out[0] = (uint8_t)(value >> 24);
	out[1] = (uint8_t)(value >> 16);
	out[2] = (uint8_t)(value >> 8);
	out[3] = (uint8_t)value;
}

static uint8_t *append_chunk(uint8_t *out, const char type[4],
			     const uint8_t *data, uint32_t length)
{
	uint32_t crc;
	put_be32(out, length);
	out += 4;
	memcpy(out, type, 4);
	out += 4;
	if (length) {
		memcpy(out, data, length);
		out += length;
	}
	crc = crc32_update(0, (const uint8_t *)type, 4);
	crc = crc32_update(crc, data, length);
	put_be32(out, crc);
	return out + 4;
}

static int make_png(const uint8_t *rgb, uint8_t **png_out, size_t *length_out)
{
	static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
	uint8_t ihdr[13] = {0};
	size_t raw_length = (WIDTH * 3U + 1U) * HEIGHT;
	size_t blocks = (raw_length + 65534U) / 65535U;
	size_t z_length = 2U + raw_length + blocks * 5U + 4U;
	size_t png_length = z_length + 57U;
	uint8_t *raw = malloc(raw_length), *z = malloc(z_length), *png = malloc(png_length);
	uint8_t *rp, *zp, *pp;
	size_t row, remaining, amount;
	uint32_t adler;
	if (!raw || !z || !png)
		goto fail;
	rp = raw;
	for (row = 0; row < HEIGHT; ++row) {
		*rp++ = 0;
		memcpy(rp, rgb + row * WIDTH * 3U, WIDTH * 3U);
		rp += WIDTH * 3U;
	}
	zp = z;
	*zp++ = 0x78;
	*zp++ = 0x01;
	remaining = raw_length;
	rp = raw;
	while (remaining) {
		amount = remaining > 65535U ? 65535U : remaining;
		*zp++ = remaining == amount ? 1 : 0;
		*zp++ = (uint8_t)amount;
		*zp++ = (uint8_t)(amount >> 8);
		*zp++ = (uint8_t)~amount;
		*zp++ = (uint8_t)(~amount >> 8);
		memcpy(zp, rp, amount);
		zp += amount;
		rp += amount;
		remaining -= amount;
	}
	adler = adler32_bytes(raw, raw_length);
	put_be32(zp, adler);
	zp += 4;
	if ((size_t)(zp - z) != z_length)
		goto fail;
	put_be32(ihdr, WIDTH);
	put_be32(ihdr + 4, HEIGHT);
	ihdr[8] = 8;
	ihdr[9] = 2;
	pp = png;
	memcpy(pp, signature, sizeof(signature));
	pp += sizeof(signature);
	pp = append_chunk(pp, "IHDR", ihdr, sizeof(ihdr));
	pp = append_chunk(pp, "IDAT", z, (uint32_t)z_length);
	pp = append_chunk(pp, "IEND", NULL, 0);
	if ((size_t)(pp - png) != png_length)
		goto fail;
	free(z);
	free(raw);
	*png_out = png;
	*length_out = png_length;
	return 0;
fail:
	free(png);
	free(z);
	free(raw);
	return -1;
}

#if !defined(GKD_APPLICATION_UI)
static void composite(const uint8_t *primary, const uint8_t *layer1,
		      const struct layer_desc *osd, uint8_t *rgb)
{
	size_t i, x, y;
	uint32_t width = osd->size & 0xfffU, height = (osd->size >> 16) & 0xfffU;
	uint32_t position_x = osd->position & 0xfffU;
	uint32_t position_y = (osd->position >> 16) & 0xfffU;
	for (i = 0; i < PIXELS; ++i) {
		uint16_t p = (uint16_t)primary[i * 2U] |
			((uint16_t)primary[i * 2U + 1U] << 8);
		rgb[i * 3U] = (uint8_t)(((p >> 11) & 31U) * 255U / 31U);
		rgb[i * 3U + 1U] = (uint8_t)(((p >> 5) & 63U) * 255U / 63U);
		rgb[i * 3U + 2U] = (uint8_t)((p & 31U) * 255U / 31U);
	}
	for (y = 0; y < height; ++y) {
		for (x = 0; x < width; ++x) {
			size_t source = (y * osd->stride + x) * 4U;
			size_t target = ((position_y + y) * WIDTH + position_x + x) * 3U;
			uint8_t b = layer1[source], g = layer1[source + 1U];
			uint8_t r = layer1[source + 2U], a = layer1[source + 3U];
			rgb[target] = (uint8_t)((r * a + rgb[target] * (255U - a) + 127U) / 255U);
			rgb[target + 1U] = (uint8_t)((g * a + rgb[target + 1U] * (255U - a) + 127U) / 255U);
			rgb[target + 2U] = (uint8_t)((b * a + rgb[target + 2U] * (255U - a) + 127U) / 255U);
		}
	}
}

static int read_mmio_u32(int fd, off_t address, uint32_t *value)
{
	long page_size = sysconf(_SC_PAGESIZE);
	off_t page, within;
	void *mapping;
	if (page_size <= 0)
		return -1;
	page = address & ~((off_t)page_size - 1);
	within = address - page;
	mapping = mmap(NULL, (size_t)page_size, PROT_READ, MAP_SHARED, fd, page);
	if (mapping == MAP_FAILED)
		return -1;
	*value = *(volatile uint32_t *)((uint8_t *)mapping + within);
	munmap(mapping, (size_t)page_size);
	return 0;
}

static int valid_range(uint32_t address, size_t length)
{
	return !(address & 3U) && address >= 0x00100000U &&
		address < RAM_LIMIT && length <= RAM_LIMIT - address;
}

static int validate_desc(const struct desc_block *desc)
{
	const struct layer_desc *primary = &desc->layer[0], *osd = &desc->layer[1];
	uint32_t width = osd->size & 0xfffU, height = (osd->size >> 16) & 0xfffU;
	uint32_t position_x = osd->position & 0xfffU;
	uint32_t position_y = (osd->position >> 16) & 0xfffU;
	size_t osd_bytes;
	if (desc->frame.size != FRAME_SIZE ||
	    (desc->frame.layer_cfg_enable != LAYER_ENABLE &&
	     desc->frame.layer_cfg_enable != SINGLE_LAYER_ENABLE))
		return -1;
	if (primary->size != FRAME_SIZE || primary->config != RGB565_CFG ||
	    primary->stride != WIDTH || primary->position != 0 ||
	    !valid_range(primary->buffer_addr, PRIMARY_BYTES))
		return -1;
	if (desc->frame.layer_cfg_enable == SINGLE_LAYER_ENABLE) {
		if (desc->frame.layer_cfg_addr[1] || osd->size || osd->config ||
		    osd->buffer_addr || osd->position || osd->stride)
			return -1;
		return 0;
	}
	if (!width || !height || width > WIDTH || height > HEIGHT ||
	    osd->size != (height << 16 | width) ||
	    position_x > WIDTH - width || position_y > HEIGHT - height ||
	    osd->position != (position_y << 16 | position_x) ||
	    osd->config != ARGB8888_CFG || osd->stride != WIDTH)
		return -1;
	osd_bytes = ((size_t)(height - 1U) * osd->stride + width) * 4U;
	if (osd_bytes > LAYER1_BYTES || !valid_range(osd->buffer_addr, osd_bytes))
		return -1;
	return 0;
}

static void trace(const char *status, const char *detail)
{
	char line[512];
	struct stat st;
	int fd, length;
	(void)mkdir(RUN_DIR, 0700);
	if (lstat(RUN_DIR, &st) || !S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))
		return;
	(void)chmod(RUN_DIR, 0700);
	fd = open(TRACE_FILE, O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW, 0600);
	if (fd < 0)
		return;
	if (!fstat(fd, &st) && S_ISREG(st.st_mode)) {
		if (st.st_size > 65536 && ftruncate(fd, 0)) {
			close(fd);
			return;
		}
		length = snprintf(line, sizeof(line),
			"event=screenshot status=%s detail=%s\n", status, detail);
		if (length > 0 && length < (int)sizeof(line))
			(void)write_all(fd, line, (size_t)length);
	}
	close(fd);
}

static int ensure_dir(const char *path, mode_t mode)
{
	struct stat st;
	if (mkdir(path, mode) && errno != EEXIST)
		return -1;
	if (lstat(path, &st) || !S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))
		return -1;
	return 0;
}

static int valid_output_dir(const char *path)
{
	const unsigned char *p = (const unsigned char *)path;
	size_t length = strlen(path);
	if (length < 8 || length > 240 || strncmp(path, "/media/", 7) ||
	    strstr(path, "//") || strstr(path, "/../") || strstr(path, "/./") ||
	    (length >= 3 && !strcmp(path + length - 3, "/..")) ||
	    (length >= 2 && !strcmp(path + length - 2, "/.")))
		return 0;
	while (*p) {
		if (*p < 0x20 || *p == 0x7f || *p == '\\')
			return 0;
		++p;
	}
	return 1;
}

static const char *select_config(void)
{
	return access(OVERRIDE_CONFIG, R_OK) == 0 ? OVERRIDE_CONFIG : DEFAULT_CONFIG;
}

static const char *config_dir(const char *config_file)
{
	return !strcmp(config_file, OVERRIDE_CONFIG) ?
		OVERRIDE_CONFIG_DIR : DEFAULT_CONFIG_DIR;
}

static int read_config(const char *config_file, char *config, size_t capacity,
		       size_t *length_out, struct stat *stat_out)
{
	struct stat st;
	size_t total = 0;
	int fd = open(config_file, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != 0 ||
	    st.st_size <= 0 || st.st_size >= (off_t)capacity) {
		if (fd >= 0)
			close(fd);
		return -1;
	}
	while (total < (size_t)st.st_size) {
		ssize_t got = read(fd, config + total, (size_t)st.st_size - total);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0) {
			close(fd);
			return -1;
		}
		total += (size_t)got;
	}
	close(fd);
	if (memchr(config, '\0', total))
		return -1;
	config[total] = '\0';
	*length_out = total;
	if (stat_out)
		*stat_out = st;
	return 0;
}

static int find_output_setting(const char *config, size_t length,
			       size_t *line_start_out,
			       size_t *line_end_out,
			       size_t *value_start_out,
			       size_t *value_length_out)
{
	static const char prefix[] = CONFIG_PREFIX;
	size_t position = 0, matches = 0;
	while (position < length) {
		size_t line_start = position, line_end, content_end;
		while (position < length && config[position] != '\n')
			++position;
		line_end = position;
		content_end = line_end;
		if (content_end > line_start && config[content_end - 1U] == '\r')
			--content_end;
		if (content_end - line_start >= sizeof(prefix) - 1U &&
		    !memcmp(config + line_start, prefix, sizeof(prefix) - 1U)) {
			++matches;
			if (matches == 1U) {
				*line_start_out = line_start;
				*line_end_out = content_end;
				*value_start_out = line_start + sizeof(prefix) - 1U;
				*value_length_out = content_end - *value_start_out;
			}
		}
		if (position < length)
			++position;
	}
	return matches == 1U ? 0 : -1;
}

static int load_output_dir(char *path, size_t path_size)
{
	char config[CONFIG_MAX_BYTES + 1U];
	const char *config_file = select_config();
	size_t length, line_start, line_end, value_start, value_length;
	if (read_config(config_file, config, sizeof(config), &length, NULL) ||
	    find_output_setting(config, length, &line_start, &line_end,
				&value_start, &value_length) ||
	    value_length >= path_size)
		return -1;
	(void)line_start;
	(void)line_end;
	memcpy(path, config + value_start, value_length);
	path[value_length] = '\0';
	if (!valid_output_dir(path))
		return -1;
	return 0;
}

static unsigned int load_notification_timeout(void)
{
	static const char prefix[] = NOTIFICATION_PREFIX;
	char config[CONFIG_MAX_BYTES + 1U];
	size_t length, position = 0, matches = 0;
	unsigned int value = NOTIFICATION_DEFAULT_MS;
	if (read_config(select_config(), config, sizeof(config), &length, NULL))
		return value;
	while (position < length) {
		size_t start = position, end;
		char number[16], *tail;
		unsigned long parsed;
		while (position < length && config[position] != '\n')
			++position;
		end = position;
		if (end > start && config[end - 1U] == '\r')
			--end;
		if (end - start >= sizeof(prefix) - 1U &&
		    !memcmp(config + start, prefix, sizeof(prefix) - 1U)) {
			size_t bytes = end - start - (sizeof(prefix) - 1U);
			++matches;
			if (bytes == 0 || bytes >= sizeof(number))
				return NOTIFICATION_DEFAULT_MS;
			memcpy(number, config + start + sizeof(prefix) - 1U, bytes);
			number[bytes] = '\0';
			errno = 0;
			parsed = strtoul(number, &tail, 10);
			if (errno || *tail ||
			    (parsed != 0UL && parsed < 200UL) || parsed > 5000UL)
				return NOTIFICATION_DEFAULT_MS;
			value = (unsigned int)parsed;
		}
		if (position < length)
			++position;
	}
	return matches == 1U ? value : NOTIFICATION_DEFAULT_MS;
}

static int parse_hotkey_name(const char *name, size_t length,
			     unsigned short *code)
{
	if (length == 2U && !memcmp(name, "L1", 2U))
		*code = KEY_TAB;
	else if (length == 2U && !memcmp(name, "R1", 2U))
		*code = KEY_BACKSPACE;
	else if (length == 2U && !memcmp(name, "L2", 2U))
		*code = KEY_PAGEUP;
	else if (length == 2U && !memcmp(name, "R2", 2U))
		*code = KEY_PAGEDOWN;
	else
		return -1;
	return 0;
}

static int parse_hotkey_value(const char *value, size_t length,
			      struct screenshot_hotkey *hotkey)
{
	const char *plus = memchr(value, '+', length);
	size_t first_length, second_length;
	if (length == 4U && !memcmp(value, "NONE", 4U)) {
		hotkey->first = hotkey->second = 0;
		memcpy(hotkey->label, "NONE", 5U);
		return 0;
	}
	if (!plus || memchr(plus + 1, '+', length - (size_t)(plus + 1 - value)))
		return -1;
	first_length = (size_t)(plus - value);
	second_length = length - first_length - 1U;
	if (!first_length || !second_length ||
	    parse_hotkey_name(value, first_length, &hotkey->first) ||
	    parse_hotkey_name(plus + 1, second_length, &hotkey->second) ||
	    hotkey->first == hotkey->second || length >= sizeof(hotkey->label))
		return -1;
	memcpy(hotkey->label, value, length);
	hotkey->label[length] = '\0';
	return 0;
}

static int load_hotkey(struct screenshot_hotkey *hotkey)
{
	static const char prefix[] = HOTKEY_PREFIX;
	char config[CONFIG_MAX_BYTES + 1U];
	size_t length, position = 0, matches = 0;
	if (read_config(select_config(), config, sizeof(config), &length, NULL))
		return parse_hotkey_value(HOTKEY_DEFAULT,
			sizeof(HOTKEY_DEFAULT) - 1U, hotkey);
	while (position < length) {
		size_t start = position, end;
		while (position < length && config[position] != '\n')
			++position;
		end = position;
		if (end > start && config[end - 1U] == '\r')
			--end;
		if (end - start >= sizeof(prefix) - 1U &&
		    !memcmp(config + start, prefix, sizeof(prefix) - 1U)) {
			size_t value_length = end - start - (sizeof(prefix) - 1U);
			++matches;
			if (matches != 1U ||
			    parse_hotkey_value(config + start + sizeof(prefix) - 1U,
				value_length, hotkey))
				return -1;
		}
		if (position < length)
			++position;
	}
	if (matches == 0U)
		return parse_hotkey_value(HOTKEY_DEFAULT,
			sizeof(HOTKEY_DEFAULT) - 1U, hotkey);
	return 0;
}

static void publish_notification(int success, unsigned int timeout_ms)
{
	char payload[32];
	int fd, length;
	if (!timeout_ms)
		return;
	fd = open(NOTIFICATION_SYSFS,
		  O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return;
	length = snprintf(payload, sizeof(payload), "%s %u\n",
		success ? "saved" : "failed", timeout_ms);
	if (length > 0 && length < (int)sizeof(payload))
		(void)write_all(fd, payload, (size_t)length);
	close(fd);
}

static int rewrite_output_setting(const char *config, size_t length,
				  const char *path, char **updated_out,
				  size_t *updated_length_out)
{
	static const char prefix[] = CONFIG_PREFIX;
	size_t line_start, line_end, value_start, value_length;
	size_t replacement_length = sizeof(prefix) - 1U + strlen(path);
	size_t updated_length;
	char *updated;
	if (!valid_output_dir(path) ||
	    find_output_setting(config, length, &line_start, &line_end,
				&value_start, &value_length))
		return -1;
	(void)value_start;
	(void)value_length;
	updated_length = length - (line_end - line_start) + replacement_length;
	if (updated_length > CONFIG_MAX_BYTES)
		return -1;
	updated = malloc(updated_length + 1U);
	if (!updated)
		return -1;
	memcpy(updated, config, line_start);
	memcpy(updated + line_start, prefix, sizeof(prefix) - 1U);
	memcpy(updated + line_start + sizeof(prefix) - 1U, path, strlen(path));
	memcpy(updated + line_start + replacement_length, config + line_end,
	       length - line_end);
	updated[updated_length] = '\0';
	*updated_out = updated;
	*updated_length_out = updated_length;
	return 0;
}

static int save_output_dir(const char *path)
{
	char temporary[128], config[CONFIG_MAX_BYTES + 1U];
	char *updated = NULL;
	const char *config_file = select_config();
	const char *directory_path = config_dir(config_file);
	struct stat st;
	size_t length, updated_length;
	int fd = -1, directory = -1, result = -1;
	if (getuid() != 0 || !valid_output_dir(path) || ensure_dir(path, 0755))
		return -1;
	if (read_config(config_file, config, sizeof(config), &length, &st) ||
	    rewrite_output_setting(config, length, path, &updated, &updated_length) ||
	    snprintf(temporary, sizeof(temporary),
		     "%s/.gdkmini.conf.new.%ld", directory_path,
		     (long)getpid()) >= (int)sizeof(temporary))
		goto out;
	fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
	if (fd < 0 || fchmod(fd, st.st_mode & 0777) ||
	    write_all(fd, updated, updated_length) || fsync(fd) || close(fd))
		goto out;
	fd = -1;
	if (rename(temporary, config_file))
		goto out;
	directory = open(directory_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (directory >= 0)
		(void)fsync(directory);
	result = 0;
out:
	if (fd >= 0)
		close(fd);
	if (directory >= 0)
		close(directory);
	if (result)
		unlink(temporary);
	free(updated);
	return result;
}

static int create_png_file(const char *directory, char *path, size_t path_size)
{
	time_t now = time(NULL);
	struct tm tm_value;
	char stamp[32];
	unsigned int suffix;
	int fd;
	if (ensure_dir(directory, 0755) || !localtime_r(&now, &tm_value) ||
	    !strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm_value))
		return -1;
	for (suffix = 0; suffix < 1000; ++suffix) {
		if (snprintf(path, path_size, "%s/GKD-%s-%03u.png", directory,
			     stamp, suffix) >= (int)path_size)
			return -1;
		fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
		if (fd >= 0)
			return fd;
		if (errno != EEXIST)
			return -1;
	}
	return -1;
}

static int capture(void)
{
	struct desc_block first, second;
	uint8_t *primary = NULL, *layer1 = NULL, *rgb = NULL, *png_data = NULL;
	size_t png_length = 0, osd_length;
	uint32_t desc_address;
	char directory[256], output[512];
	int mem = -1, png = -1, result = 1, attempt, locked = 0;
	unsigned int notification_timeout = load_notification_timeout();
	int final_snapshot = 0;
	if (ensure_dir(RUN_DIR, 0700) || mkdir(LOCK_DIR, 0700)) {
		trace("degraded", "capture-busy");
		publish_notification(0, notification_timeout);
		return 1;
	}
	locked = 1;
	if (load_output_dir(directory, sizeof(directory))) {
		trace("failed", "invalid-output-config");
		goto out;
	}
	primary = malloc(PRIMARY_BYTES);
	layer1 = malloc(LAYER1_BYTES);
	rgb = malloc(RGB_BYTES);
	if (!primary || !layer1 || !rgb)
		goto out;
	mem = open(SNAPSHOT_SYSFS, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (mem >= 0) {
		memset(&first, 0, sizeof(first));
		if (pread_all(mem, primary, PRIMARY_BYTES, 0))
			goto out;
		final_snapshot = 1;
	} else {
		mem = open("/dev/mem", O_RDONLY | O_CLOEXEC);
		if (mem < 0 || read_mmio_u32(mem, DPU_FRAME_CFG_ADDR, &desc_address) ||
		    (desc_address & 15U) || !valid_range(desc_address, sizeof(first)))
			goto out;
	}
	if (!final_snapshot) {
		for (attempt = 0; attempt < 3; ++attempt) {
			if (pread_all(mem, &first, sizeof(first), desc_address) ||
			    validate_desc(&first) ||
			    pread_all(mem, primary, PRIMARY_BYTES,
				      first.layer[0].buffer_addr))
				goto out;
			if (first.frame.layer_cfg_enable == LAYER_ENABLE) {
				osd_length = (((size_t)((first.layer[1].size >> 16) &
					0xfffU) - 1U) * first.layer[1].stride +
					(first.layer[1].size & 0xfffU)) * 4U;
				if (pread_all(mem, layer1, osd_length,
					      first.layer[1].buffer_addr))
					goto out;
			}
			if (pread_all(mem, &second, sizeof(second), desc_address))
				goto out;
			if (!memcmp(&first, &second, sizeof(first)))
				break;
		}
		if (attempt == 3)
			goto out;
	}
	composite(primary, layer1, &first.layer[1], rgb);
	if (make_png(rgb, &png_data, &png_length))
		goto out;
	png = create_png_file(directory, output, sizeof(output));
	if (png < 0 || write_all(png, png_data, png_length) || fsync(png) || close(png))
		goto out;
	png = -1;
	trace("ok", output);
	puts(output);
	result = 0;
out:
	if (result) {
		if (png >= 0) {
			close(png);
			unlink(output);
		}
		trace("failed", "capture");
	}
	if (mem >= 0)
		close(mem);
	free(png_data);
	free(rgb);
	free(layer1);
	free(primary);
	if (locked)
		rmdir(LOCK_DIR);
	publish_notification(result == 0, notification_timeout);
	return result;
}

static int find_input(void)
{
	char path[64], name[128];
	int index, fd;
	for (index = 0; index < 16; ++index) {
		snprintf(path, sizeof(path), "/dev/input/event%d", index);
		fd = open(path, O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			continue;
		memset(name, 0, sizeof(name));
		if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0 &&
		    !strcmp(name, "gpio-keys"))
			return fd;
		close(fd);
	}
	return -1;
}

static int daemon_loop(void)
{
	struct input_event events[8];
	struct screenshot_hotkey hotkey;
	struct sigaction action;
	char detail[32];
	int fd = -1, first_down = 0, second_down = 0, latched = 0;
	memset(&action, 0, sizeof(action));
	action.sa_handler = on_signal;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGTERM, &action, NULL) || sigaction(SIGINT, &action, NULL))
		return 1;
	if (load_hotkey(&hotkey)) {
		trace("failed", "invalid-hotkey-config");
		return 1;
	}
	(void)snprintf(detail, sizeof(detail), "hotkey=%s", hotkey.label);
	if (!hotkey.first && !hotkey.second) {
		trace("disabled", detail);
		return 0;
	}
	trace("started", detail);
	while (running) {
		ssize_t got;
		size_t index, count;
		if (fd < 0) {
			fd = find_input();
			if (fd < 0) {
				sleep(1);
				continue;
			}
			first_down = second_down = latched = 0;
		}
		got = read(fd, events, sizeof(events));
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0 || got % (ssize_t)sizeof(events[0])) {
			close(fd);
			fd = -1;
			continue;
		}
		count = (size_t)got / sizeof(events[0]);
		for (index = 0; index < count; ++index) {
			if (events[index].type != EV_KEY)
				continue;
			if (events[index].code == hotkey.first)
				first_down = events[index].value != 0;
			else if (events[index].code == hotkey.second)
				second_down = events[index].value != 0;
			if (!first_down || !second_down)
				latched = 0;
			if (first_down && second_down && !latched) {
				latched = 1;
				(void)capture();
			}
		}
	}
	if (fd >= 0)
		close(fd);
	trace("stopped", "signal");
	return 0;
}

static int self_test(void)
{
	struct desc_block desc;
	struct screenshot_hotkey hotkey;
	static const char sample[] =
		"# note\nbrightness_steps=2,10,100\n"
		CONFIG_PREFIX "/media/data/screenshots\nvolume_step=5\n";
	static const char expected[] =
		"# note\nbrightness_steps=2,10,100\n"
		CONFIG_PREFIX "/media/roms/screenshots\nvolume_step=5\n";
	char *updated = NULL;
	size_t updated_length = 0;
	uint8_t red565[2] = {0x00, 0xf8}, blue_argb[4] = {0xff, 0x00, 0x00, 0xff};
	uint16_t p = (uint16_t)red565[0] | ((uint16_t)red565[1] << 8);
	uint8_t br = (uint8_t)(((p >> 11) & 31U) * 255U / 31U);
	uint8_t a = blue_argb[3];
	uint8_t out_r = (uint8_t)((blue_argb[2] * a + br * (255U - a) + 127U) / 255U);
	if (sizeof(struct frame_desc) != 48 || sizeof(struct layer_desc) != 48 ||
	    sizeof(struct desc_block) != 144 || out_r != 0 ||
	    parse_hotkey_value("L1+L2", 5U, &hotkey) ||
	    hotkey.first != KEY_TAB || hotkey.second != KEY_PAGEUP ||
	    strcmp(hotkey.label, "L1+L2") ||
	    parse_hotkey_value("NONE", 4U, &hotkey) || hotkey.first || hotkey.second ||
	    strcmp(hotkey.label, "NONE") ||
	    parse_hotkey_value("R1+R2", 5U, &hotkey) ||
	    hotkey.first != KEY_BACKSPACE || hotkey.second != KEY_PAGEDOWN ||
	    !parse_hotkey_value("L1+L1", 5U, &hotkey) ||
	    !parse_hotkey_value("L1+X2", 5U, &hotkey) ||
	    !parse_hotkey_value("L1++L2", 6U, &hotkey) ||
	    crc32_update(0, (const uint8_t *)"123456789", 9) != 0xcbf43926U ||
	    adler32_bytes((const uint8_t *)"Wikipedia", 9) != 0x11e60398U)
		return 1;
	memset(&desc, 0, sizeof(desc));
	desc.frame.size = FRAME_SIZE;
	desc.frame.layer_cfg_enable = LAYER_ENABLE;
	desc.layer[0].size = FRAME_SIZE;
	desc.layer[0].config = RGB565_CFG;
	desc.layer[0].buffer_addr = 0x01000000;
	desc.layer[0].stride = WIDTH;
	desc.layer[1].size = FRAME_SIZE;
	desc.layer[1].config = ARGB8888_CFG;
	desc.layer[1].buffer_addr = 0x01100000;
	desc.layer[1].stride = WIDTH;
	if (validate_desc(&desc) ||
	    rewrite_output_setting(sample, sizeof(sample) - 1U,
				   "/media/roms/screenshots", &updated,
				   &updated_length) ||
	    updated_length != sizeof(expected) - 1U ||
	    memcmp(updated, expected, sizeof(expected) - 1U)) {
		free(updated);
		return 1;
	}
	free(updated);
	puts("GKD_SCREENSHOT_SELF_TEST=PASS hotkey=config-default-L1+L2 grab=none storage=local");
	return 0;
}
#endif

#if defined(GKD_APPLICATION_UI)
#define APPLICATION_CARD_ROOT "/media/sdcard"
#define APPLICATION_CARD_DEVICE "/dev/mmcblk1p1"

static int application_root_valid(int root_fd, dev_t *device_out)
{
	struct stat root, block;

	if (fstat(root_fd, &root) || !S_ISDIR(root.st_mode) ||
	    stat(APPLICATION_CARD_DEVICE, &block) || !S_ISBLK(block.st_mode) ||
	    root.st_dev != block.st_rdev) {
		errno = EXDEV;
		return -1;
	}
	*device_out = root.st_dev;
	return 0;
}

static int application_relative_path(const char *path, const char **relative_out)
{
	static const char prefix[] = APPLICATION_CARD_ROOT "/";
	const unsigned char *p;
	const char *component;

	if (strncmp(path, prefix, sizeof(prefix) - 1U) ||
	    !path[sizeof(prefix) - 1U]) {
		errno = EINVAL;
		return -1;
	}
	component = path + sizeof(prefix) - 1U;
	p = (const unsigned char *)component;
	while (*p) {
		const unsigned char *start = p;

		while (*p && *p != '/') {
			if (*p < 0x20U || *p == 0x7fU || *p == '\\') {
				errno = EINVAL;
				return -1;
			}
			++p;
		}
		if (p == start || (p - start == 1 && start[0] == '.') ||
		    (p - start == 2 && start[0] == '.' && start[1] == '.') ||
		    p - start > 255) {
			errno = EINVAL;
			return -1;
		}
		if (*p && !*++p) {
			errno = EINVAL;
			return -1;
		}
	}
	*relative_out = component;
	return 0;
}

static int application_open_directory(int root_fd, dev_t device,
				      const char *relative)
{
	char path[512], *component, *separator;
	int current, next;

	if (strlen(relative) >= sizeof(path)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	memcpy(path, relative, strlen(relative) + 1U);
	current = fcntl(root_fd, F_DUPFD, 4);
	if (current < 0)
		return -1;
	if (fcntl(current, F_SETFD, FD_CLOEXEC))
		goto fail;
	component = path;
	for (;;) {
		struct stat directory;

		separator = strchr(component, '/');
		if (separator)
			*separator = '\0';
		if (mkdirat(current, component, 0755) && errno != EEXIST)
			goto fail;
		next = openat(current, component,
			      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
		if (next < 0)
			goto fail;
		if (fstat(next, &directory) || !S_ISDIR(directory.st_mode) ||
		    directory.st_dev != device) {
			close(next);
			errno = EXDEV;
			goto fail;
		}
		close(current);
		current = next;
		if (!separator)
			return current;
		component = separator + 1;
	}

fail:
	{
		int saved = errno;

		close(current);
		errno = saved;
		return -1;
	}
}

static void application_rgb565_to_rgb(const uint8_t *primary, uint8_t *rgb)
{
	size_t i;

	for (i = 0; i < PIXELS; ++i) {
		uint16_t pixel = (uint16_t)primary[i * 2U] |
			((uint16_t)primary[i * 2U + 1U] << 8);

		rgb[i * 3U] = (uint8_t)(((pixel >> 11) & 31U) * 255U / 31U);
		rgb[i * 3U + 1U] =
			(uint8_t)(((pixel >> 5) & 63U) * 255U / 63U);
		rgb[i * 3U + 2U] = (uint8_t)((pixel & 31U) * 255U / 31U);
	}
}

static int application_create_png(int directory_fd, char *filename,
				  size_t filename_size)
{
	time_t now = time(NULL);
	struct tm tm_value;
	char stamp[32];
	unsigned int suffix;

	if (now == (time_t)-1 || !localtime_r(&now, &tm_value) ||
	    !strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm_value)) {
		errno = EIO;
		return -1;
	}
	for (suffix = 0; suffix < 1000U; ++suffix) {
		int fd;

		if (snprintf(filename, filename_size, "GKD-%s-%03u.png",
			     stamp, suffix) >= (int)filename_size) {
			errno = ENAMETOOLONG;
			return -1;
		}
		fd = openat(directory_fd, filename,
			    O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
			    0644);
		if (fd >= 0)
			return fd;
		if (errno != EEXIST)
			return -1;
	}
	errno = EEXIST;
	return -1;
}

static int application_capture(int root_fd, const char *output_directory)
{
	uint8_t *primary = NULL, *rgb = NULL, *png_data = NULL;
	size_t png_length = 0;
	const char *relative;
	char filename[64];
	dev_t device;
	int snapshot = -1, directory = -1, png = -1, result = 1, saved;

	if (application_root_valid(root_fd, &device) ||
	    application_relative_path(output_directory, &relative))
		goto out;
	directory = application_open_directory(root_fd, device, relative);
	if (directory < 0)
		goto out;
	primary = malloc(PRIMARY_BYTES);
	rgb = malloc(RGB_BYTES);
	if (!primary || !rgb) {
		errno = ENOMEM;
		goto out;
	}
	snapshot = open(SNAPSHOT_SYSFS, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (snapshot < 0)
		goto out;
	errno = 0;
	if (pread_all(snapshot, primary, PRIMARY_BYTES, 0)) {
		if (!errno)
			errno = EIO;
		goto out;
	}
	/* Notify only after the image is captured: progress must never enter the PNG. */
	if (printf("GKD_SCREENSHOT_CAPTURED\n") < 0 || fflush(stdout)) {
		errno = EIO;
		goto out;
	}
	application_rgb565_to_rgb(primary, rgb);
	if (make_png(rgb, &png_data, &png_length)) {
		errno = ENOMEM;
		goto out;
	}
	png = application_create_png(directory, filename, sizeof(filename));
	if (png < 0 || write_all(png, png_data, png_length) || fsync(png) ||
	    close(png))
		goto out;
	png = -1;
	if (fsync(directory))
		goto out;
	if (printf("GKD_SCREENSHOT_RESULT=success filename=%s/%s\n",
		   output_directory, filename) < 0 || fflush(stdout)) {
		errno = EIO;
		goto out;
	}
	result = 0;
out:
	saved = errno ? errno : EIO;
	if (png >= 0) {
		close(png);
		if (directory >= 0)
			(void)unlinkat(directory, filename, 0);
	}
	if (snapshot >= 0)
		close(snapshot);
	if (directory >= 0)
		close(directory);
	free(png_data);
	free(rgb);
	free(primary);
	if (result) {
		(void)printf("GKD_SCREENSHOT_RESULT=failure error=%d\n", saved);
		errno = saved;
	}
	return result;
}
#endif

int main(int argc, char **argv)
{
#if defined(GKD_APPLICATION_UI)
	if (argc == 4 && !strcmp(argv[1], "--application-capture") &&
	    !strcmp(argv[2], "3"))
		return application_capture(3, argv[3]);
	fputs("usage: gkd-screenshot --application-capture 3 /media/sdcard/DIRECTORY\n",
	      stderr);
	return 2;
#else
	char path[256];
	if (argc == 2 && !strcmp(argv[1], "--self-test"))
		return self_test();
	if (argc == 2 && !strcmp(argv[1], "daemon"))
		return daemon_loop();
	if (argc == 2 && !strcmp(argv[1], "get-path")) {
		if (load_output_dir(path, sizeof(path)))
			return 1;
		puts(path);
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "set-path"))
		return save_output_dir(argv[2]) ? 1 : 0;
	if (argc == 1 || (argc == 2 && !strcmp(argv[1], "capture")))
		return capture();
	fprintf(stderr, "usage: %s [capture|daemon|get-path|set-path PATH|--self-test]\n", argv[0]);
	return 2;
#endif
}
