#!/usr/bin/env python3
import argparse
from pathlib import Path

INCLUDE = '#include "round83-ram-payload.generated.h"'
COMMON_CODE = r'''
static int gkdu_request_present(void)
{
	static const unsigned char magic[8] = {'G','K','D','R','Q','1',0,0};
	unsigned char actual[8];
	int fd = open("/dev/mmcblk0", O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
	int result = 0;
	if (fd >= 0 && R64M_LSEEK(fd, (off_t)20967424ULL, SEEK_SET) ==
	    (off_t)20967424ULL && R64M_READ(fd, actual, sizeof(actual)) ==
	    (long)sizeof(actual) && !memcmp(actual, magic, sizeof(actual))) result = 1;
	if (fd >= 0) (void)close(fd);
	return result;
}

static int gkdu_mount_game_card(void)
{
	unsigned int step;
	(void)mkdir("/media", 0755); (void)mkdir("/media/sdcard", 0755);
	for (step = 0; step < 50U; ++step) {
		if (mount("/dev/mmcblk1p1", "/media/sdcard", "vfat",
		    MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, "utf8") == 0)
			return 0;
		if (errno == EBUSY) return 0;
		(void)msleep(100);
	}
	return -1;
}

static int gkdu_update_boot(void)
{
	char *args[] = {"/usr/sbin/gkd-update-coordinator",
		"/media/sdcard/gkd-update/system.gkdupdate", "/dev/mmcblk0p3",
		"/dev/mmcblk0p1", "/dev/mmcblk0", "/usr/sbin/gkd-update-engine", 0};
	char *envp[] = {"HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin", 0};
	int status = 0, game_ready;
	pid_t child;
	game_ready = gkdu_mount_game_card() == 0;
	child = fork();
	if (child < 0) return -1;
	if (!child) { execve(args[0], args, envp); exit(127); }
	if (waitpid(child, &status, 0) != child) return -1;
	if (game_ready && umount2("/media/sdcard", 0) < 0) return -1;
	if (status == 0) return 0;
	if (status == (10 << 8) || status == (20 << 8)) {
		(void)r80i_trace("UPTR");
		(void)my_syscall0(__NR_sync);
		(void)reboot(LINUX_REBOOT_CMD_RESTART);
		for (;;) (void)msleep(1000);
	}
	return -1;
}
'''

NORMAL_HOLD_CODE = r'''
static __attribute__((noreturn)) void gkdu_recovery_hold(const char marker[4])
{
	(void)r80i_trace(marker);
	report("GKD independent RAM recovery entered\n");
	if (r80_debug_runtime(&r64m_final_payload) == 0) {
		(void)my_syscall0(__NR_sync);
		(void)reboot(LINUX_REBOOT_CMD_RESTART);
		for (;;) (void)msleep(1000);
	}
	(void)r80i_trace("8RCV");
	report("GKD RAM recovery service failed; holding instead of rebooting\n");
	for (;;) (void)msleep(1000);
}
'''

DEDICATED_HOLD_CODE = r'''
static __attribute__((noreturn)) void gkdu_recovery_hold(const char marker[4])
{
	(void)r80i_trace(marker);
	report("GKD independent RAM recovery entered\n");
	r_boot_begin();
	if (r64m_runtime(&r64m_final_payload) == 0) {
		(void)my_syscall0(__NR_sync);
		(void)reboot(LINUX_REBOOT_CMD_RESTART);
		for (;;) (void)msleep(1000);
	}
	{
		unsigned failed_stage = r_boot_stage;
		if (!r_boot_mapping) r_boot_begin();
		r_boot_present(failed_stage, 1);
		r_boot_close();
	}
	report("GKD RAM recovery service failed; preserving stage marker and holding\n");
	for (;;) (void)msleep(1000);
}
'''

parser = argparse.ArgumentParser()
parser.add_argument("--source", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--dedicated-recovery", action="store_true")
arguments = parser.parse_args()
text = arguments.source.read_text(encoding="utf-8")
if (arguments.output.exists() or text.count(INCLUDE) != 1 or
        text.count("int main(int argc, char **argv, char **envp)") != 1):
    raise SystemExit("GKDSU_SUPERVISOR=BLOCKED")
injected = COMMON_CODE + (
    DEDICATED_HOLD_CODE if arguments.dedicated_recovery else NORMAL_HOLD_CODE
)
text = text.replace(
    "int main(int argc, char **argv, char **envp)",
    injected + "\nint main(int argc, char **argv, char **envp)", 1,
)
anchor = "\tround39_framebuffer_ready = framebuffer_rgb565_selftest() == 0;\n"
if text.count(anchor) != 1:
    raise SystemExit("GKDSU_SUPERVISOR=BLOCKED anchor")
if arguments.dedicated_recovery:
    trace_anchor = '#define R80M_TRACE(marker) r80i_trace(marker)'
    if text.count(trace_anchor) != 1:
        raise SystemExit("GKDSU_SUPERVISOR=BLOCKED boot-trace-anchor")
    text = text.replace(trace_anchor,
                        'static int r_boot_trace(const char marker[4]);\n#define R80M_TRACE(marker) r_boot_trace(marker)', 1)
    text = text.replace(INCLUDE, '#include "dedicated-r-boot.h"\n' + INCLUDE, 1)
    replacement = anchor + "\tgkdu_recovery_hold(\"RCVR\");\n"
else:
    replacement = anchor + (
        "\tif (r80_recovery_menu_held() > 0)\n"
        "\t\tgkdu_recovery_hold(\"RCVM\");\n"
        "\tif (gkdu_request_present() && gkdu_update_boot() < 0)\n"
        "\t\tgkdu_recovery_hold(\"RCVU\");\n"
    )
text = text.replace(anchor, replacement, 1)
arguments.output.write_text(text, encoding="utf-8")
if arguments.dedicated_recovery:
    print("GKDSU_SUPERVISOR=PASS recovery_priority=dedicated-r")
else:
    print("GKDSU_SUPERVISOR=PASS recovery_priority=menu-update-p1-debug")
