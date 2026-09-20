/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_CREDENTIALS_H
#define GKD_APP_CREDENTIALS_H
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Shared strict ancillary policy for event and preparation receipts.
 * Close every rejected delivered FD, including truncated messages.
 * Linux closes rights that did not fit the receiver's control buffer.
 */
/* wanted is exact, not a maximum. On any refusal close all delivered rights.
 * Existing event channels request zero; preparation requests exactly one.
 */
static inline int gkd_app_receive_credentials(struct msghdr *message, pid_t pid,
                                              uid_t uid, int *rights, size_t wanted)
{
    struct cmsghdr *item;
    int found = 0, invalid = 0;
    size_t count = 0;
    for (size_t i = 0; i < wanted; ++i) rights[i] = -1;
    for (item = CMSG_FIRSTHDR(message); item; item = CMSG_NXTHDR(message, item)) {
        if (item->cmsg_level == SOL_SOCKET && item->cmsg_type == SCM_RIGHTS) {
            size_t bytes = item->cmsg_len >= CMSG_LEN(0) ? item->cmsg_len - CMSG_LEN(0) : 0;
            if (!wanted || !bytes) invalid = 1;
            for (size_t i = 0; i + sizeof(int) <= bytes; i += sizeof(int)) {
                int received;
                memcpy(&received, (unsigned char *)CMSG_DATA(item) + i, sizeof(received));
                if (count < wanted) rights[count] = received;
                else { (void)close(received); invalid = 1; }
                ++count;
            }
            if (bytes % sizeof(int)) invalid = 1;
        } else if (item->cmsg_level == SOL_SOCKET && item->cmsg_type == SCM_CREDENTIALS &&
                   item->cmsg_len == CMSG_LEN(sizeof(struct ucred))) {
            struct ucred peer;
            memcpy(&peer, CMSG_DATA(item), sizeof(peer));
            if (++found != 1 || peer.pid != pid || peer.uid != uid) invalid = 1;
        } else invalid = 1;
    }
    if (found == 1 && !invalid && count == wanted &&
        !(message->msg_flags & (MSG_TRUNC | MSG_CTRUNC))) return 1;
    for (size_t i = 0; i < wanted; ++i)
        if (rights[i] >= 0) { (void)close(rights[i]); rights[i] = -1; }
    return 0;
}
static inline int gkd_app_check_credentials(struct msghdr *message, pid_t pid, uid_t uid)
{
    return gkd_app_receive_credentials(message, pid, uid, NULL, 0);
}
#endif
