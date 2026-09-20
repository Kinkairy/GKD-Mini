#define _GNU_SOURCE
#include "gkd-opk-plan.h"
#include "opk.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct mock_pair {
    const char *key;
    size_t key_size;
    const char *value;
    size_t value_size;
};
struct mock_meta {
    const char *filename;
    const struct mock_pair *pairs;
    size_t pair_count;
};
struct mock_fixture {
    const struct mock_meta *metas;
    size_t meta_count;
    int fail_open;
    int fail_meta;
    int fail_pair;
    const char *last_path;
    unsigned closes;
} fixture;

struct OPK {
    size_t next_meta;
    size_t active_meta;
    size_t next_pair;
};

static int allocation_budget = -1;

void *gkd_opk_plan_test_malloc(size_t size)
{
    if (!allocation_budget) return NULL;
    if (allocation_budget > 0) --allocation_budget;
    return malloc(size);
}
void gkd_opk_plan_test_free(void *ptr) { free(ptr); }

struct OPK *opk_open(const char *path)
{
    static struct OPK opk;
    fixture.last_path = path;
    fixture.closes = 0;
    if (fixture.fail_open) {
        errno = EIO;
        return NULL;
    }
    opk.next_meta = 0;
    opk.active_meta = 0;
    opk.next_pair = 0;
    return &opk;
}
void opk_close(struct OPK *opk)
{
    assert(opk);
    ++fixture.closes;
}
int opk_open_metadata(struct OPK *opk, const char **filename)
{
    if (fixture.fail_meta) {
        errno = EIO;
        return -1;
    }
    if (opk->next_meta == fixture.meta_count) return 0;
    opk->active_meta = opk->next_meta++;
    opk->next_pair = 0;
    *filename = fixture.metas[opk->active_meta].filename;
    return 1;
}
int opk_read_pair(struct OPK *opk, const char **key, size_t *key_size,
                  const char **value, size_t *value_size)
{
    const struct mock_meta *meta;
    const struct mock_pair *pair;
    if (fixture.fail_pair) {
        errno = EIO;
        return -1;
    }
    meta = &fixture.metas[opk->active_meta];
    if (opk->next_pair == meta->pair_count) return 0;
    pair = &meta->pairs[opk->next_pair++];
    *key = pair->key;
    *key_size = pair->key_size;
    *value = pair->value;
    *value_size = pair->value_size;
    return 1;
}

#define PAIR(k, v) {(k), sizeof(k) - 1U, (v), sizeof(v) - 1U}
#define RAW(k, ks, v, vs) {(k), (ks), (v), (vs)}

static void configure(const struct mock_meta *metas, size_t count)
{
    memset(&fixture, 0, sizeof(fixture));
    fixture.metas = metas;
    fixture.meta_count = count;
    allocation_budget = -1;
}

static void expect_empty(const struct gkd_opk_plan *plan)
{
    assert(!plan->mount_name);
    for (size_t i = 0; i < GKD_OPK_PLAN_ARGV_MAX; ++i) assert(!plan->argv[i]);
}
static void expect_failure(const char *image, const char *metadata,
                           int argc, char *const argv[], int error)
{
    struct gkd_opk_plan plan = {0};
    errno = 0;
    assert(gkd_opk_plan_open(image, metadata, argc, argv, &plan) == -1);
    assert(errno == error);
    expect_empty(&plan);
    assert(fixture.closes == 1U || fixture.fail_open);
}
static void touch_file(const char *path)
{
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
    assert(fd >= 0);
    assert(!close(fd));
}
static void test_metadata_and_flags(void)
{
    static const char utf_name[] = "\xe6\xb5\x8b\xe8\xaf\x95 Game";
    static const struct mock_pair ignored[] = {PAIR("Exec", "ignored")};
    static const struct mock_pair selected[] = {
        PAIR("Name", utf_name), PAIR("Exec", "runner alpha   beta"),
        PAIR("Terminal", "true"), PAIR("X-OD-NeedsJoystick", "true"),
        PAIR("X-OD-NeedsGSensor", "true"), PAIR("X-OD-NeedsDownscaling", "true")
    };
    static const struct mock_meta metas[] = {
        {"ignored.desktop", ignored, 1U}, {"selected.desktop", selected, 6U}
    };
    struct gkd_opk_plan plan = {0};
    configure(metas, 2U);
    assert(!gkd_opk_plan_open("/proc/self/fd/17", "selected.desktop", 0, NULL, &plan));
    assert(!strcmp(fixture.last_path, "/proc/self/fd/17"));
    assert(!strcmp(plan.mount_name, utf_name));
    assert(!strcmp(plan.argv[0], "runner"));
    assert(!strcmp(plan.argv[1], "alpha"));
    assert(!strcmp(plan.argv[2], "beta"));
    assert(!plan.argv[3]);
    assert(plan.needs_terminal && plan.needs_joystick && plan.needs_gsensor && plan.needs_downscaling);
    gkd_opk_plan_close(&plan);
    expect_empty(&plan);
}
static void test_realpath_tokens(void)
{
    static const struct mock_pair f_pairs[] = {PAIR("Name", "mount"), PAIR("Exec", "run %F")};
    static const struct mock_pair u_pairs[] = {PAIR("Name", "mount"), PAIR("Exec", "run %U")};
    static const struct mock_pair one_pairs[] = {PAIR("Name", "mount"), PAIR("Exec", "run %f")};
    static const struct mock_pair one_url_pairs[] = {PAIR("Name", "mount"), PAIR("Exec", "run %u")};
    static const struct mock_meta f_meta[] = {{"main.desktop", f_pairs, 2U}};
    static const struct mock_meta u_meta[] = {{"main.desktop", u_pairs, 2U}};
    static const struct mock_meta one_meta[] = {{"main.desktop", one_pairs, 2U}};
    static const struct mock_meta one_url_meta[] = {{"main.desktop", one_url_pairs, 2U}};
    static const char remote_url[] = "https://example.invalid/game.opk?slot=1";
    char directory[] = "/tmp/gkd-opk-plan.XXXXXX";
    char spaced[512], utf8[512], *args[2];
    char *expected_one;
    struct gkd_opk_plan plan = {0};

    assert(mkdtemp(directory));
    snprintf(spaced, sizeof(spaced), "%s/file with space", directory);
    snprintf(utf8, sizeof(utf8), "%s/\xe5\x8f\x82\xe6\x95\xb0", directory);
    touch_file(spaced);
    touch_file(utf8);
    args[0] = spaced; args[1] = utf8;

    configure(f_meta, 1U);
    assert(!gkd_opk_plan_open("image", NULL, 2, args, &plan));
    assert(!strcmp(plan.argv[0], "run"));
    expected_one = realpath(spaced, NULL);
    assert(expected_one && !strcmp(plan.argv[1], expected_one));
    free(expected_one);
    expected_one = realpath(utf8, NULL);
    assert(expected_one && !strcmp(plan.argv[2], expected_one));
    free(expected_one);
    gkd_opk_plan_close(&plan);

    configure(u_meta, 1U);
    assert(!gkd_opk_plan_open("image", NULL, 2, args, &plan));
    expected_one = realpath(spaced, NULL);
    assert(expected_one);
    assert(!strncmp(plan.argv[1], "file://", 7U));
    assert(!strcmp(plan.argv[1] + 7, expected_one));
    free(expected_one);
    gkd_opk_plan_close(&plan);

    configure(one_meta, 1U);
    assert(!gkd_opk_plan_open("image", NULL, 1, args, &plan));
    gkd_opk_plan_close(&plan);
    configure(one_url_meta, 1U);
    assert(!gkd_opk_plan_open("image", NULL, 1, args, &plan));
    expected_one = realpath(spaced, NULL);
    assert(expected_one && !strcmp(plan.argv[1] + 7, expected_one));
    free(expected_one);
    assert(!strncmp(plan.argv[1], "file://", 7U));
    gkd_opk_plan_close(&plan);

    /* A URI is not a local path. %u/%U must preserve it when realpath()
     * cannot resolve it, while local inputs still become file: URIs. */
    args[0] = (char *)remote_url;
    configure(one_url_meta, 1U);
    assert(!gkd_opk_plan_open("image", NULL, 1, args, &plan));
    assert(!strcmp(plan.argv[0], "run"));
    assert(!strcmp(plan.argv[1], remote_url));
    gkd_opk_plan_close(&plan);

    args[0] = spaced; args[1] = (char *)remote_url;
    configure(u_meta, 1U);
    assert(!gkd_opk_plan_open("image", NULL, 2, args, &plan));
    expected_one = realpath(spaced, NULL);
    assert(expected_one && !strncmp(plan.argv[1], "file://", 7U));
    assert(!strcmp(plan.argv[1] + 7, expected_one));
    assert(!strcmp(plan.argv[2], remote_url));
    free(expected_one);
    gkd_opk_plan_close(&plan);

    args[0] = spaced; args[1] = utf8;
    configure(one_meta, 1U);
    expect_failure("image", NULL, 2, args, E2BIG);
    configure(one_meta, 1U);
    expect_failure("image", NULL, 0, NULL, EINVAL);
    args[0] = "/no/such/gkd-opk-plan-file";
    configure(one_meta, 1U);
    expect_failure("image", NULL, 1, args, ENOENT);
    unlink(spaced);
    unlink(utf8);
    rmdir(directory);
}
static void test_invalid_metadata(void)
{
    static const char nul_name[] = {'a', 0, 'b'};
    static const struct mock_pair missing_name[] = {PAIR("Exec", "run")};
    static const struct mock_pair missing_exec[] = {PAIR("Name", "mount")};
    static const struct mock_pair empty_name[] = {PAIR("Name", ""), PAIR("Exec", "run")};
    static const struct mock_pair empty_exec[] = {PAIR("Name", "mount"), PAIR("Exec", "")};
    static const struct mock_pair slash_name[] = {PAIR("Name", "a/b"), PAIR("Exec", "run")};
    static const struct mock_pair dot_name[] = {PAIR("Name", "."), PAIR("Exec", "run")};
    static const struct mock_pair dots_name[] = {PAIR("Name", ".."), PAIR("Exec", "run")};
    static const struct mock_pair nul_value[] = {
        PAIR("Name", "mount"), RAW("Exec", 4U, nul_name, sizeof(nul_name))
    };
    static const struct mock_pair nul_name_pair[] = {
        RAW("Name", 4U, nul_name, sizeof(nul_name)), PAIR("Exec", "run")
    };
    static const struct mock_pair wrong_keys[] = {PAIR("NameX", "mount"), PAIR("ExecX", "run")};
    static const struct mock_pair duplicate[] = {
        PAIR("Name", "mount"), PAIR("Name", "again"), PAIR("Exec", "run")
    };
    const struct mock_pair *sets[] = {missing_name, missing_exec, empty_name, empty_exec, slash_name,
        dot_name, dots_name, nul_value, nul_name_pair, wrong_keys, duplicate};
    const size_t counts[] = {1U, 1U, 2U, 2U, 2U, 2U, 2U, 2U, 2U, 2U, 3U};
    for (size_t i = 0; i < sizeof(sets) / sizeof(sets[0]); ++i) {
        struct mock_meta meta = {"main.desktop", sets[i], counts[i]};
        configure(&meta, 1U);
        expect_failure("image", NULL, 0, NULL, EPROTO);
    }
}
static void test_boundaries_and_allocations(void)
{
    static const struct mock_pair base[] = {PAIR("Name", "mount"), PAIR("Exec", "run")};
    static const struct mock_meta base_meta[] = {{"main.desktop", base, 2U}};
    char *many[255];
    char *exec = NULL;
    size_t cap = 2048U, length = 0;
    struct mock_pair pairs[2];
    struct mock_meta meta = {"main.desktop", pairs, 2U};
    struct gkd_opk_plan plan = {0};

    for (size_t i = 0; i < 255U; ++i) many[i] = "/dev/null";
    configure(base_meta, 1U);
    expect_failure("image", NULL, 1, many, E2BIG);

    exec = malloc(cap);
    assert(exec);
    for (size_t i = 0; i < 255U; ++i) {
        length += (size_t)snprintf(exec + length, cap - length, "%s%s", i ? " " : "", "x");
    }
    pairs[0] = (struct mock_pair)PAIR("Name", "mount");
    pairs[1] = (struct mock_pair)RAW("Exec", 4U, exec, length);
    configure(&meta, 1U);
    assert(!gkd_opk_plan_open("image", NULL, 0, NULL, &plan));
    gkd_opk_plan_close(&plan);
    length = 0;
    for (size_t i = 0; i < 256U; ++i) {
        length += (size_t)snprintf(exec + length, cap - length, "%s%s", i ? " " : "", "x");
    }
    pairs[1].value_size = length;
    configure(&meta, 1U);
    expect_failure("image", NULL, 0, NULL, E2BIG);
    free(exec);

    for (int budget = 0; budget < 3; ++budget) {
        configure(base_meta, 1U);
        allocation_budget = budget;
        expect_failure("image", NULL, 0, NULL, ENOMEM);
    }
    allocation_budget = -1;
}
static void test_library_errors_and_exact_flags(void)
{
    static const struct mock_pair pairs[] = {
        PAIR("Name", "mount"), PAIR("Exec", "run"),
        PAIR("Terminal", "tru"), PAIR("X-OD-NeedsJoystick", "true")
    };
    static const struct mock_meta metas[] = {{"main.desktop", pairs, 4U}};
    struct gkd_opk_plan plan = {0};
    configure(metas, 1U);
    assert(!gkd_opk_plan_open("image", NULL, 0, NULL, &plan));
    assert(!plan.needs_terminal && plan.needs_joystick);
    gkd_opk_plan_close(&plan);
    configure(metas, 1U);
    expect_failure("image", "missing.desktop", 0, NULL, ENOENT);
    configure(metas, 1U); fixture.fail_open = 1;
    expect_failure("image", NULL, 0, NULL, EIO);
    configure(metas, 1U); fixture.fail_meta = 1;
    expect_failure("image", NULL, 0, NULL, EIO);
    configure(metas, 1U); fixture.fail_pair = 1;
    expect_failure("image", NULL, 0, NULL, EIO);
}
int main(void)
{
    test_metadata_and_flags();
    test_realpath_tokens();
    test_invalid_metadata();
    test_boundaries_and_allocations();
    test_library_errors_and_exact_flags();
    puts("gkd-opk-plan mock tests PASS cases=5");
    return 0;
}

