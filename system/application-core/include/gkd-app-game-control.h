/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_GAME_CONTROL_H
#define GKD_APP_GAME_CONTROL_H
#include <sys/types.h>
/* Best-effort progress UI; the game lifecycle never depends on its availability. */
int gkd_app_game_wait(int active);
enum gkd_app_game_operation { GKD_GAME_EXIT=1, GKD_GAME_MENU=2 };
int gkd_app_game_accept_operation(int, unsigned long long, unsigned *operation);
int gkd_app_game_reply_operation(int, unsigned operation, int error);
int gkd_app_game_request_operation(pid_t, unsigned long long, int, unsigned operation);
int gkd_app_game_listen(unsigned long long start);
int gkd_app_game_accept(int listener, unsigned long long start);
int gkd_app_game_reply(int client, int error);
int gkd_app_game_request(pid_t pid, unsigned long long start, int pidfd);
#endif
