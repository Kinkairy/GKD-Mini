/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_LIFECYCLE_H
#define GKD_APP_LIFECYCLE_H

#include <stdint.h>

enum gkd_app_lifecycle_state {
    GKD_LIFECYCLE_ACTIVE,
    GKD_LIFECYCLE_MENU_OPENING,
    GKD_LIFECYCLE_MENU,
    GKD_LIFECYCLE_TRANSITION,
    GKD_LIFECYCLE_STORAGE,
    GKD_LIFECYCLE_DEBUG,
    GKD_LIFECYCLE_WAIT_APP_READY,
    GKD_LIFECYCLE_SUSPENDED,
    GKD_LIFECYCLE_TERMINAL,
    GKD_LIFECYCLE_RECOVERY
};

enum gkd_app_lifecycle_event {
    GKD_LIFECYCLE_EVENT_MENU,
    GKD_LIFECYCLE_EVENT_CANCEL,
    GKD_LIFECYCLE_EVENT_CHARGE,
    GKD_LIFECYCLE_EVENT_STORAGE,
    GKD_LIFECYCLE_EVENT_DEBUG,
    GKD_LIFECYCLE_EVENT_SUSPEND,
    GKD_LIFECYCLE_EVENT_REBOOT,
    GKD_LIFECYCLE_EVENT_SHUTDOWN,
    GKD_LIFECYCLE_EVENT_RETURN,
    GKD_LIFECYCLE_EVENT_DETACH,
    GKD_LIFECYCLE_EVENT_RETRY,
    GKD_LIFECYCLE_EVENT_CARD_REFRESH,
    GKD_LIFECYCLE_EVENT_CARD_REMOVED,
    GKD_LIFECYCLE_EVENT_APP_READY,
    GKD_LIFECYCLE_EVENT_APP_EXIT
};

enum gkd_app_lifecycle_action {
    GKD_LIFECYCLE_ACTION_NONE,
    GKD_LIFECYCLE_ACTION_MENU_ACQUIRE,
    GKD_LIFECYCLE_ACTION_MENU_RELEASE,
    GKD_LIFECYCLE_ACTION_USB_CHARGE,
    GKD_LIFECYCLE_ACTION_DISPLAY_FREEZE,
    GKD_LIFECYCLE_ACTION_DISPLAY_THAW,
    GKD_LIFECYCLE_ACTION_APP_PAUSE,
    GKD_LIFECYCLE_ACTION_GAME_UNMOUNT,
    GKD_LIFECYCLE_ACTION_GAME_PROBE,
    GKD_LIFECYCLE_ACTION_GAME_MOUNT,
    GKD_LIFECYCLE_ACTION_APP_RESUME,
    GKD_LIFECYCLE_ACTION_GAME_IDLE_CHECK,
    GKD_LIFECYCLE_ACTION_APP_STOP,
    GKD_LIFECYCLE_ACTION_P2_RELEASE,
    GKD_LIFECYCLE_ACTION_LOOPS_RELEASE,
    GKD_LIFECYCLE_ACTION_USB_EXPORT_GAME,
    GKD_LIFECYCLE_ACTION_USB_EXPORT_SYSTEM,
    GKD_LIFECYCLE_ACTION_USB_CLEAR_LUN,
    GKD_LIFECYCLE_ACTION_USB_FLUSH,
    GKD_LIFECYCLE_ACTION_SYSTEM_MEDIA_VALIDATE,
    GKD_LIFECYCLE_ACTION_APP_START,
    /*
     * For suspend, QUIESCE pauses the application namespace and owns the
     * input/menu guard; CARD_RELEASE performs syncfs while preserving mounts.
     * For reboot/shutdown, QUIESCE cleanly stops the application host and
     * CARD_RELEASE proves the native media idle guard. The caller selects
     * those concrete semantics from the subsequent POWER_* action.
     */
    GKD_LIFECYCLE_ACTION_POWER_QUIESCE,
    GKD_LIFECYCLE_ACTION_POWER_CARD_RELEASE,
    GKD_LIFECYCLE_ACTION_POWER_SUSPEND,
    GKD_LIFECYCLE_ACTION_POWER_REBOOT,
    GKD_LIFECYCLE_ACTION_POWER_SHUTDOWN,
    /* Undo suspend quiesce and resume the same preserved application. */
    GKD_LIFECYCLE_ACTION_POWER_RESUME
};

enum gkd_app_lifecycle_media_state {
    GKD_LIFECYCLE_MEDIA_UNKNOWN = -1,
    GKD_LIFECYCLE_MEDIA_UNMOUNTED = 0,
    GKD_LIFECYCLE_MEDIA_MOUNTED = 1
};

enum gkd_app_lifecycle_usb_state {
    GKD_LIFECYCLE_USB_UNKNOWN = -1,
    GKD_LIFECYCLE_USB_NONE = 0,
    GKD_LIFECYCLE_USB_GAME,
    GKD_LIFECYCLE_USB_SYSTEM
};

struct gkd_app_lifecycle_request {
    enum gkd_app_lifecycle_action action;
    uint64_t token;
    /* Nonzero only for APP_START. The caller must bind APP_READY/APP_EXIT
     * reports to this exact generation. */
    uint64_t generation;
};

/*
 * Returning zero accepts one asynchronous operation. Returning -1 means that
 * no side effect was started; errno is recorded and the controller enters
 * RECOVERY. Completion from inside this callback is rejected as reentrant.
 *
 * MENU_ACQUIRE owns the real menu child. MENU_RELEASE remains an explicit
 * proof point; the caller may implement it as a no-op only after that child
 * has reported successful hide/clear/release and exit.
 */
typedef int (*gkd_app_lifecycle_begin_fn)(
    void *opaque, const struct gkd_app_lifecycle_request *request);

struct gkd_app_lifecycle {
    enum gkd_app_lifecycle_state state;
    enum gkd_app_lifecycle_action pending_action;
    enum gkd_app_lifecycle_action failed_action;
    enum gkd_app_lifecycle_media_state game_media;
    enum gkd_app_lifecycle_usb_state usb;
    uint64_t pending_token;
    uint64_t generation;
    uint64_t requested_generation;
    int last_error;
    unsigned menu_owned : 1;
    unsigned display_frozen : 1;
    unsigned app_running : 1;
    unsigned app_ready : 1;
    unsigned app_paused : 1;
    unsigned power_quiesced : 1;
    unsigned card_present : 1;

    gkd_app_lifecycle_begin_fn begin;
    void *opaque;
    unsigned flow, step;
    unsigned leave_requested : 1;
    unsigned waiting_ready : 1;
    unsigned dispatching : 1;
    unsigned app_exit_pending : 1;
    enum gkd_app_lifecycle_action probe_origin;
};

enum gkd_app_wait_kind { GKD_APP_WAIT_NONE, GKD_APP_WAIT_USB, GKD_APP_WAIT_POWER, GKD_APP_WAIT_APPLICATION };
enum gkd_app_wait_kind gkd_app_lifecycle_wait_kind(const struct gkd_app_lifecycle *lifecycle);

/* True only while entering/leaving USB export, including waiting for app READY. */
int gkd_app_lifecycle_usb_transition(const struct gkd_app_lifecycle *lifecycle);

int gkd_app_lifecycle_init(struct gkd_app_lifecycle *lifecycle,
    uint64_t active_generation, gkd_app_lifecycle_begin_fn begin, void *opaque);
int gkd_app_lifecycle_init_no_app(struct gkd_app_lifecycle *lifecycle,
    gkd_app_lifecycle_begin_fn begin, void *opaque);

/*
 * generation is required for APP_READY and APP_EXIT and must be zero for every
 * other event. A successful call may synchronously dispatch one begin callback.
 */
int gkd_app_lifecycle_event(struct gkd_app_lifecycle *lifecycle,
    enum gkd_app_lifecycle_event event, uint64_t generation);

/* Completes exactly the current non-probe action. */
int gkd_app_lifecycle_complete(struct gkd_app_lifecycle *lifecycle,
    uint64_t token);

/* Fails exactly the current action with a positive errno value. */
int gkd_app_lifecycle_fail(struct gkd_app_lifecycle *lifecycle,
    uint64_t token, int error);

/* GAME_PROBE has a typed completion so unknown media cannot become success. */
int gkd_app_lifecycle_probe_complete(struct gkd_app_lifecycle *lifecycle,
    uint64_t token, enum gkd_app_lifecycle_media_state state);

#endif
