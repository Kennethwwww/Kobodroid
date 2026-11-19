/* Sleep and Wi-Fi for the Kobodroid panel.
 * - Power key: bd71828-pwrkey (KEY_POWER). Nickel holds its grab, so its state is polled; across a suspend the
 *   PMIC's per-source interrupt counts tell a power-key wakeup from a charger or RTC one.
 * - Sleep cover: KEY_H on gpio-keys, whose grab Nickel holds, so its state is polled like the page keys.
 * - Android is frozen through the runner's freezer cgroup while asleep, so it uses no CPU.
 * - power.sh does the Kobo-specific Wi-Fi switching and suspend (after KOReader's MTK sequence). */
#include <glob.h>

enum { COVER_KEY = 35 };

struct power {
    int key_fd;
    char freezer[256];
    int64_t ignore_keys_until;
    int key_was_down;
    pid_t wifi_child;
    int wifi_child_target, wifi_initial;
};

static int key_is_down(int fd, unsigned code) {
    unsigned long bits[KEY_MAX / (8 * sizeof(unsigned long)) + 1] = {0};
    if (fd < 0 || ioctl(fd, EVIOCGKEY(sizeof(bits)), bits) < 0) return 0;
    return (bits[code / (8 * sizeof(unsigned long))] >> (code % (8 * sizeof(unsigned long)))) & 1;
}

static void power_init(struct power *p) {
    memset(p, 0, sizeof(*p));
    p->key_fd = -1;
    for (int i = 0; i < 32 && p->key_fd < 0; ++i) {
        char path[64], name[64] = "";
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
        if (strcmp(name, "bd71828-pwrkey")) { close(fd); continue; }
        printf("POWER_KEY path=%s\n", path);
        p->key_fd = fd;
    }
}

/* 1 when the power key was pressed and released (the panel polls at least every 40 ms). */
static int power_pressed(struct power *p) {
    int down = key_is_down(p->key_fd, KEY_POWER), released = p->key_was_down && !down;
    p->key_was_down = down;
    return released && now_ms() >= p->ignore_keys_until;
}

/* Sum of /proc/interrupts counts for every line naming source. */
static long irq_count(const char *source) {
    FILE *f = fopen("/proc/interrupts", "r");
    char line[256];
    long total = 0;
    if (!f) return -1;
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, source)) continue;
        char *s = strchr(line, ':');
        for (s = s ? s + 1 : line;;) {
            char *end;
            long n = strtol(s, &end, 10);
            if (end == s) break;
            total += n;
            s = end;
        }
    }
    fclose(f);
    return total;
}

static int freeze_android(struct power *p, int frozen) {
    if (!p->freezer[0]) {
        /* The runner's private freezer mount, reached through its mount namespace. */
        glob_t g;
        if (!glob("/proc/[0-9]*/root/tmp/kobo-core-cg-*/session-*/freezer.state", 0, NULL, &g) && g.gl_pathc) {
            snprintf(p->freezer, sizeof(p->freezer), "%s", g.gl_pathv[0]);
            printf("ANDROID_FREEZER=%s\n", p->freezer);
        }
        globfree(&g);
    }
    FILE *f = p->freezer[0] ? fopen(p->freezer, "w") : NULL;
    if (!f) return 0;
    int ok = fputs(frozen ? "FROZEN\n" : "THAWED\n", f) >= 0;
    ok = !fclose(f) && ok;
    printf("ANDROID_%s=%s\n", frozen ? "FROZEN" : "THAWED", ok ? "PASS" : "FAIL");
    return ok;
}

static pid_t power_spawn(const char *command) {
    pid_t pid = fork();
    if (pid == 0) {
        const char *base = getenv("KOBODROID_BASE");
        char sh[256], script[256];
        snprintf(sh, sizeof(sh), "%s/busybox", base ? base : ".");
        snprintf(script, sizeof(script), "%s/power.sh", base ? base : ".");
        execl(sh, "sh", script, command, NULL);
        _exit(127);
    }
    return pid;
}

static int power_run(const char *command) {
    pid_t pid = power_spawn(command);
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) != pid) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int plugged_in(void) { return read_int("/sys/class/power_supply/bd71827_ac/online", 0) != 0; }

/* Wi-Fi changes run in the background; the UI shows them as busy until they finish. */
static void wifi_start(struct power *p, struct ui *ui, int on) {
    if (p->wifi_child > 0) return;
    p->wifi_child = power_spawn(on ? "wifi-on" : "wifi-off");
    p->wifi_child_target = on;
    ui->wifi_busy = on ? 1 : -1;
    ui->recompose = ui->immediate = 1;
    printf("WIFI_REQUEST on=%d\n", on);
}

static void wifi_poll(struct power *p, struct ui *ui) {
    if (p->wifi_child <= 0) return;
    int status;
    if (waitpid(p->wifi_child, &status, WNOHANG) != p->wifi_child) return;
    p->wifi_child = 0;
    ui->wifi_busy = 0;
    ui_poll_status(ui);
    ui->recompose = ui->immediate = 1;
    ui_write_state(ui);
    printf("WIFI_DONE on=%d status=%d connected=%d\n", p->wifi_child_target, WIFEXITED(status) ? WEXITSTATUS(status) : -1,
           ui->wifi);
}
