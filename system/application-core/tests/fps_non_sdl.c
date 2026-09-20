/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    const char *expected = getenv("GKD_TEST_EXPECT_PRELOAD");
    const char *actual = getenv("LD_PRELOAD");
    if (!expected || (*expected ? (!actual || strcmp(actual, expected)) : actual != NULL) ||
        getenv("GKD_FPS_COUNTER_FD") || getenv("GKD_FPS_LIFETIME_FD") ||
        getenv("GKD_FPS_SESSION") || getenv("GKD_FPS_PRELOAD_PATH")) return 9;
    if (argc == 2 && !strcmp(argv[1], "hold")) sleep(2);
    return 0;
}
