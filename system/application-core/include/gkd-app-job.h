/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_JOB_H
#define GKD_APP_JOB_H
#include <stdint.h>
#include <sys/types.h>
enum gkd_app_job_state { GKD_JOB_IDLE, GKD_JOB_RUNNING, GKD_JOB_STOPPING, GKD_JOB_DONE, GKD_JOB_FAILED };
struct gkd_app_job {
    pid_t pid;
    int pidfd, outputfd, status, error, transaction;
    uint64_t deadline, last_clock;
    enum gkd_app_job_state state;
    char output[512];
    unsigned used;
};
#define GKD_APP_JOB_INIT {.pid=-1,.pidfd=-1,.outputfd=-1,.status=-1}
int gkd_app_job_start(struct gkd_app_job *,char *const argv[],unsigned timeout_ms,uint64_t now);
/* Optional borrowed descriptor becomes fd3 in the child; no other fd survives. */
int gkd_app_job_start_fd(struct gkd_app_job *,char *const argv[],unsigned timeout_ms,uint64_t now,int inherited_fd);
/* Sealed update transactions have no UI deadline and reject cancellation. */
int gkd_app_job_start_transaction(struct gkd_app_job *,char *const argv[],uint64_t now,int inherited_fd);
enum gkd_app_job_state gkd_app_job_poll(struct gkd_app_job *,uint64_t now);
int gkd_app_job_cancel(struct gkd_app_job *,uint64_t now);
int gkd_app_job_close(struct gkd_app_job *);
#endif
