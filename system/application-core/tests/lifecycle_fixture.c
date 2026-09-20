/* SPDX-License-Identifier: GPL-2.0 */
#include "gkd-app-lifecycle.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_REQUESTS 128U

struct fixture {
    struct gkd_app_lifecycle lifecycle;
    struct gkd_app_lifecycle_request requests[MAX_REQUESTS];
    unsigned count;
    enum gkd_app_lifecycle_action reject_action;
    unsigned reject_count;
    int reenter;
};

static int begin_action(void *opaque,
    const struct gkd_app_lifecycle_request *request)
{
    struct fixture *fixture = opaque;

    assert(fixture);
    assert(request);
    assert(request->action != GKD_LIFECYCLE_ACTION_NONE);
    assert(request->token);
    assert(fixture->count < MAX_REQUESTS);
    if (fixture->count)
        assert(request->token >
            fixture->requests[fixture->count - 1U].token);
    fixture->requests[fixture->count++] = *request;
    if (fixture->reenter) {
        fixture->reenter = 0;
        assert(gkd_app_lifecycle_complete(&fixture->lifecycle,
            request->token) < 0);
        assert(errno == EBUSY);
    }
    if (request->action == fixture->reject_action &&
        fixture->reject_count) {
        fixture->reject_count--;
        errno = EIO;
        return -1;
    }
    return 0;
}

static void setup(struct fixture *fixture, uint64_t generation)
{
    memset(fixture, 0, sizeof(*fixture));
    assert(!gkd_app_lifecycle_init(&fixture->lifecycle,
        generation, begin_action, fixture));
    assert(fixture->lifecycle.state == GKD_LIFECYCLE_ACTIVE);
    assert(fixture->lifecycle.app_running);
    assert(fixture->lifecycle.app_ready);
    assert(fixture->lifecycle.game_media ==
        GKD_LIFECYCLE_MEDIA_MOUNTED);
}

static struct gkd_app_lifecycle_request *pending(struct fixture *fixture,
    enum gkd_app_lifecycle_action action)
{
    assert(fixture->count);
    assert(fixture->lifecycle.pending_action == action);
    assert(fixture->requests[fixture->count - 1U].action == action);
    assert(fixture->requests[fixture->count - 1U].token ==
        fixture->lifecycle.pending_token);
    return &fixture->requests[fixture->count - 1U];
}

static void complete_action(struct fixture *fixture,
    enum gkd_app_lifecycle_action action)
{
    uint64_t token = pending(fixture, action)->token;
    assert(!gkd_app_lifecycle_complete(&fixture->lifecycle, token));
}

static void fail_action(struct fixture *fixture,
    enum gkd_app_lifecycle_action action, int error)
{
    uint64_t token = pending(fixture, action)->token;
    assert(!gkd_app_lifecycle_fail(&fixture->lifecycle, token, error));
    assert(fixture->lifecycle.state == GKD_LIFECYCLE_RECOVERY);
    assert(fixture->lifecycle.failed_action == action);
    assert(fixture->lifecycle.last_error == error);
    assert(fixture->lifecycle.pending_action ==
        GKD_LIFECYCLE_ACTION_NONE);
}

static void probe_action(struct fixture *fixture,
    enum gkd_app_lifecycle_media_state state)
{
    uint64_t token =
        pending(fixture, GKD_LIFECYCLE_ACTION_GAME_PROBE)->token;
    assert(!gkd_app_lifecycle_probe_complete(
        &fixture->lifecycle, token, state));
}

static void open_menu(struct fixture *fixture)
{
    assert(!gkd_app_lifecycle_event(&fixture->lifecycle,
        GKD_LIFECYCLE_EVENT_MENU, 0));
    assert(fixture->lifecycle.state == GKD_LIFECYCLE_MENU_OPENING);
    complete_action(fixture, GKD_LIFECYCLE_ACTION_MENU_ACQUIRE);
    assert(fixture->lifecycle.state == GKD_LIFECYCLE_MENU);
    assert(fixture->lifecycle.menu_owned);
}

static void test_storage_roundtrip(void)
{
    struct fixture f;
    static const enum gkd_app_lifecycle_action order[] = {
        GKD_LIFECYCLE_ACTION_MENU_ACQUIRE,
        GKD_LIFECYCLE_ACTION_MENU_RELEASE,
        GKD_LIFECYCLE_ACTION_APP_PAUSE,
        GKD_LIFECYCLE_ACTION_GAME_UNMOUNT,
        GKD_LIFECYCLE_ACTION_USB_EXPORT_GAME,
        GKD_LIFECYCLE_ACTION_USB_CLEAR_LUN,
        GKD_LIFECYCLE_ACTION_USB_FLUSH,
        GKD_LIFECYCLE_ACTION_GAME_MOUNT,
        GKD_LIFECYCLE_ACTION_APP_RESUME
    };
    unsigned i;

    setup(&f, 7U);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_MENU, 0));
    assert(gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_MENU, 0) < 0 && errno == EBUSY);
    assert(gkd_app_lifecycle_complete(&f.lifecycle,
        f.lifecycle.pending_token + 1U) < 0 && errno == ESTALE);
    complete_action(&f, GKD_LIFECYCLE_ACTION_MENU_ACQUIRE);
    assert(gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_MENU, 0) < 0 && errno == EALREADY);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_STORAGE, 0));
    complete_action(&f, GKD_LIFECYCLE_ACTION_MENU_RELEASE);
    complete_action(&f, GKD_LIFECYCLE_ACTION_APP_PAUSE);
    complete_action(&f, GKD_LIFECYCLE_ACTION_GAME_UNMOUNT);
    complete_action(&f, GKD_LIFECYCLE_ACTION_USB_EXPORT_GAME);
    assert(f.lifecycle.state == GKD_LIFECYCLE_STORAGE);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_RETURN, 0));
    complete_action(&f, GKD_LIFECYCLE_ACTION_USB_CLEAR_LUN);
    complete_action(&f, GKD_LIFECYCLE_ACTION_USB_FLUSH);
    complete_action(&f, GKD_LIFECYCLE_ACTION_GAME_MOUNT);
    complete_action(&f, GKD_LIFECYCLE_ACTION_APP_RESUME);
    assert(f.lifecycle.state == GKD_LIFECYCLE_ACTIVE);
    assert(f.lifecycle.usb == GKD_LIFECYCLE_USB_NONE);
    assert(f.lifecycle.game_media == GKD_LIFECYCLE_MEDIA_MOUNTED);
    assert(!f.lifecycle.app_paused);
    assert(f.count == sizeof(order) / sizeof(order[0]));
    for (i = 0; i < f.count; i++)
        assert(f.requests[i].action == order[i]);
}

static void test_cancel_and_charge(void)
{
    struct fixture f;

    setup(&f, 1U);
    open_menu(&f);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_CANCEL, 0));
    complete_action(&f, GKD_LIFECYCLE_ACTION_MENU_RELEASE);
    assert(f.lifecycle.state == GKD_LIFECYCLE_ACTIVE);
    assert(f.count == 2U);

    open_menu(&f);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_CHARGE, 0));
    complete_action(&f, GKD_LIFECYCLE_ACTION_MENU_RELEASE);
    complete_action(&f, GKD_LIFECYCLE_ACTION_USB_CHARGE);
    assert(f.lifecycle.state == GKD_LIFECYCLE_ACTIVE);
}

static void test_cancel_during_menu_open(void)
{
    struct fixture f;

    setup(&f, 2U);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_MENU, 0));
    pending(&f, GKD_LIFECYCLE_ACTION_MENU_ACQUIRE);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_DETACH, 0));
    assert(f.lifecycle.pending_action ==
        GKD_LIFECYCLE_ACTION_MENU_ACQUIRE);
    complete_action(&f, GKD_LIFECYCLE_ACTION_MENU_ACQUIRE);
    complete_action(&f, GKD_LIFECYCLE_ACTION_MENU_RELEASE);
    assert(f.lifecycle.state == GKD_LIFECYCLE_ACTIVE);
    assert(f.count == 2U);
}

static void test_detach_inflight(void)
{
    struct fixture f;

    setup(&f, 2U);
    open_menu(&f);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_STORAGE, 0));
    complete_action(&f, GKD_LIFECYCLE_ACTION_MENU_RELEASE);
    complete_action(&f, GKD_LIFECYCLE_ACTION_APP_PAUSE);
    pending(&f, GKD_LIFECYCLE_ACTION_GAME_UNMOUNT);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_DETACH, 0));
    complete_action(&f, GKD_LIFECYCLE_ACTION_GAME_UNMOUNT);
    pending(&f, GKD_LIFECYCLE_ACTION_USB_CLEAR_LUN);
    complete_action(&f, GKD_LIFECYCLE_ACTION_USB_CLEAR_LUN);
    complete_action(&f, GKD_LIFECYCLE_ACTION_USB_FLUSH);
    complete_action(&f, GKD_LIFECYCLE_ACTION_GAME_MOUNT);
    complete_action(&f, GKD_LIFECYCLE_ACTION_APP_RESUME);
    assert(f.lifecycle.state == GKD_LIFECYCLE_ACTIVE);
    for (unsigned i = 0; i < f.count; i++)
        assert(f.requests[i].action !=
            GKD_LIFECYCLE_ACTION_USB_EXPORT_GAME);
}

static void test_media_failure_recovery(void)
{
    struct fixture f;
    unsigned before;
    uint64_t stale;

    setup(&f, 3U);
    open_menu(&f);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_STORAGE, 0));
    complete_action(&f, GKD_LIFECYCLE_ACTION_MENU_RELEASE);
    complete_action(&f, GKD_LIFECYCLE_ACTION_APP_PAUSE);
    stale = pending(&f, GKD_LIFECYCLE_ACTION_GAME_UNMOUNT)->token;
    fail_action(&f, GKD_LIFECYCLE_ACTION_GAME_UNMOUNT, EIO);
    assert(f.lifecycle.game_media == GKD_LIFECYCLE_MEDIA_UNKNOWN);
    before = f.count;
    assert(gkd_app_lifecycle_complete(&f.lifecycle, stale) < 0 &&
        errno == ESTALE);
    assert(f.count == before);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_RETRY, 0));
    fail_action(&f, GKD_LIFECYCLE_ACTION_GAME_PROBE, EIO);
    assert(f.count == before + 1U);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_RETRY, 0));
    probe_action(&f, GKD_LIFECYCLE_MEDIA_UNMOUNTED);
    complete_action(&f, GKD_LIFECYCLE_ACTION_USB_EXPORT_GAME);
    assert(f.lifecycle.state == GKD_LIFECYCLE_STORAGE);

    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_RETURN, 0));
    complete_action(&f, GKD_LIFECYCLE_ACTION_USB_CLEAR_LUN);
    complete_action(&f, GKD_LIFECYCLE_ACTION_USB_FLUSH);
    complete_action(&f, GKD_LIFECYCLE_ACTION_GAME_MOUNT);
    fail_action(&f, GKD_LIFECYCLE_ACTION_APP_RESUME, EIO);
    before = f.count;
    assert(gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_MENU, 0) < 0 && errno == EBUSY);
    assert(f.count == before);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_RETRY, 0));
    assert(f.count == before + 1U);
    pending(&f, GKD_LIFECYCLE_ACTION_APP_RESUME);
    complete_action(&f, GKD_LIFECYCLE_ACTION_APP_RESUME);
    assert(f.lifecycle.state == GKD_LIFECYCLE_ACTIVE);
}

static void test_exit_during_storage_operation(void)
{
    struct fixture f;
    uint64_t token, next;

    setup(&f, 10U);
    open_menu(&f);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_STORAGE, 0));
    complete_action(&f, GKD_LIFECYCLE_ACTION_MENU_RELEASE);
    complete_action(&f, GKD_LIFECYCLE_ACTION_APP_PAUSE);
    token = pending(&f, GKD_LIFECYCLE_ACTION_GAME_UNMOUNT)->token;
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_APP_EXIT, 10U));
    assert(f.lifecycle.pending_action ==
        GKD_LIFECYCLE_ACTION_GAME_UNMOUNT);
    assert(f.lifecycle.pending_token == token);
    assert(!gkd_app_lifecycle_complete(&f.lifecycle, token));
    assert(f.lifecycle.state == GKD_LIFECYCLE_RECOVERY);
    assert(f.lifecycle.pending_action ==
        GKD_LIFECYCLE_ACTION_NONE);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_RETRY, 0));
    complete_action(&f, GKD_LIFECYCLE_ACTION_USB_CLEAR_LUN);
    complete_action(&f, GKD_LIFECYCLE_ACTION_USB_FLUSH);
    complete_action(&f, GKD_LIFECYCLE_ACTION_GAME_MOUNT);
    next = pending(&f, GKD_LIFECYCLE_ACTION_APP_START)->generation;
    complete_action(&f, GKD_LIFECYCLE_ACTION_APP_START);
    assert(f.lifecycle.state == GKD_LIFECYCLE_WAIT_APP_READY);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_APP_READY, next));
    assert(f.lifecycle.state == GKD_LIFECYCLE_ACTIVE);
}

static void enter_debug(struct fixture *fixture)
{
    open_menu(fixture);
    assert(!gkd_app_lifecycle_event(&fixture->lifecycle,
        GKD_LIFECYCLE_EVENT_DEBUG, 0));
    complete_action(fixture, GKD_LIFECYCLE_ACTION_MENU_RELEASE);
    complete_action(fixture, GKD_LIFECYCLE_ACTION_DISPLAY_FREEZE);
    complete_action(fixture, GKD_LIFECYCLE_ACTION_APP_STOP);
    complete_action(fixture, GKD_LIFECYCLE_ACTION_P2_RELEASE);
    complete_action(fixture, GKD_LIFECYCLE_ACTION_LOOPS_RELEASE);
    complete_action(fixture, GKD_LIFECYCLE_ACTION_USB_EXPORT_SYSTEM);
    assert(fixture->lifecycle.state == GKD_LIFECYCLE_DEBUG);
}

static void test_debug_roundtrip(void)
{
    struct fixture f;
    uint64_t old_generation, new_generation;

    setup(&f, 11U);
    old_generation = f.lifecycle.generation;
    enter_debug(&f);
    assert(!f.lifecycle.app_running);
    assert(f.lifecycle.display_frozen);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_RETURN, 0));
    complete_action(&f, GKD_LIFECYCLE_ACTION_USB_CLEAR_LUN);
    complete_action(&f, GKD_LIFECYCLE_ACTION_USB_FLUSH);
    complete_action(&f, GKD_LIFECYCLE_ACTION_SYSTEM_MEDIA_VALIDATE);
    new_generation =
        pending(&f, GKD_LIFECYCLE_ACTION_APP_START)->generation;
    assert(new_generation != old_generation);
    complete_action(&f, GKD_LIFECYCLE_ACTION_APP_START);
    assert(f.lifecycle.state == GKD_LIFECYCLE_WAIT_APP_READY);
    assert(f.lifecycle.pending_action == GKD_LIFECYCLE_ACTION_NONE);
    assert(gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_APP_READY, old_generation) < 0 &&
        errno == ESTALE);
    assert(gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_APP_EXIT, old_generation) < 0 &&
        errno == ESTALE);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_APP_READY, new_generation));
    complete_action(&f, GKD_LIFECYCLE_ACTION_DISPLAY_THAW);
    assert(f.lifecycle.state == GKD_LIFECYCLE_ACTIVE);
    assert(f.lifecycle.app_running && f.lifecycle.app_ready);
    assert(!f.lifecycle.display_frozen);
    assert(gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_APP_READY, old_generation) < 0 &&
        errno == ESTALE);
}

static void test_failed_stop_never_exports(void)
{
    struct fixture f;
    unsigned before;

    setup(&f, 12U);
    open_menu(&f);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_DEBUG, 0));
    complete_action(&f, GKD_LIFECYCLE_ACTION_MENU_RELEASE);
    complete_action(&f, GKD_LIFECYCLE_ACTION_DISPLAY_FREEZE);
    before = f.count;
    fail_action(&f, GKD_LIFECYCLE_ACTION_APP_STOP, EBUSY);
    assert(f.count == before);
    assert(f.lifecycle.app_running);
    for (unsigned i = 0; i < f.count; i++)
        assert(f.requests[i].action !=
            GKD_LIFECYCLE_ACTION_USB_EXPORT_SYSTEM);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_RETRY, 0));
    complete_action(&f, GKD_LIFECYCLE_ACTION_APP_STOP);
    pending(&f, GKD_LIFECYCLE_ACTION_P2_RELEASE);
}

static void test_unexpected_exit_requires_retry(void)
{
    struct fixture f;
    unsigned before;
    uint64_t next;

    setup(&f, 20U);
    before = f.count;
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_APP_EXIT, 20U));
    assert(f.lifecycle.state == GKD_LIFECYCLE_RECOVERY);
    assert(f.count == before);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_RETRY, 0));
    next = pending(&f, GKD_LIFECYCLE_ACTION_APP_START)->generation;
    assert(next == 21U);
    complete_action(&f, GKD_LIFECYCLE_ACTION_APP_START);
    assert(f.lifecycle.state == GKD_LIFECYCLE_WAIT_APP_READY);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_APP_READY, next));
    assert(f.lifecycle.state == GKD_LIFECYCLE_ACTIVE);
}

static void test_power_order_and_resume(void)
{
    struct fixture f;

    setup(&f, 30U);
    open_menu(&f);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_SUSPEND, 0));
    complete_action(&f, GKD_LIFECYCLE_ACTION_MENU_RELEASE);
    complete_action(&f, GKD_LIFECYCLE_ACTION_POWER_QUIESCE);
    complete_action(&f, GKD_LIFECYCLE_ACTION_POWER_CARD_RELEASE);
    complete_action(&f, GKD_LIFECYCLE_ACTION_POWER_SUSPEND);
    assert(f.lifecycle.state == GKD_LIFECYCLE_SUSPENDED);
    assert(f.lifecycle.app_running && f.lifecycle.power_quiesced);
    assert(f.lifecycle.pending_action == GKD_LIFECYCLE_ACTION_NONE);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_RETURN, 0));
    complete_action(&f, GKD_LIFECYCLE_ACTION_POWER_RESUME);
    assert(f.lifecycle.state == GKD_LIFECYCLE_ACTIVE);
    assert(!f.lifecycle.power_quiesced);

    open_menu(&f);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_REBOOT, 0));
    complete_action(&f, GKD_LIFECYCLE_ACTION_MENU_RELEASE);
    complete_action(&f, GKD_LIFECYCLE_ACTION_POWER_QUIESCE);
    fail_action(&f, GKD_LIFECYCLE_ACTION_POWER_CARD_RELEASE, EIO);
    assert(f.lifecycle.pending_action == GKD_LIFECYCLE_ACTION_NONE);
    assert(f.lifecycle.state == GKD_LIFECYCLE_RECOVERY);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_RETRY, 0));
    complete_action(&f, GKD_LIFECYCLE_ACTION_POWER_CARD_RELEASE);
    complete_action(&f, GKD_LIFECYCLE_ACTION_POWER_REBOOT);
    assert(!f.lifecycle.app_running);
    assert(f.lifecycle.state == GKD_LIFECYCLE_TERMINAL);
}

static void test_power_stop_exit_is_expected(void)
{
    struct fixture f;
    uint64_t token;

    setup(&f, 31U);
    open_menu(&f);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_SHUTDOWN, 0));
    complete_action(&f, GKD_LIFECYCLE_ACTION_MENU_RELEASE);
    token = pending(&f,
        GKD_LIFECYCLE_ACTION_POWER_QUIESCE)->token;
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_APP_EXIT, 31U));
    assert(f.lifecycle.pending_token == token);
    assert(f.lifecycle.state == GKD_LIFECYCLE_TRANSITION);
    assert(!gkd_app_lifecycle_complete(&f.lifecycle, token));
    complete_action(&f, GKD_LIFECYCLE_ACTION_POWER_CARD_RELEASE);
    complete_action(&f, GKD_LIFECYCLE_ACTION_POWER_SHUTDOWN);
    assert(f.lifecycle.state == GKD_LIFECYCLE_TERMINAL);
}

static void test_autosuspend_without_menu(void)
{
    struct fixture f;

    setup(&f, 31U);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_SUSPEND, 0));
    assert(f.count == 1U);
    pending(&f, GKD_LIFECYCLE_ACTION_POWER_QUIESCE);
    complete_action(&f, GKD_LIFECYCLE_ACTION_POWER_QUIESCE);
    complete_action(&f, GKD_LIFECYCLE_ACTION_POWER_CARD_RELEASE);
    complete_action(&f, GKD_LIFECYCLE_ACTION_POWER_SUSPEND);
    assert(f.lifecycle.state == GKD_LIFECYCLE_SUSPENDED);
    assert(f.lifecycle.app_running);
}

static void test_update_reboot_without_menu(void)
{
    struct fixture f;
    setup(&f, 32U);
    assert(!gkd_app_lifecycle_event(&f.lifecycle, GKD_LIFECYCLE_EVENT_REBOOT, 0));
    assert(f.count == 1U && !f.lifecycle.menu_owned);
    complete_action(&f, GKD_LIFECYCLE_ACTION_POWER_QUIESCE);
    complete_action(&f, GKD_LIFECYCLE_ACTION_POWER_CARD_RELEASE);
    complete_action(&f, GKD_LIFECYCLE_ACTION_POWER_REBOOT);
    assert(f.lifecycle.state == GKD_LIFECYCLE_TERMINAL && !f.lifecycle.app_running);
}

static void test_dispatch_failure_and_reentrancy(void)
{
    struct fixture f;

    setup(&f, 40U);
    f.reject_action = GKD_LIFECYCLE_ACTION_MENU_ACQUIRE;
    f.reject_count = 1U;
    assert(gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_MENU, 0) < 0 && errno == EIO);
    assert(f.lifecycle.state == GKD_LIFECYCLE_RECOVERY);
    assert(f.lifecycle.failed_action ==
        GKD_LIFECYCLE_ACTION_MENU_ACQUIRE);
    f.reenter = 1;
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_RETRY, 0));
    complete_action(&f, GKD_LIFECYCLE_ACTION_MENU_ACQUIRE);
    assert(f.lifecycle.state == GKD_LIFECYCLE_MENU);
}

static void test_invalid_api(void)
{
    struct fixture f;

    errno = 0;
    assert(gkd_app_lifecycle_init(NULL, 1U, begin_action, NULL) < 0 &&
        errno == EINVAL);
    setup(&f, 50U);
    assert(gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_MENU, 1U) < 0 && errno == EINVAL);
    assert(gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_APP_READY, 0) < 0 && errno == EINVAL);
    assert(gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_CANCEL, 0) < 0 && errno == EINVAL);
    assert(!gkd_app_lifecycle_event(&f.lifecycle,
        GKD_LIFECYCLE_EVENT_MENU, 0));
    assert(gkd_app_lifecycle_probe_complete(&f.lifecycle,
        f.lifecycle.pending_token, GKD_LIFECYCLE_MEDIA_MOUNTED) < 0 &&
        errno == EPROTO);
    complete_action(&f, GKD_LIFECYCLE_ACTION_MENU_ACQUIRE);
}

static void test_card_refresh(void)
{
 struct fixture f;setup(&f,60U);
 assert(!gkd_app_lifecycle_event(&f.lifecycle,GKD_LIFECYCLE_EVENT_CARD_REFRESH,0));
 complete_action(&f,GKD_LIFECYCLE_ACTION_DISPLAY_FREEZE);
 fail_action(&f,GKD_LIFECYCLE_ACTION_APP_STOP,EIO);
 assert(f.lifecycle.app_running&&f.lifecycle.usb==GKD_LIFECYCLE_USB_NONE);
 assert(!gkd_app_lifecycle_event(&f.lifecycle,GKD_LIFECYCLE_EVENT_RETRY,0));
 complete_action(&f,GKD_LIFECYCLE_ACTION_APP_STOP);
 complete_action(&f,GKD_LIFECYCLE_ACTION_P2_RELEASE);
 complete_action(&f,GKD_LIFECYCLE_ACTION_LOOPS_RELEASE);
 complete_action(&f,GKD_LIFECYCLE_ACTION_APP_START);
 assert(f.lifecycle.state==GKD_LIFECYCLE_WAIT_APP_READY&&f.lifecycle.generation==61U);
 assert(gkd_app_lifecycle_event(&f.lifecycle,GKD_LIFECYCLE_EVENT_APP_READY,60U)<0&&errno==ESTALE);
 assert(!gkd_app_lifecycle_event(&f.lifecycle,GKD_LIFECYCLE_EVENT_APP_READY,61U));
 complete_action(&f,GKD_LIFECYCLE_ACTION_DISPLAY_THAW);
 assert(f.lifecycle.state==GKD_LIFECYCLE_ACTIVE&&f.lifecycle.app_ready&&!f.lifecycle.display_frozen);
 setup(&f,70U);f.lifecycle.usb=GKD_LIFECYCLE_USB_GAME;
 assert(gkd_app_lifecycle_event(&f.lifecycle,GKD_LIFECYCLE_EVENT_CARD_REFRESH,0)<0&&errno==EBUSY&&!f.count);
 setup(&f,80U);f.lifecycle.state=GKD_LIFECYCLE_RECOVERY;f.lifecycle.failed_action=GKD_LIFECYCLE_ACTION_GAME_MOUNT;
 assert(!gkd_app_lifecycle_event(&f.lifecycle,GKD_LIFECYCLE_EVENT_CARD_REFRESH,0));
 puts("GKD_CARD_REFRESH=PASS stop-before-release/new-ready-generation/failure-retry/export-blocked/stale-media-recovery");
}

static void test_wait_kinds(void)
{
 struct fixture f;
 const enum gkd_app_lifecycle_event power[]={GKD_LIFECYCLE_EVENT_SUSPEND,GKD_LIFECYCLE_EVENT_REBOOT,GKD_LIFECYCLE_EVENT_SHUTDOWN};
 assert(gkd_app_lifecycle_wait_kind(NULL)==GKD_APP_WAIT_NONE);
 for(unsigned i=0;i<3;i++){
  setup(&f,1);assert(gkd_app_lifecycle_wait_kind(&f.lifecycle)==GKD_APP_WAIT_NONE);
  assert(!gkd_app_lifecycle_event(&f.lifecycle,GKD_LIFECYCLE_EVENT_MENU,0));
  complete_action(&f,GKD_LIFECYCLE_ACTION_MENU_ACQUIRE);
  assert(!gkd_app_lifecycle_event(&f.lifecycle,power[i],0));
  assert(gkd_app_lifecycle_wait_kind(&f.lifecycle)==GKD_APP_WAIT_NONE);
  complete_action(&f,GKD_LIFECYCLE_ACTION_MENU_RELEASE);
  assert(gkd_app_lifecycle_wait_kind(&f.lifecycle)==GKD_APP_WAIT_POWER);
  fail_action(&f,GKD_LIFECYCLE_ACTION_POWER_QUIESCE,EIO);
  assert(gkd_app_lifecycle_wait_kind(&f.lifecycle)==GKD_APP_WAIT_NONE);
 }
 setup(&f,1);assert(!gkd_app_lifecycle_event(&f.lifecycle,GKD_LIFECYCLE_EVENT_MENU,0));
 complete_action(&f,GKD_LIFECYCLE_ACTION_MENU_ACQUIRE);
 assert(!gkd_app_lifecycle_event(&f.lifecycle,GKD_LIFECYCLE_EVENT_CHARGE,0));
 assert(gkd_app_lifecycle_wait_kind(&f.lifecycle)==GKD_APP_WAIT_NONE);
 complete_action(&f,GKD_LIFECYCLE_ACTION_MENU_RELEASE);
 assert(gkd_app_lifecycle_wait_kind(&f.lifecycle)==GKD_APP_WAIT_USB);
 complete_action(&f,GKD_LIFECYCLE_ACTION_USB_CHARGE);
 assert(gkd_app_lifecycle_wait_kind(&f.lifecycle)==GKD_APP_WAIT_NONE);
}
static void test_usb_transition_visibility(void)
{
 struct fixture f;assert(!gkd_app_lifecycle_usb_transition(NULL));
 for(unsigned mode=0;mode<2;mode++){
  setup(&f,1);assert(!gkd_app_lifecycle_usb_transition(&f.lifecycle));
  assert(!gkd_app_lifecycle_event(&f.lifecycle,GKD_LIFECYCLE_EVENT_MENU,0));
  complete_action(&f,GKD_LIFECYCLE_ACTION_MENU_ACQUIRE);
  assert(!gkd_app_lifecycle_event(&f.lifecycle,mode?GKD_LIFECYCLE_EVENT_DEBUG:GKD_LIFECYCLE_EVENT_STORAGE,0));
  assert(!gkd_app_lifecycle_usb_transition(&f.lifecycle));
  complete_action(&f,GKD_LIFECYCLE_ACTION_MENU_RELEASE);
  while(f.lifecycle.pending_action){
   assert(gkd_app_lifecycle_usb_transition(&f.lifecycle));
   complete_action(&f,f.lifecycle.pending_action);
  }
  assert(!gkd_app_lifecycle_usb_transition(&f.lifecycle));
  assert(!gkd_app_lifecycle_event(&f.lifecycle,GKD_LIFECYCLE_EVENT_RETURN,0));
  while(f.lifecycle.pending_action){
   assert(gkd_app_lifecycle_usb_transition(&f.lifecycle));
   complete_action(&f,f.lifecycle.pending_action);
  }
  if(mode){
   assert(f.lifecycle.waiting_ready&&gkd_app_lifecycle_usb_transition(&f.lifecycle));
   assert(!gkd_app_lifecycle_event(&f.lifecycle,GKD_LIFECYCLE_EVENT_APP_READY,f.lifecycle.requested_generation));
   complete_action(&f,GKD_LIFECYCLE_ACTION_DISPLAY_THAW);
  }
  assert(f.lifecycle.state==GKD_LIFECYCLE_ACTIVE&&!gkd_app_lifecycle_usb_transition(&f.lifecycle));
 }
 setup(&f,1);assert(!gkd_app_lifecycle_event(&f.lifecycle,GKD_LIFECYCLE_EVENT_MENU,0));
 complete_action(&f,GKD_LIFECYCLE_ACTION_MENU_ACQUIRE);
 assert(!gkd_app_lifecycle_event(&f.lifecycle,GKD_LIFECYCLE_EVENT_STORAGE,0));
 complete_action(&f,GKD_LIFECYCLE_ACTION_MENU_RELEASE);
 fail_action(&f,GKD_LIFECYCLE_ACTION_APP_PAUSE,EIO);
 assert(!gkd_app_lifecycle_usb_transition(&f.lifecycle));
}

int main(void)
{
    test_wait_kinds();
    test_usb_transition_visibility();
    test_card_refresh();
    test_storage_roundtrip();
    test_cancel_and_charge();
    test_cancel_during_menu_open();
    test_detach_inflight();
    test_media_failure_recovery();
    test_exit_during_storage_operation();
    test_debug_roundtrip();
    test_failed_stop_never_exports();
    test_unexpected_exit_requires_retry();
    test_power_order_and_resume();
    test_power_stop_exit_is_expected();
    test_autosuspend_without_menu();
    test_update_reboot_without_menu();
    test_dispatch_failure_and_reentrancy();
    test_invalid_api();
    puts("GKD_APP_LIFECYCLE_FIXTURE=PASS ordering/tokens/storage/debug/power/failure/retry/generation/detach");
    return 0;
}
