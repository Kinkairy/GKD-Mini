/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-fps-counter.h"
#include "gkd-game-orientation.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static struct gkd_fps_counter_page *counter_page;
static int lifetime_fd = -1;
static int (*original_flip)(void *);
static struct gkd_orientation_page *orientation_page;
static unsigned int *burn_active,*burn_count;
static unsigned int (*burn_flags)(void);
static char *(*burn_name)(unsigned int);

/* The launcher only enables this for the two hash-pinned arcade OPKs. No
 * process-memory offsets, framebuffer heuristics or mutable game configuration. */
static void orientation_attach(int fd)
{
    struct stat st;void *map;
    if(fd<3)return;
    if(!counter_page||fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size!=(off_t)sizeof(struct gkd_orientation_page))goto done;
    int seals=fcntl(fd,F_GET_SEALS);
    if(seals<0||(seals&(F_SEAL_SHRINK|F_SEAL_GROW))!=(F_SEAL_SHRINK|F_SEAL_GROW))goto done;
    map=mmap(NULL,sizeof(struct gkd_orientation_page),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(map==MAP_FAILED)goto done;
    struct gkd_orientation_page *p=map;
    if(p->magic!=GKD_ORIENTATION_MAGIC||p->version!=1||p->aspect||p->reserved||
       !p->driver[0]||!memchr(p->driver,0,sizeof(p->driver)))goto bad;
    burn_active=dlsym(RTLD_DEFAULT,"nBurnDrvActive");
    burn_count=dlsym(RTLD_DEFAULT,"nBurnDrvCount");
    burn_flags=(unsigned int (*)(void))dlsym(RTLD_DEFAULT,"BurnDrvGetFlags");
    burn_name=(char *(*)(unsigned int))dlsym(RTLD_DEFAULT,"BurnDrvGetTextA");
    if(!burn_active||!burn_count||!burn_flags||!burn_name)goto bad;
    orientation_page=p;goto done;
bad:munmap(map,sizeof(struct gkd_orientation_page));
done:close(fd);
}
static void orientation_present(void)
{
    if(!orientation_page)return;
    unsigned aspect=0;
    if(*burn_count>0&&*burn_count<=100000&&*burn_active<*burn_count){
        const char *name=burn_name(0); /* DRV_NAME */
        if(name&&!strncmp(name,orientation_page->driver,sizeof(orientation_page->driver)))
            aspect=(burn_flags()&4U)?GKD_ASPECT_PORTRAIT:GKD_ASPECT_LANDSCAPE;
    }
    __atomic_store_n(&orientation_page->aspect,aspect,__ATOMIC_RELEASE);
}


static int fd_number(const char *value)
{
    unsigned int number = 0;
    if (!value || !*value) return -1;
    for (; *value; ++value) {
        unsigned int digit;
        if (*value < '0' || *value > '9') return -1;
        digit = (unsigned int)(*value - '0');
        if (number > ((unsigned int)INT32_MAX - digit) / 10U) return -1;
        number = number * 10U + digit;
    }
    return number >= 3U ? (int)number : -1;
}

static int hex_nibble(unsigned char c, unsigned int *value)
{
    if (c >= '0' && c <= '9') *value = c - '0';
    else if (c >= 'a' && c <= 'f') *value = c - 'a' + 10U;
    else if (c >= 'A' && c <= 'F') *value = c - 'A' + 10U;
    else return 0;
    return 1;
}

static int session_token(const char *text, uint64_t *hi, uint64_t *lo)
{
    uint64_t values[2] = {0, 0};
    unsigned int index, nibble;
    if (!text || strlen(text) != 32U) return 0;
    for (index = 0; index < 32U; ++index) {
        if (!hex_nibble((unsigned char)text[index], &nibble)) return 0;
        values[index / 16U] = (values[index / 16U] << 4) | nibble;
    }
    if (!(values[0] | values[1])) return 0;
    *hi = values[0]; *lo = values[1];
    return 1;
}

static int separator(unsigned char c)
{
    return c == ':' || c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int consume_self_preload(const char *self_path)
{
    const char *preload = getenv("LD_PRELOAD"), *cursor, *start;
    char *clean;
    size_t self_length, output = 0, preload_length;
    unsigned int removed = 0;
    int result;

    if (!preload || !self_path || self_path[0] != '/') return 0;
    self_length = strlen(self_path);
    for (cursor = self_path; *cursor; ++cursor)
        if (separator((unsigned char)*cursor)) return 0;
    preload_length = strlen(preload);
    clean = malloc(preload_length + 1U);
    if (!clean) return 0;
    cursor = preload;
    while (*cursor) {
        size_t length;
        while (*cursor && separator((unsigned char)*cursor)) ++cursor;
        start = cursor;
        while (*cursor && !separator((unsigned char)*cursor)) ++cursor;
        length = (size_t)(cursor - start);
        if (!length) continue;
        if (length == self_length && !memcmp(start, self_path, length)) {
            ++removed;
            continue;
        }
        if (output) clean[output++] = ':';
        memcpy(clean + output, start, length); output += length;
    }
    /* The trusted launcher supplies the one absolute token it appended. */
    if (removed != 1U) { free(clean); return 0; }
    clean[output] = 0;
    result = output ? setenv("LD_PRELOAD", clean, 1) == 0
                    : unsetenv("LD_PRELOAD") == 0;
    free(clean);
    return result;
}

static int page_header_valid(const struct gkd_fps_counter_page *page,
                             uint64_t hi, uint64_t lo)
{
    unsigned int index;
    if (page->magic != GKD_FPS_COUNTER_MAGIC ||
        page->version != GKD_FPS_COUNTER_VERSION ||
        page->bytes != GKD_FPS_COUNTER_BYTES || page->flags ||
        page->session_hi != hi || page->session_lo != lo ||
        page->producer_state != GKD_FPS_PRODUCER_UNAVAILABLE) return 0;
    for (index = 0; index < 6U; ++index)
        if (page->reserved[index]) return 0;
    return 1;
}

static int cloexec(int fd)
{
    int flags = fcntl(fd, F_GETFD);
    return flags >= 0 && fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

static int write_pipe(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    return flags >= 0 && (flags & O_ACCMODE) == O_WRONLY;
}

static void after_fork_child(void)
{
    counter_page = NULL;
    orientation_page = NULL;
    if (lifetime_fd >= 0) (void)close(lifetime_fd);
    lifetime_fd = -1;
}

__attribute__((constructor)) static void fps_present_init(void)
{
    int saved = errno;
    int orientation = fd_number(getenv(GKD_ORIENTATION_FD_ENV));
    (void)unsetenv(GKD_ORIENTATION_FD_ENV);
    int fd = fd_number(getenv(GKD_FPS_COUNTER_FD_ENV));
    int life = fd_number(getenv(GKD_FPS_LIFETIME_FD_ENV));
    uint64_t hi = 0, lo = 0;
    int token_ok = session_token(getenv(GKD_FPS_SESSION_ENV), &hi, &lo);
    const char *preload_path = getenv(GKD_FPS_PRELOAD_PATH_ENV);
    int preload_ok;
    struct stat st, life_st;
    void *mapping = MAP_FAILED;

    original_flip = (int (*)(void *))dlsym(RTLD_NEXT, "SDL_Flip");
    preload_ok = consume_self_preload(preload_path);
    (void)unsetenv(GKD_FPS_COUNTER_FD_ENV);
    (void)unsetenv(GKD_FPS_LIFETIME_FD_ENV);
    (void)unsetenv(GKD_FPS_SESSION_ENV);
    (void)unsetenv(GKD_FPS_PRELOAD_PATH_ENV);

    if (preload_ok && original_flip && fd >= 3 && life >= 3 && fd != life &&
        token_ok && !fstat(fd, &st) && S_ISREG(st.st_mode) &&
        st.st_size == (off_t)GKD_FPS_COUNTER_BYTES &&
        !fstat(life, &life_st) && S_ISFIFO(life_st.st_mode) &&
        write_pipe(life) && cloexec(fd) && cloexec(life)) {
        mapping = mmap(NULL, GKD_FPS_COUNTER_BYTES, PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd, 0);
        if (mapping != MAP_FAILED &&
            page_header_valid((const struct gkd_fps_counter_page *)mapping, hi, lo)) {
            counter_page = mapping;
            lifetime_fd = life;
            life = -1;
            if (pthread_atfork(NULL, NULL, after_fork_child) != 0) {
                counter_page = NULL;
                (void)close(lifetime_fd);
                lifetime_fd = -1;
                (void)munmap(mapping, GKD_FPS_COUNTER_BYTES);
            } else {
                __atomic_store_n(&counter_page->producer_state,
                                 GKD_FPS_PRODUCER_ATTACHED, __ATOMIC_RELEASE);
            }
        } else if (mapping != MAP_FAILED) {
            (void)munmap(mapping, GKD_FPS_COUNTER_BYTES);
        }
    }
    if(orientation>=3&&orientation!=fd&&orientation!=life&&orientation!=lifetime_fd)orientation_attach(orientation);
    if (fd >= 3) (void)close(fd);
    if (life >= 3) (void)close(life);
    errno = saved;
}

__attribute__((destructor)) static void fps_present_done(void)
{
    if(orientation_page){__atomic_store_n(&orientation_page->aspect,0U,__ATOMIC_RELEASE);munmap(orientation_page,sizeof(*orientation_page));orientation_page=NULL;}
    if (lifetime_fd >= 0) (void)close(lifetime_fd);
    lifetime_fd = -1;
    counter_page = NULL;
}

__attribute__((visibility("default"))) int SDL_Flip(void *surface)
{
    int result, saved;
    if (!original_flip) { errno = ENOSYS; return -1; }
    result = original_flip(surface);
    saved = errno;
    if (result == 0 && counter_page)
        (void)__atomic_fetch_add(&counter_page->successful_flips, 1U,
                                 __ATOMIC_RELAXED);
    if(result==0)orientation_present();
    errno = saved;
    return result;
}
