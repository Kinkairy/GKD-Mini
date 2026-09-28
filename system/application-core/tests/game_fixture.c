/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#define GKD_APP_GAME_FIXTURE
#define GKD_APP_GAME_NO_MAIN
#include "../source/gkd-app-game.c"

#include <assert.h>
#include <limits.h>
#include <sys/mount.h>
#include <sys/prctl.h>

#if defined(__GNUC__)
#define FIXTURE_CHILD __attribute__((no_sanitize_address, no_sanitize_undefined))
#else
#define FIXTURE_CHILD
#endif
static char fixture_root[PATH_MAX];
static int commands_pipe[2], reports_pipe[2], child_ready_pipe[2];
static pid_t menu_pid = -1, publisher_pid = -1;
static FIXTURE_CHILD void fixture_die(int code)
{
    dprintf(STDERR_FILENO, "GAME_FIXTURE_CHILD_FAILED code=%d errno=%d\n", code, errno);
    _exit(code);
}

static void write_all(int fd, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    while (size) {
        ssize_t n = write(fd, bytes, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) fixture_die(100);
        bytes += n; size -= (size_t)n;
    }
}
static void read_all(int fd, void *data, size_t size)
{
    unsigned char *bytes = data;
    while (size) {
        ssize_t n = read(fd, bytes, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { fprintf(stderr, "fixture read failed errno=%d\n", errno); abort(); }
        bytes += n; size -= (size_t)n;
    }
}
static void mkdir_exact(const char *path)
{
    if (mkdir(path, 0755) && errno != EEXIST) { perror(path); abort(); }
}
static FIXTURE_CHILD void child_pause(void)
{
    for (;;) pause();
}
static FIXTURE_CHILD void menu_child(void)
{
    if (prctl(PR_SET_NAME, "simplemenu.real", 0, 0, 0)) fixture_die(101);
    int display = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (display < 0) fixture_die(102);
    struct gkd_app_process_identity identity;
    if (gkd_app_process_identity_read(getpid(), &identity)) fixture_die(103);
    int fd = open("/run/menu.identity", O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
    if (fd < 0) fixture_die(104);
    dprintf(fd, "%ld %llu\n", (long)getpid(), identity.starttime);
    close(fd);
    write_all(child_ready_pipe[1], "M", 1);
    child_pause();
}
static FIXTURE_CHILD void game_leader(void)
{
    if (setsid() < 0 || prctl(PR_SET_NAME, "opkrun", 0, 0, 0)) fixture_die(105);
    pid_t emulator = fork();
    if (!emulator) {
        if (prctl(PR_SET_NAME, "fbasdl.dge", 0, 0, 0)) fixture_die(106);
        child_pause();
    }
    if (emulator < 0) fixture_die(107);
    struct gkd_app_process_identity identity;
    if (gkd_app_process_identity_read(getpid(), &identity) || identity.pgid != getpid() ||
        identity.sid != getpid()) fixture_die(108);
    int fd = open(ACTIVE_GAME_FILE, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    if (fd < 0) fixture_die(109);
    dprintf(fd, "pid=%ld\npgid=%ld\nstarttime=%llu\n",
            (long)getpid(), (long)getpid(), identity.starttime);
    close(fd);
    int listener = gkd_app_game_listen(identity.starttime);
    if (listener < 0) fixture_die(110);
    write_all(child_ready_pipe[1], "G", 1);
    for(unsigned request=0;request<3;request++) {
        struct pollfd wait = {listener, POLLIN, 0};unsigned operation;
        if (poll(&wait, 1, 4000) != 1 || !(wait.revents & POLLIN)) fixture_die(111);
        int client = gkd_app_game_accept_operation(listener, identity.starttime, &operation);
        if(client<0)fixture_die(111);
        if(request==1){
            struct gkd_game_orientation orientation=GKD_GAME_ORIENTATION_INIT;
            orientation.aspect=GKD_ASPECT_PORTRAIT;orientation.source=GKD_ORIGIN_BURN_DRIVER;
            orientation.scope=GKD_SCOPE_ACTIVE_DRIVER;
            if(operation!=GKD_GAME_ORIENTATION||gkd_app_game_reply_orientation(client,&orientation))fixture_die(111);
        }else if(operation!=(request?GKD_GAME_EXIT:GKD_GAME_MENU)||
            gkd_app_game_reply_operation(client,operation,0))fixture_die(111);
    }
    close(listener);
    if (kill(emulator, SIGTERM)) fixture_die(112);
    int status;
    while (waitpid(emulator, &status, 0) < 0)
        if (errno != EINTR) fixture_die(113);
    _exit(0);
}
static FIXTURE_CHILD void publisher_child(void)
{
    pid_t leader = fork();
    if (!leader) game_leader();
    if (leader < 0) fixture_die(111);
    int status;
    while (waitpid(leader, &status, 0) < 0)
        if (errno != EINTR) fixture_die(112);
    unlink(ACTIVE_GAME_FILE);
    _exit(0);
}
static FIXTURE_CHILD int init_child(void *unused)
{
    (void)unused;
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) ||
        mount(fixture_root, fixture_root, NULL, MS_BIND | MS_REC, NULL)) fixture_die(113);
    char proc_path[PATH_MAX], executable[PATH_MAX];
    ssize_t executable_length = readlink("/proc/self/exe", executable, sizeof(executable) - 1U);
    if (executable_length <= 0) fixture_die(114);
    executable[executable_length] = 0;
    if (snprintf(proc_path, sizeof(proc_path), "%s/proc", fixture_root) >= (int)sizeof(proc_path)) fixture_die(114);
    if (mount("proc", proc_path, "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL)) fixture_die(114);
    if (snprintf(proc_path, sizeof(proc_path), "%s/usr/sbin/gkd-app-game", fixture_root) >= (int)sizeof(proc_path) ||
        mount(executable, proc_path, NULL, MS_BIND, NULL)) fixture_die(114);
    if (snprintf(proc_path, sizeof(proc_path), "%s/dev", fixture_root) >= (int)sizeof(proc_path) ||
        mount("/dev", proc_path, NULL, MS_BIND | MS_REC, NULL) ||
        chroot(fixture_root) || chdir("/")) fixture_die(114);
    if (pipe(child_ready_pipe)) fixture_die(115);
    write_all(reports_pipe[1], "I", 1);
    for (;;) {
        char command;
        ssize_t n = read(commands_pipe[0], &command, 1);
        if (n < 0 && errno == EINTR) continue;
        if (n != 1) fixture_die(115);
        if (command == 'S') {
            menu_pid = fork();
            if (!menu_pid) menu_child();
            if (menu_pid < 0) fixture_die(116);
            char ready;
            read_all(child_ready_pipe[0], &ready, 1);
            if (ready != 'M') fixture_die(117);
            publisher_pid = fork();
            if (!publisher_pid) publisher_child();
            if (publisher_pid < 0) fixture_die(118);
            read_all(child_ready_pipe[0], &ready, 1);
            if (ready != 'G') fixture_die(119);
            write_all(reports_pipe[1], "A", 1);
        } else if (command == 'X') {
            if (menu_pid > 0) kill(menu_pid, SIGKILL);
            if (publisher_pid > 0) kill(publisher_pid, SIGKILL);
            while (waitpid(-1, NULL, WNOHANG) > 0) {}
            _exit(0);
        } else fixture_die(120);
    }
}
static FIXTURE_CHILD int host_child(void *unused)
{
    (void)unused;
    if (unshare(CLONE_NEWNS) || mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)) fixture_die(121);
    void *stack = malloc(256 * 1024);
    if (!stack) fixture_die(122);
    pid_t init = clone(init_child, (char *)stack + 256 * 1024,
                       CLONE_NEWPID | CLONE_NEWNS | SIGCHLD, NULL);
    if (init < 0) fixture_die(123);
    write_all(reports_pipe[1], &init, sizeof(init));
    int status;
    while (waitpid(init, &status, 0) < 0)
        if (errno != EINTR) fixture_die(124);
    free(stack);
    _exit(WIFEXITED(status) ? WEXITSTATUS(status) : 125);
}
static int run_cli(const char *verb, pid_t host, pid_t init, char *output, size_t size)
{
    int pipefd[2];
    assert(!pipe2(pipefd, O_CLOEXEC));
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        assert(dup2(pipefd[1], STDOUT_FILENO) == STDOUT_FILENO);
        close(pipefd[0]); close(pipefd[1]);
        char host_text[32], init_text[32];
        snprintf(host_text, sizeof(host_text), "%ld", (long)host);
        snprintf(init_text, sizeof(init_text), "%ld", (long)init);
        char *argv[] = {"gkd-app-game", (char *)verb, host_text, init_text, NULL};
        _exit(gkd_app_game_main(4, argv));
    }
    close(pipefd[1]);
    size_t used = 0;
    while (used + 1 < size) {
        ssize_t n = read(pipefd[0], output + used, size - used - 1);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        used += (size_t)n;
    }
    output[used] = 0; close(pipefd[0]);
    int status;
    assert(waitpid(child, &status, 0) == child);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 255;
}
static void registry_path(char *path, size_t size)
{
    assert(snprintf(path, size, "%s%s", fixture_root, ACTIVE_GAME_FILE) < (int)size);
}
static char *read_file(const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    assert(fd >= 0);
    char *data = calloc(1, 1024);
    assert(data);
    ssize_t n = read(fd, data, 1023);
    assert(n >= 0); close(fd); return data;
}
static void write_file(const char *path, const char *data)
{
    int fd = open(path, O_WRONLY | O_TRUNC | O_CLOEXEC);
    assert(fd >= 0);
    write_all(fd, data, strlen(data)); close(fd);
}
static void identity_contract(void)
{
    struct gkd_app_process_identity identity;
    assert(!gkd_app_process_identity_read(getpid(), &identity));
    assert(identity.starttime && identity.pgid > 0 && identity.sid > 0);
    errno = 0;
    assert(gkd_app_process_identity_read(0, &identity) && errno == EINVAL);
}
int main(int argc, char **argv)
{
    assert(argc == 2 && geteuid() == 0);
    assert(strlen(argv[1]) + 1 < sizeof(fixture_root));
    identity_contract();
    strcpy(fixture_root, argv[1]);
    mkdir_exact(fixture_root);
    char path[PATH_MAX];
    const char *dirs[] = {"/proc", "/dev", "/var", "/var/run", "/var/run/gkd-mini", "/run", "/usr", "/usr/sbin"};
    for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
        snprintf(path, sizeof(path), "%s%s", fixture_root, dirs[i]);
        mkdir_exact(path);
    }
    assert(snprintf(path, sizeof(path), "%s/usr/sbin/gkd-app-game", fixture_root) < (int)sizeof(path));
    int game_file = open(path, O_WRONLY | O_CREAT | O_EXCL, 0500); assert(game_file >= 0); assert(!close(game_file));
    assert(!pipe(commands_pipe) && !pipe(reports_pipe));
    void *stack = malloc(256 * 1024);
    assert(stack);
    pid_t host = clone(host_child, (char *)stack + 256 * 1024, SIGCHLD, NULL);
    assert(host > 1);
    pid_t init;
    read_all(reports_pipe[0], &init, sizeof(init));
    char ready;
    read_all(reports_pipe[0], &ready, 1);
    assert(ready == 'I');

    char output[512];
    assert(run_cli("check", host, init, output, sizeof(output)) == 0);
    assert(!strcmp(output, "IDLE\n"));
    assert(!run_cli("orientation",host,init,output,sizeof(output)));
    assert(strstr(output,"session=idle aspect=unknown "));
    assert(run_cli("exit", host, init, output, sizeof(output)) != 0);
    assert(!output[0]);

    write_all(commands_pipe[1], "S", 1);
    read_all(reports_pipe[0], &ready, 1);
    assert(ready == 'A');
    assert(run_cli("check", host, init, output, sizeof(output)) == 0);
    assert(!strcmp(output, "ACTIVE\n"));
    assert(!run_cli("menu",host,init,output,sizeof(output))&&!strcmp(output,"MENU_DELIVERED\n"));
    assert(!run_cli("check",host,init,output,sizeof(output))&&!strcmp(output,"ACTIVE\n"));

    assert(!run_cli("orientation",host,init,output,sizeof(output)));
    assert(strstr(output,"session=active aspect=portrait ")&&strstr(output,"source=burn-driver scope=active-driver"));
    char active_path[PATH_MAX];
    registry_path(active_path, sizeof(active_path));
    char *valid = read_file(active_path);
    char stale[384];
    long game_pid, game_pgid;
    unsigned long long game_start;
    assert(sscanf(valid, "pid=%ld\npgid=%ld\nstarttime=%llu\n",
                  &game_pid, &game_pgid, &game_start) == 3);
    snprintf(stale, sizeof(stale), "pid=%ld\npgid=%ld\nstarttime=%llu\n",
             game_pid, game_pgid, game_start + 1);
    write_file(active_path, stale);
    assert(run_cli("check", host, init, output, sizeof(output)) != 0 && !output[0]);
    write_file(active_path, "pid=2\npid=2\nstarttime=1\n");
    assert(run_cli("check", host, init, output, sizeof(output)) != 0 && !output[0]);
    write_file(active_path, valid);
    free(valid);
    assert(run_cli("check", getpid(), init, output, sizeof(output)) != 0 && !output[0]);

    char menu_path[PATH_MAX];
    assert(snprintf(menu_path, sizeof(menu_path), "%s/run/menu.identity", fixture_root) < (int)sizeof(menu_path));
    char *menu_before = read_file(menu_path);
    assert(run_cli("exit", host, init, output, sizeof(output)) == 0);
    assert(!strcmp(output, "EXITED\n"));
    for (unsigned i = 0; i < 200 && access(active_path, F_OK) == 0; ++i) usleep(10000);
    assert(access(active_path, F_OK) && errno == ENOENT);
    assert(!run_cli("orientation",host,init,output,sizeof(output)));
    assert(strstr(output,"session=idle aspect=unknown "));
    char *menu_after = read_file(menu_path);
    assert(!strcmp(menu_before, menu_after));
    free(menu_before); free(menu_after);
    assert(run_cli("check", host, init, output, sizeof(output)) == 0);
    assert(!strcmp(output, "IDLE\n"));
    assert(!run_cli("orientation",host,init,output,sizeof(output)));
    assert(strstr(output,"session=idle aspect=unknown "));

    write_all(commands_pipe[1], "X", 1);
    int status;
    assert(waitpid(host, &status, 0) == host && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    free(stack);
    puts("GKD_APP_GAME_FIXTURE=PASS pidns/mntns/root-pin/idle/active/stale/malformed/listen-accept-reply/typed-menu-stays-active/same-menu");
    return 0;
}
