/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
int main(void) { return getenv("GKD_PROFILE_MARKER") ? 0 : 1; }
