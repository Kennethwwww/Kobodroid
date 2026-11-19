/* Native UI handoff. The child owns the pause and its reversal, including
 * when the display bridge is killed or stops sending its heartbeat. */
#include <dirent.h>
#include <signal.h>

#ifndef KOBO_NICKEL_EXE
#define KOBO_NICKEL_EXE "/usr/local/Kobo/nickel"
#endif
#ifndef KOBO_DBUS_SEND
#define KOBO_DBUS_SEND "/bin/dbus-send"
#endif

struct identity { pid_t pid; unsigned long long start; };
struct lease { pid_t guardian; int life; int *refresh_busy; };
/* The bridge loop beats about once a second; the guardian recovers Kobo after LEASE_SILENCE_MS without one. */
enum { LEASE_SILENCE_MS = 30000 };
static void lease_beat(struct lease lease) {
    if (lease.guardian && write(lease.life, "H", 1) < 0) perror("display guardian heartbeat");
}
static void lease_refresh(struct lease lease, int busy) {
    if (lease.refresh_busy) __atomic_store_n(lease.refresh_busy, busy, __ATOMIC_RELEASE);
}

static unsigned long long proc_start(pid_t pid, char *state) {
    char path[64], line[1024];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int ok = fgets(line, sizeof(line), f) != NULL;
    fclose(f);
    char *tail = ok ? strrchr(line, ')') : NULL;
    if (!tail || strlen(tail) < 4) return 0;
    *state = tail[2];
    tail += 4;
    for (int field = 4; field < 22; ++field) {
        tail = strchr(tail, ' ');
        if (!tail) return 0;
        ++tail;
    }
    return strtoull(tail, NULL, 10);
}

static int same_process(struct identity id, char *state) {
    return id.start && proc_start(id.pid, state) == id.start;
}

static struct identity find_nickel(void) {
    struct identity found = {0};
    DIR *proc = opendir("/proc");
    if (!proc) return found;
    struct dirent *entry;
    while ((entry = readdir(proc))) {
        char *end, path[64], exe[256], state;
        long pid = strtol(entry->d_name, &end, 10);
        if (*end || pid <= 1 || pid > INT32_MAX) continue;
        snprintf(path, sizeof(path), "/proc/%ld/exe", pid);
        ssize_t count = readlink(path, exe, sizeof(exe) - 1);
        if (count < 0) continue;
        exe[count] = 0;
        if (strcmp(exe, KOBO_NICKEL_EXE)) continue;
        unsigned long long start = proc_start(pid, &state);
        if (!start || state == 'T' || state == 't' || state == 'Z' || found.pid) {
            found = (struct identity){0};
            break;
        }
        found = (struct identity){pid, start};
    }
    closedir(proc);
    return found;
}

static int session_bus(struct identity nickel, char *address, size_t size) {
    char path[64], env[32768];
    snprintf(path, sizeof(path), "/proc/%d/environ", nickel.pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    ssize_t n = read(fd, env, sizeof(env));
    close(fd);
    if (n <= 0) return 0;
    const char key[] = "DBUS_SESSION_BUS_ADDRESS=";
    for (ssize_t i = 0; i < n;) {
        char *end = memchr(env + i, 0, n - i);
        if (!end) break;
        if (!strncmp(env + i, key, sizeof(key) - 1)) {
            const char *value = env + i + sizeof(key) - 1;
            if (strncmp(value, "unix:", 5) || strchr(value, ';') || strlen(value) >= size)
                return 0;
            strcpy(address, value);
            return 1;
        }
        i = end - env + 1;
    }
    return 0;
}

static int watchdog_call(const char *address, const char *method) {
    pid_t child = fork();
    if (!child) {
        int nullfd = open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (nullfd >= 0) { dup2(nullfd, STDOUT_FILENO); close(nullfd); }
        unsetenv("LD_LIBRARY_PATH");
        setenv("DBUS_SESSION_BUS_ADDRESS", address, 1);
        const char *limit = getenv("KOBO_RUN_LIMIT");
        execl(limit, "run-limit", "3", KOBO_DBUS_SEND, "--session", "--print-reply",
              "--reply-timeout=1500", "--dest=com.kobo.watchdog.Sickel", "/", method, NULL);
        _exit(127);
    }
    int status;
    return child > 0 && waitpid(child, &status, 0) == child &&
           WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int geometry_matches(int fd, const struct fb_var_screeninfo *var,
                            const struct fb_fix_screeninfo *fix) {
    struct fb_var_screeninfo current = {0};
    struct fb_fix_screeninfo fixed = {0};
    return !ioctl(fd, FBIOGET_VSCREENINFO, &current) &&
           !ioctl(fd, FBIOGET_FSCREENINFO, &fixed) &&
           !memcmp(var, &current, sizeof(current)) &&
           fixed.smem_start == fix->smem_start && fixed.smem_len == fix->smem_len &&
           fixed.line_length == fix->line_length;
}

static void lease_guardian(int life, int ready, struct identity nickel, struct identity owner,
                           const char *bus, int fb_fd, uint8_t *fb, uint8_t *snapshot,
                           const struct fb_var_screeninfo *var,
                           const struct fb_fix_screeninfo *fix, const int *refresh_busy) {
    const char *suspend = "com.kobo.watchdog.Sickel.Suspend";
    const char *resume = "com.kobo.watchdog.Sickel.Resume";
    const char *ping = "com.kobo.watchdog.Sickel.Ping";
    char state;
    int paused = 0, saved = 0, normal = 0, restored = 1;
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    if (setsid() < 0) _exit(1);
    if (!watchdog_call(bus, suspend)) goto recover;
    if (!same_process(nickel, &state) || state == 'T' || state == 't' ||
        kill(nickel.pid, SIGSTOP)) goto recover;
    paused = 1;
    int64_t wait_until = now_ms() + 1200;
    while (now_ms() < wait_until && same_process(nickel, &state) && state != 'T')
        usleep(10000);
    if (!same_process(nickel, &state) || state != 'T' ||
        !geometry_matches(fb_fd, var, fix)) goto recover;
    memcpy(snapshot, fb, fix->smem_len);
    saved = 1;
    if (write(ready, "R", 1) != 1) goto recover;
    close(ready);
    ready = -1;
    printf("NICKEL_PAUSED pid=%d heartbeat_ms=%d\n", nickel.pid, LEASE_SILENCE_MS);
    int64_t deadline = now_ms() + LEASE_SILENCE_MS;
    struct pollfd event = {life, POLLIN | POLLHUP, 0};
    int64_t next_ping = 0;
    while (now_ms() < deadline) {
        int result = poll(&event, 1, 500);
        if (result > 0) {
            char command = 0;
            ssize_t n = read(life, &command, 1);
            if (n == 1 && command == 'H') {
                deadline = now_ms() + LEASE_SILENCE_MS;
            } else {
                normal = n == 1 && command == 'D';
                break;
            }
        }
        if (result < 0) break;
        /* Heartbeats keep poll busy, so the Sickel ping runs on its own 5 s schedule (each call is two short-lived processes). */
        if (now_ms() >= next_ping) {
            next_ping = now_ms() + 5000;
            if (!same_process(nickel, &state) || state != 'T' ||
                !watchdog_call(bus, suspend) || !watchdog_call(bus, ping)) break;
        }
    }
recover:
    if (ready >= 0) close(ready);
    if (saved && !normal && same_process(owner, &state) && state != 'Z') {
        kill(owner.pid, SIGKILL);
        int64_t until = now_ms() + 1000;
        while (now_ms() < until && same_process(owner, &state) && state != 'Z')
            usleep(10000);
    }
    if (saved && !normal && __atomic_load_n(refresh_busy, __ATOMIC_ACQUIRE)) {
        /* A bounded FBInk helper may have outlived its bridge. */
        int64_t until = now_ms() + 13000;
        while (now_ms() < until) {
            watchdog_call(bus, suspend);
            watchdog_call(bus, ping);
            usleep(1000000);
        }
    }
    if (saved) {
        watchdog_call(bus, suspend);
        restored = geometry_matches(fb_fd, var, fix);
        if (restored) {
            memcpy(fb, snapshot, fix->smem_len);
            /* Restore memory before handing the screen back. The full refresh
             * is bounded by the same helper used during normal display. */
            restored = refresh();
        }
        printf("NICKEL_FRAMEBUFFER_RESTORE=%s\n", restored ? "PASS" : "FAIL");
    }
    int resumed = !paused;
    if (paused && same_process(nickel, &state) && kill(nickel.pid, SIGCONT) == 0) {
        int64_t until = now_ms() + 1000;
        while (now_ms() < until && same_process(nickel, &state)) {
            if (state != 'T' && state != 't' && state != 'Z') { resumed = 1; break; }
            usleep(10000);
        }
    }
    int watchdog = resumed && watchdog_call(bus, resume);
    printf("NICKEL_RESUME=%s SICKEL_RESUME=%s\n",
           resumed ? "PASS" : "FAIL", watchdog ? "PASS" : "FAIL");
    close(life);
    _exit(saved && restored && resumed && watchdog ? 0 : 1);
}

static int start_lease(struct lease *lease, int fb_fd, uint8_t *fb, uint8_t *snapshot,
                       struct pollfd *inputs, int count, int keys,
                       const struct fb_var_screeninfo *var,
                       const struct fb_fix_screeninfo *fix) {
    char bus[512], state;
    struct identity nickel = find_nickel();
    struct identity owner = {.pid=getpid()};
    owner.start = proc_start(owner.pid, &state);
    if (!nickel.pid || !owner.start || !session_bus(nickel, bus, sizeof(bus))) {
        puts("NICKEL_HANDOFF_PREFLIGHT=FAIL");
        return 0;
    }
    int *refresh_busy = mmap(NULL, sizeof(*refresh_busy), PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (refresh_busy == MAP_FAILED) return 0;
    *refresh_busy = 0;
    int life[2], ready[2];
    if (pipe2(life, O_CLOEXEC)) { munmap(refresh_busy, sizeof(*refresh_busy)); return 0; }
    if (pipe2(ready, O_CLOEXEC)) {
        close(life[0]); close(life[1]);
        munmap(refresh_busy, sizeof(*refresh_busy));
        return 0;
    }
    pid_t child = fork();
    if (child == 0) {
        close(life[1]);
        close(ready[0]);
        for (int i = 0; i < count; ++i) close(inputs[i].fd);
        if (keys >= 0) close(keys);
        lease_guardian(life[0], ready[1], nickel, owner, bus, fb_fd, fb, snapshot, var, fix, refresh_busy);
    }
    close(life[0]);
    close(ready[1]);
    struct pollfd event = {ready[0], POLLIN | POLLHUP, 0};
    char ack;
    int ok = child > 0 && poll(&event, 1, 6000) > 0 &&
             read(ready[0], &ack, 1) == 1 && ack == 'R';
    close(ready[0]);
    if (!ok) {
        close(life[1]);
        if (child > 0) waitpid(child, NULL, 0);
        munmap(refresh_busy, sizeof(*refresh_busy));
        puts("NICKEL_HANDOFF=FAIL");
        return 0;
    }
    *lease = (struct lease){child, life[1], refresh_busy};
    return 1;
}

static int finish_lease(struct lease lease) {
    signal(SIGPIPE, SIG_IGN);
    if (write(lease.life, "D", 1) < 0) perror("display guardian");
    close(lease.life);
    munmap(lease.refresh_busy, sizeof(*lease.refresh_busy));
    int status;
    return waitpid(lease.guardian, &status, 0) == lease.guardian &&
           WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
