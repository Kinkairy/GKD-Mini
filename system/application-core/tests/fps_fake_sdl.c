/* SPDX-License-Identifier: GPL-2.0 */
#include <errno.h>
int SDL_Flip(void *surface)
{
    int result = *(const int *)surface;
    errno = 97;
    return result;
}
