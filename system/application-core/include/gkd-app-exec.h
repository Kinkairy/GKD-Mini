/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_EXEC_H
#define GKD_APP_EXEC_H
#include <stddef.h>
#include <sys/types.h>

struct gkd_app_exec_request {
    pid_t preparer_pid;
    int executable_fd;
    int terminal_fd;
    int output_fd;
    int ready_fd;
    const char *preload_path;
    const unsigned char *launch_token; /* 16 bytes, generated before fork */
    char *const *argv;
    char *const *base_env;
};

/* Final leaf of a SINGLE-THREADED launch worker's fork child, never RAM
 * supervisor or namespace PID 1. Caller prepares/validates the mounted root,
 * cwd, executable/dependency manifest and terminal first. output_fd is a
 * mandatory distinct CLOEXEC >=3, NONBLOCK write-only FIFO descriptor; the
 * controller supplies its anonymous pipe writer. No default destination.
 * The open executable
 * pins the selected ELF inode; this does not hash or freeze its contents.
 *
 * Success replaces the child. Failure returns -1/errno; caller MUST _exit,
 * never resume ordinary code after partially changed descriptors. stdin uses
 * terminal (0); stdout/stderr use output (1/2); sender (3) survives exec.
 * Descriptor 4 is a
 * CLOEXEC executable reference, all higher inherited descriptors are closed.
 * No mount, PID namespace, configuration parser or parent process is changed.
 */
int gkd_app_exec_replace(const struct gkd_app_exec_request *request);

/* Opt-in failure receipt; same child-only and immediate-_exit contract.
 * error_fd is a dedicated, connected anonymous AF_UNIX SOCK_SEQPACKET endpoint
 * >=3, NONBLOCK and CLOEXEC, distinct from all request descriptors. Caller owns
 * its peer and closes all other sender copies before waiting for EOF.
 * Valid child/request failures attempt ONE nonblocking MSG_NOSIGNAL send:
 * 8 bytes "GKDEXE1\0" followed by positive errno as 4 big-endian bytes.
 * Invalid caller/channel sends nothing and returns -1/errno without mutation.
 * The temporary sender is CLOEXEC slot 5; all slots >=6 are contained. Nothing
 * extra survives successful exec. Return errno is preserved even if send fails.
 * EOF is NOT proof of exec/readiness: exit/kill also closes this channel. Bind
 * the receipt to the exact child, monitor its pidfd, and use gkd-app-ready.
 */
#define GKD_APP_EXEC_ERROR_BYTES 12U
int gkd_app_exec_replace_report(const struct gkd_app_exec_request *request,
                                int error_fd);
/* Wire validation only; caller must authenticate kernel sender credentials.
 * Success sets *error, failure leaves it unchanged. Does not read a channel.
 */
int gkd_app_exec_error_decode(const void *packet, size_t length, int *error);
#endif
