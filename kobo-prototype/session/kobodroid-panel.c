/* Kobodroid display panel. Pauses Nickel, shows the loading screen while Android starts, then shows Android below
 * the status bar and routes touch and page keys (session-ui.h). Refresh pacing follows the Kindle bridge. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int64_t clock_ms(clockid_t clock) {
    struct timespec t;
    clock_gettime(clock, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int64_t now_ms(void) { return clock_ms(CLOCK_MONOTONIC); }

static int refresh(void) {
    pid_t child = fork();
    if (child == 0) {
        execl(getenv("KOBO_RUN_LIMIT"), "run-limit", "12", "/mnt/onboard/.adds/koreader/fbink", "-w", "-W", "GCC16",
              "--refresh", NULL);
        _exit(127);
    }
    int status;
    return child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int grab_touch(struct pollfd *input) {
    for (int i = 0; i < 32; ++i) {
        char path[64], name[128] = "";
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
        if (strcmp(name, "Elan Touchscreen")) { close(fd); continue; }
        int grabbed = ioctl(fd, EVIOCGRAB, 1) == 0;
        printf("INPUT_GRAB name=%s path=%s result=%s\n", name, path, grabbed ? "PASS" : "FAIL");
        if (!grabbed) { close(fd); return 0; }
        *input = (struct pollfd){fd, POLLIN, 0};
        return 1;
    }
    return 0;
}

#include "bridge.h"
#include "refresh-region.h"
#include "nickel-lease.h"
#include "session-ui.h"
#include "session-power.h"

enum { WIDTH = PANEL_W, HEIGHT = PANEL_H, ROW = WIDTH * 4 };

static int supported_layout(const struct fb_var_screeninfo *var, const struct fb_fix_screeninfo *fix) {
    return var->xres == 1264 && var->yres == 1680 &&
           (var->rotate == FB_ROTATE_CW || var->rotate == FB_ROTATE_CCW) &&
           !var->xoffset && !var->yoffset && var->bits_per_pixel == 32 &&
           var->red.offset == 0 && var->green.offset == 8 && var->blue.offset == 16 &&
           var->red.length == 8 && var->green.length == 8 && var->blue.length == 8 &&
           fix->line_length == var->xres * 4 && fix->smem_len == fix->line_length * var->yres;
}

/* Android's frame (GUEST_W x GUEST_H, RGB565 or RGBA, the panel's size) into a panel image. */
static int load_frame(const char *path, uint8_t *panel) {
    static uint8_t *raw;
    static uint8_t r5[32], g6[64];
    if (!raw) {
        raw = malloc(12 + GUEST_W * GUEST_H * 4 + 1);
        for (int i = 0; i < 32; ++i) r5[i] = i * 255 / 31;
        for (int i = 0; i < 64; ++i) g6[i] = i * 255 / 63;
    }
    FILE *f = fopen(path, "rb");
    if (!f || !raw) { if (f) fclose(f); return 0; }
    size_t n = fread(raw, 1, 12 + GUEST_W * GUEST_H * 4 + 1, f);
    fclose(f);
    uint32_t header[3];
    if (n < 12) return 0;
    memcpy(header, raw, sizeof(header));
    if (header[0] != GUEST_W || header[1] != GUEST_H || (header[2] != 1 && header[2] != 2 && header[2] != 4))
        return 0;
    unsigned bpp = header[2] == 4 ? 2 : 4;
    if (n != 12 + GUEST_W * GUEST_H * bpp) return 0;
    const uint8_t *src = raw + 12;
    uint8_t *dst = panel;
    for (size_t i = 0; i < (size_t)GUEST_W * GUEST_H; ++i, dst += 4) {
        if (bpp == 2) {
            unsigned rgb = src[0] | (src[1] << 8);
            src += 2;
            dst[0] = r5[(rgb >> 11) & 31];
            dst[1] = g6[(rgb >> 5) & 63];
            dst[2] = r5[rgb & 31];
        } else {
            memcpy(dst, src, 3);
            src += 4;
        }
        dst[3] = 255;
    }
    return 1;
}

/* SurfaceFlinger's framebuffer (gralloc.kindle shared memory), mapped read-only through /proc/<pid>/map_files.
 * Each composed frame is visible as soon as its sequence turns even: no capture process copying it out of
 * /proc/<pid>/mem into a 4 MB file and no file read here (about 0.3 s of CPU per page change). */
#include "kd-frame-protocol.h"
enum { STATUS_BAR_ROWS = 64 };  /* status_bar_height in the framework overlay */
struct direct_frame {
    const uint8_t *map;
    size_t length;
    pid_t pid;
    uint32_t sequence;
    uint16_t *raw, *last;
    unsigned change_bottom;  /* last changed row + 1 since the frame was last shown; 0: none */
};

static pid_t find_surfaceflinger(void) {
    char path[64], name[32];
    for (pid_t pid = 2; pid < 32768; ++pid) {
        snprintf(path, sizeof(path), "/proc/%d/comm", (int)pid);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        ssize_t n = read(fd, name, sizeof(name) - 1);
        close(fd);
        if (n > 0) { name[n] = 0; if (!strcmp(name, "surfaceflinger\n")) return pid; }
    }
    return 0;
}

static int direct_open(struct direct_frame *d) {
    const size_t pixels = (size_t)GUEST_W * GUEST_H * 2, length = kd_frame_offset(pixels) + KD_FRAME_PAGE;
    pid_t pid = find_surfaceflinger();
    if (!pid) return 0;
    char path[96], line[512];
    snprintf(path, sizeof(path), "/proc/%d/maps", (int)pid);
    FILE *maps = fopen(path, "r");
    if (!maps) return 0;
    unsigned long long start = 0, end = 0, s, e;
    char access[5];
    while (fgets(line, sizeof(line), maps))
        if (sscanf(line, "%llx-%llx %4s", &s, &e, access) == 3 && !strcmp(access, "rw-s") &&
            strstr(line, "kindleroid-framebuffer") && e - s == length) { start = s; end = e; break; }
    fclose(maps);
    if (!start) return 0;
    snprintf(path, sizeof(path), "/proc/%d/map_files/%llx-%llx", (int)pid, start, end);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    void *map = mmap(NULL, length, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return 0;
    if (!d->raw) d->raw = malloc(pixels);
    if (!d->last) d->last = calloc(1, pixels);
    if (!d->raw || !d->last) { munmap(map, length); return 0; }
    d->map = map;
    d->length = length;
    d->pid = pid;
    d->sequence = 0;
    printf("FRAME_DIRECT=READY pid=%d\n", (int)pid);
    return 1;
}

/* Copy a committed frame. 1: its content differs from the last one taken (now in d->last); 0: identical, torn or
   not new; -1: the mapping is gone (SurfaceFlinger restarted). SurfaceFlinger re-posts identical frames during a
   page change, so only real changes count. */
static int direct_take(struct direct_frame *d) {
    const size_t pixels = (size_t)GUEST_W * GUEST_H * 2;
    const struct kd_frame_meta *meta = (const void *)(d->map + kd_frame_offset(pixels));
    uint32_t sequence = __atomic_load_n(&meta->sequence, __ATOMIC_ACQUIRE);
    if (sequence == d->sequence || (sequence & 1)) return 0;
    char path[32];
    snprintf(path, sizeof(path), "/proc/%d", (int)d->pid);
    if (access(path, F_OK)) { munmap((void *)d->map, d->length); d->map = NULL; return -1; }
    if (meta->magic != KD_FRAME_MAGIC || meta->width != GUEST_W || meta->height != GUEST_H ||
        meta->stride != GUEST_W * 2) return 0;
    memcpy(d->raw, d->map, pixels);
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (__atomic_load_n(&meta->sequence, __ATOMIC_ACQUIRE) != sequence) return 0;  /* torn: retry next loop */
    d->sequence = sequence;
    if (!memcmp(d->raw, d->last, pixels)) return 0;
    for (unsigned y = GUEST_H; y > d->change_bottom; --y)
        if (memcmp(d->raw + (size_t)(y - 1) * GUEST_W, d->last + (size_t)(y - 1) * GUEST_W, GUEST_W * 2)) {
            d->change_bottom = y;
            break;
        }
    uint16_t *swap = d->last;
    d->last = d->raw;
    d->raw = swap;
    return 1;
}

static void direct_convert(const struct direct_frame *d, uint8_t *panel) {
    static uint8_t r5[32], g6[64];
    if (!r5[31]) {
        for (int i = 0; i < 32; ++i) r5[i] = i * 255 / 31;
        for (int i = 0; i < 64; ++i) g6[i] = i * 255 / 63;
    }
    const uint16_t *src = d->last;
    uint32_t *dst = (uint32_t *)panel;
    for (size_t i = 0; i < (size_t)GUEST_W * GUEST_H; ++i) {
        unsigned rgb = src[i];
        dst[i] = r5[rgb >> 11] | (uint32_t)g6[(rgb >> 5) & 63] << 8 | (uint32_t)r5[rgb & 31] << 16 | 0xff000000u;
    }
}

/* The Kobo's KX122 accelerometer. Its tilt engine interrupts when the device is turned (Nickel keeps it armed);
 * With the page keys on the right, the N428 reports negative Y when held upright.
 * Keep this sensor polarity separate from the panel's pixel and touch transforms.
 * Flat or between orientations: no decision. */
#define TILT_DIR "/sys/devices/platform/1001e000.i2c/i2c-2/2-001e/input/input2/"
static int tilt_rotation(void) {
    char line[96];
    int x, y, z;
    FILE *f = fopen(TILT_DIR "xyz", "r");
    if (!f) return -1;
    int ok = fgets(line, sizeof(line), f) && sscanf(line, "x:%d , y:%d , z:%d", &x, &y, &z) == 3;
    fclose(f);
    if (!ok) return -1;
    int ax = abs(x), ay = abs(y);
    if (ax < 6000 && ay < 6000) return -1;  /* lying flat */
    if (ay * 2 > ax * 3) return y < 0 ? 0 : 2;
    if (ax * 2 > ay * 3) return x > 0 ? 1 : 3;
    return -1;
}

/* Nickel's own auto-sleep timer (AutoSleepMinutes, 60 on this Kobo) kept running while it was paused, so after a
 * long Android session it went to sleep, light off, the moment it resumed. A tap is queued for it before it resumes,
 * so it starts from fresh activity. Kobodroid starts from NickelMenu's main menu, so Nickel returns to its home
 * screen; the tap lands on its Home tab (bottom left), which does nothing there. Raw Elan axes: MT_POSITION_X runs
 * bottom to top, MT_POSITION_Y left to right (see panel_position). */
static void nudge_nickel(void) {
    for (int i = 0; i < 32; ++i) {
        char path[64], name[128] = "";
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_WRONLY | O_CLOEXEC);
        if (fd < 0) continue;
        ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
        if (strcmp(name, "Elan Touchscreen")) { close(fd); continue; }
        const int x = 104, y = 1600;  /* panel portrait coordinates of Nickel's Home tab */
        struct input_event down[] = {
            {.type = EV_ABS, .code = ABS_MT_SLOT, .value = 0},
            {.type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = 4095},
            {.type = EV_ABS, .code = ABS_MT_POSITION_X, .value = 1679 - y},
            {.type = EV_ABS, .code = ABS_MT_POSITION_Y, .value = x},
            {.type = EV_KEY, .code = BTN_TOUCH, .value = 1},
            {.type = EV_ABS, .code = ABS_X, .value = 1679 - y},
            {.type = EV_ABS, .code = ABS_Y, .value = x},
            {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
        }, up[] = {
            {.type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = -1},
            {.type = EV_KEY, .code = BTN_TOUCH, .value = 0},
            {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
        };
        int ok = write(fd, down, sizeof(down)) == (ssize_t)sizeof(down);
        usleep(60000);
        ok = write(fd, up, sizeof(up)) == (ssize_t)sizeof(up) && ok;
        close(fd);
        printf("NICKEL_NUDGE=%s\n", ok ? "PASS" : "FAIL");
        return;
    }
    puts("NICKEL_NUDGE=NO_TOUCHSCREEN");
}

/* Startup phases written by the guest to bridge/phase, with their usual length on the N428. */
static const struct { const char *name; int percent, expected_ms; } phases[] = {
    {"start", 2, 6000}, {"services", 12, 4000}, {"zygote", 20, 8000}, {"framework", 92, 2000}, {"home", 97, 3000},
};

static int read_phase(int current) {
    char name[32] = "";
    FILE *f = fopen("bridge/phase", "r");
    if (f) {
        if (!fgets(name, sizeof(name), f)) name[0] = 0;
        fclose(f);
    }
    name[strcspn(name, "\n")] = 0;
    for (int i = (int)(sizeof(phases) / sizeof(phases[0])) - 1; i > current; --i)
        if (!strcmp(name, phases[i].name)) return i;
    return current;
}

static int phase_percent(int phase, int64_t elapsed) {
    int next = phase + 1 < (int)(sizeof(phases) / sizeof(phases[0])) ? phases[phase + 1].percent : 99;
    int64_t expected = phases[phase].expected_ms;
    if (elapsed > expected * 95 / 100) elapsed = expected * 95 / 100;
    return phases[phase].percent + (int)((next - phases[phase].percent) * elapsed / expected);
}

/* Sleep until the power key (or opening the cover) wakes the Kobo. Android is frozen, the front light is off and
   Wi-Fi is switched off; unplugged, the CPU suspends to RAM, and an unexpected wakeup sleeps again after 15 s. */
static void sleep_cycle(struct power *pw, struct ui *ui, struct lease nickel, int keys, uint8_t *fb, uint8_t *before,
                        uint8_t *drawn, uint8_t *frame, const uint8_t *base, const char *reason) {
    printf("SLEEP_BEGIN reason=%s plugged=%d pagewake=%d\n", reason, plugged_in(), ui->page_wake);
    int frozen = freeze_android(pw, 1);
    memcpy(frame, base, ROW * HEIGHT);
    draw_sleep(frame);
    reconcile(fb, before, drawn, frame, WIDTH, HEIGHT);
    lease_refresh(nickel, 1);
    refresh();
    lease_refresh(nickel, 0);
    /* The hardware value, not the control centre's: the light may have been set since. */
    int light = read_int(LIGHT_DIR "brightness", ui->light), wifi = ui->wifi, cover = key_is_down(keys, COVER_KEY);
    printf("SLEEP_LIGHT value=%d\n", light);
    if (light > 0) write_int(LIGHT_DIR "brightness", 0);
    if (pw->wifi_child > 0) { waitpid(pw->wifi_child, NULL, 0); pw->wifi_child = 0; ui->wifi_busy = 0; }
    if (wifi) power_run("wifi-off");
    power_pressed(pw);
    int page_armed = !page_key_state(keys);
    int aborted = 0;
    while (access("bridge/stop", F_OK)) {
        int woke = 0, suspend_status = 0;
        /* Page keys need an awake CPU, but a closed cover takes priority. */
        if ((!ui->page_wake || cover) && !plugged_in()) {
            long keys_before = irq_count("bd71828_pwrkey"), hall_before = irq_count("Hall");
            int64_t boot_before = clock_ms(CLOCK_BOOTTIME), awake_before = now_ms();
            int status = suspend_status = power_run("suspend");
            int64_t slept_ms = (clock_ms(CLOCK_BOOTTIME) - boot_before) - (now_ms() - awake_before);
            long keys_after = irq_count("bd71828_pwrkey"), hall_after = irq_count("Hall");
            /* Woken by the power key, or by opening the cover; a charger or RTC interrupt sleeps again below. */
            woke = status == 0 && (keys_after > keys_before ||
                                   (hall_after > hall_before && cover && !key_is_down(keys, COVER_KEY)));
            printf("SLEEP_SUSPEND status=%d power_irqs=%ld hall_irqs=%ld woke=%d cover=%d slept_ms=%lld\n",
                   status, keys_after - keys_before, hall_after - hall_before, woke, cover,
                   (long long)(slept_ms > 0 ? slept_ms : 0));
            while (access("bridge/stop", F_OK) && key_is_down(pw->key_fd, KEY_POWER)) usleep(20000);
            pw->key_was_down = 0;
        }
        /* Plugged in (no suspend on MTK) or an unexpected wakeup: wait, then suspend again if still unplugged.
           A suspend the kernel aborted (an early wake event) is retried after 3 s, backing off to a minute while
           it keeps failing, so a stuck wakeup source cannot keep the CPU busy all night. */
        aborted = suspend_status > 0 ? aborted + 1 : 0;
        int64_t retry_ms = aborted ? 3000LL << (aborted < 5 ? aborted - 1 : 4) : 15000;
        if (retry_ms > 60000) retry_ms = 60000;
        int64_t until = now_ms() + retry_ms;
        while (!woke && access("bridge/stop", F_OK)) {
            lease_beat(nickel);
            usleep(aborted ? 100000 : 40000);
            int closed = key_is_down(keys, COVER_KEY);
            unsigned page = page_key_state(keys);
            if (!page) page_armed = 1;
            if (power_pressed(pw) || (cover && !closed)) woke = 1;
            else if (ui->page_wake && !closed && page_armed && (page == 1 || page == 2)) {
                puts("SLEEP_WAKE=page-key");
                woke = 1;
            }
            int just_closed = !cover && closed;
            cover = closed;
            if (!woke && (!ui->page_wake || cover) && !plugged_in() &&
                (just_closed || now_ms() >= until)) break;
        }
        if (woke) break;
    }
    if (light > 0) { light_set_checked(light, "wake"); ui->light = light; }
    if (frozen) freeze_android(pw, 0);
    pw->ignore_keys_until = now_ms() + 800;
    ui->flash_now = 1;
    ui->recompose = ui->immediate = 1;
    ui->last_input = now_ms();
    if (wifi && access("bridge/stop", F_OK)) wifi_start(pw, ui, 1);
    puts("SLEEP_END");
}

int main(void) {
    setbuf(stdout, NULL);
    signal(SIGPIPE, SIG_IGN);
    int lock = open("/tmp/kobodroid-panel.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB)) { perror("panel lock"); return 1; }
    struct fb_fix_screeninfo fix = {0}, current_fix = {0};
    struct fb_var_screeninfo var = {0}, current_var = {0};
    int fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (fd < 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fix) || ioctl(fd, FBIOGET_VSCREENINFO, &var)) {
        perror("framebuffer");
        return 1;
    }
    printf("FRAMEBUFFER %ux%u stride=%u bpp=%u rotation=%u bytes=%u\n", var.xres, var.yres, fix.line_length,
           var.bits_per_pixel, var.rotate, fix.smem_len);
    if (!supported_layout(&var, &fix)) { puts("REFUSED_FRAMEBUFFER_LAYOUT"); return 2; }
    uint8_t *before = malloc(ROW * HEIGHT), *drawn = malloc(ROW * HEIGHT), *frame = malloc(ROW * HEIGHT);
    uint8_t *base = calloc(ROW, HEIGHT), *incoming = calloc(ROW, HEIGHT);
    if (!before || !drawn || !frame || !base || !incoming) return 3;
    uint8_t *fb = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) { perror("framebuffer mmap"); return 3; }
    struct pollfd touch_fd;
    if (!grab_touch(&touch_fd)) return 3;
    int keys = page_key_fd();
    struct ui ui;
    ui_init(&ui);
    struct power pw;
    power_init(&pw);
    struct lease nickel = {0};
    const char *freeze = getenv("KOBO_FREEZE_NICKEL");
    if (freeze && !strcmp(freeze, "1") &&
        !start_lease(&nickel, fd, fb, before, &touch_fd, 1, keys, &var, &fix)) return 6;

    /* Loading screen: one flashing full refresh, then progress-bar updates. */
    lease_refresh(nickel, 1);
    memcpy(before, fb, ROW * HEIGHT);
    int percent = phases[0].percent, phase = 0;
    draw_loading(frame, percent, LOADING_START);
    memcpy(fb, frame, ROW * HEIGHT);
    memcpy(drawn, frame, ROW * HEIGHT);
    int shown = refresh();
    lease_refresh(nickel, 0);
    printf("LOADING_SCREEN=%s\n", shown ? "PASS" : "FAIL");
    int64_t phase_started = now_ms(), next_beat = 0, next_check = 0;
    pid_t pending_refresh = 0;
    int display_error = 0, android = 0;
    struct touch_state touch = {0};
    while (shown && access("bridge/stop", F_OK)) {
        int64_t t = now_ms();
        if (t >= next_beat) { next_beat = t + 1000; lease_beat(nickel); }
        struct input_event drop;
        while (read(touch_fd.fd, &drop, sizeof(drop)) == sizeof(drop)) {}
        if (pending_refresh > 0) {
            int status;
            pid_t done = waitpid(pending_refresh, &status, WNOHANG);
            if (done == pending_refresh) {
                pending_refresh = 0;
                lease_refresh(nickel, 0);
            }
        }
        if (t >= next_check) {
            next_check = t + 250;
            if (load_frame("bridge/frame", base)) { android = 1; break; }
            int now_phase = read_phase(phase);
            if (now_phase != phase) {
                phase = now_phase;
                phase_started = t;
                printf("LOADING_PHASE %s at=%lld\n", phases[phase].name, (long long)t);
            }
            int p = phase_percent(phase, t - phase_started);
            if (p > percent && !pending_refresh) {
                percent = p;
                draw_loading_bar(frame, percent);
                struct damage changed = reconcile(fb, before, drawn, frame, WIDTH, HEIGHT);
                if (changed.right) {
                    lease_refresh(nickel, 1);
                    pending_refresh = submit_region(changed, var.rotate, frame, 0, 0);
                }
            }
        }
        usleep(20000);
    }
    if (pending_refresh > 0) waitpid(pending_refresh, NULL, 0);
    pending_refresh = 0;
    if (!android) {
        /* Android did not start: say so briefly, then the guardian returns the screen to Kobo. */
        puts("LOADING_FAILED=1");
        draw_loading(frame, percent, LOADING_FAILED);
        reconcile(fb, before, drawn, frame, WIDTH, HEIGHT);
        lease_refresh(nickel, 1);
        refresh();
        lease_refresh(nickel, 0);
        sleep(2);
        goto restore;
    }
    printf("LOADING_DONE at=%lld\n", (long long)now_ms());

    /* Session. frame = base (Android) + the control centre when open. */
    enum { SETTLE_QUIET_MS = 60, SETTLE_MAX_MS = 300 };
    struct damage bands[4];
    int band_count = 0, band_next = 0;
    struct stat displayed = {0};
    stat("bridge/frame", &displayed);
    ui_poll_status(&ui);
    pw.wifi_initial = ui.wifi;
    int cover_closed = key_is_down(keys, COVER_KEY), was_plugged = plugged_in();
    printf("COVER_STATE=%d WIFI_INITIAL=%d\n", cover_closed, ui.wifi);
    ui.recompose = 1;
    ui.flash_now = 1;
    int64_t next_frame = 0, next_status = 0, geometry_at = 0, repair_at = 0, dark_since = 0;
    int64_t refresh_started = 0, changed_at = 0, settle_started = 0;
    int64_t tilt_next = 0;
    long tilt_irqs = -1;
    int tilt_pending = 0, tilt_seen = -2, tilt_want = -1, tilt_tries = 0;
    int64_t tilt_retry_at = 0;
    struct direct_frame direct = {0};
    int64_t direct_retry = 0, direct_seen_at = 0, direct_first = 0;
    unsigned previous_key = 0, key_pressed = 0;
    int key_armed = 0, dirty = 0, ghost = 0, flashes = 0, updates = 0;
    ui.last_input = now_ms();
    power_pressed(&pw);  /* presses during the loading screen do not count */
    while (access("bridge/stop", F_OK)) {
        int64_t t = now_ms();
        if (t >= next_beat) { next_beat = t + 1000; lease_beat(nickel); }
        /* Sleep: power key, closing the cover (on its transition only), or the idle timer. */
        const char *sleep_reason = NULL;
        int cover_now = key_is_down(keys, COVER_KEY);
        if (cover_now != cover_closed) { printf("COVER_STATE=%d\n", cover_now); cover_closed = cover_now; if (cover_now) sleep_reason = "cover"; }
        if (power_pressed(&pw)) sleep_reason = "power";
        if (ui.sleep_request) { ui.sleep_request = 0; sleep_reason = "request"; }
        stream_connect(&ui);
        if (ui.idle_minutes && t - ui.last_input >= (int64_t)ui.idle_minutes * 60000) sleep_reason = "idle";
        if (sleep_reason && !pending_refresh) {
            if (ui.cc_open) cc_set_open(&ui, 0);
            sleep_cycle(&pw, &ui, nickel, keys, fb, before, drawn, frame, base, sleep_reason);
            cover_closed = key_is_down(keys, COVER_KEY);
            key_armed = 0;
            key_pressed = 0;
            next_beat = 0;
            continue;
        }
        /* Auto-rotate: on a tilt interrupt, read the orientation twice 400 ms apart and turn Android's display
           when both agree (SessionInput applies it). An app that fixes its orientation still wins. */
        if (ui.auto_rotate && ui.input_fd >= 0 && t >= tilt_next) {  /* once SessionInput listens */
            tilt_next = t + 400;
            long count = irq_count("kx122");
            if (count != tilt_irqs || ui.tilt_recheck) { tilt_irqs = count; ui.tilt_recheck = 0; tilt_pending = 2; tilt_seen = -2; }
            if (tilt_pending) {
                int r = tilt_rotation();
                if (r == tilt_seen && r >= 0) {
                    tilt_pending = 0;
                    if (r != view_rot) {
                        printf("TILT rotation=%d view=%d\n", r, view_rot);
                        stream_send(&ui, 1, "r %d\n", r);
                        tilt_want = r;
                        tilt_tries = 1;
                        tilt_retry_at = t + 2000;
                    }
                } else if (--tilt_pending == 0 && r >= 0) tilt_pending = 1;  /* still moving: keep looking */
                tilt_seen = r;
            }
        }
        /* Android sometimes drops a rotation request; ask again (twice at most). An app that fixed its
           orientation keeps it, and then this gives up. */
        if (!ui.auto_rotate) tilt_want = -1;
        if (tilt_want >= 0 && t >= tilt_retry_at) {
            if (tilt_want == view_rot || tilt_tries >= 3) tilt_want = -1;
            else {
                printf("TILT retry rotation=%d view=%d\n", tilt_want, view_rot);
                stream_send(&ui, 1, "r %d\n", tilt_want);
                ++tilt_tries;
                tilt_retry_at = t + 2000;
            }
        }
        wifi_poll(&pw, &ui);
        if (ui.wifi_request >= 0) { wifi_start(&pw, &ui, ui.wifi_request); ui.wifi_request = -1; }
        read_touch(touch_fd.fd, var.rotate, &touch, &ui);
        ui_tick(&ui);
        if (ui.exit) { puts("ANDROID_HOME_EXIT_REQUEST=1"); break; }
        if (keys >= 0) {
            unsigned state = page_key_state(keys);
            if (!state) {
                if (key_pressed) stream_key(&ui, key_pressed == 1 ? 92 : 93);
                key_pressed = 0;
                key_armed = 1;
            } else if (state > 2 || (previous_key && state != previous_key)) {
                key_armed = 0;
                key_pressed = 0;
            } else if (key_armed) {
                key_pressed = state;
            }
            if (state) ui.last_input = t;
            previous_key = state;
        }
        if (pending_refresh > 0) {
            int status;
            pid_t done = waitpid(pending_refresh, &status, WNOHANG);
            if (done == pending_refresh) {
                pending_refresh = 0;
                lease_refresh(nickel, 0);
                if (!WIFEXITED(status) || WEXITSTATUS(status)) { display_error = 1; break; }
                ++updates;
                printf("PANEL_REFRESH_MS=%lld at=%lld\n", (long long)(now_ms() - refresh_started), (long long)now_ms());
                /* Further bands of the same frame follow at once. */
                if (band_next < band_count) {
                    lease_refresh(nickel, 1);
                    refresh_started = now_ms();
                    pending_refresh = submit_region(bands[band_next++], var.rotate, frame, 0, 0);
                    if (pending_refresh < 0) { display_error = 1; break; }
                }
            } else if (done < 0) {
                display_error = 1;
                break;
            }
        }
        int fresh = 0;
        if (direct.map) {
            /* A page change arrives as several posts, most of them identical. Show the content once it has not
               changed for the settle time (or the settle limit passed): it is already settled, so it refreshes at
               once, and in-between images are never converted. */
            int taken = direct_take(&direct);
            if (taken < 0) { unlink("bridge/direct"); puts("FRAME_DIRECT=LOST"); direct_first = 0; }
            else if (taken) { direct_seen_at = t; if (!direct_first) direct_first = t; printf("FRAME_SEEN at=%lld\n", (long long)now_ms()); }
            /* A change only in the status bar is usually its first step: opening an app's menu shows the bar
               about 170 ms before the app draws the menu. Wait for the rest so both appear in one update. */
            int bar_only = view_rot == 0 && direct.change_bottom <= STATUS_BAR_ROWS;  /* the bar's rows, in portrait */
            int ime_fast = ui.ime_visible && !ui.cc_open && !ui.usb_prompt;
            int64_t quiet = ime_fast ? 20 : bar_only ? 250 : SETTLE_QUIET_MS;
            int64_t limit = ime_fast ? 90 : bar_only ? 400 : SETTLE_MAX_MS;
            if (direct_first && (t - direct_seen_at >= quiet || t - direct_first >= limit)) {
                direct.change_bottom = 0;
                direct_convert(&direct, incoming);
                printf("FRAME_TAKEN at=%lld\n", (long long)now_ms());
                direct_first = 0;
                fresh = 1;
                ui.immediate = 1;
            }
        } else if (t >= direct_retry) {
            direct_retry = t + 1000;
            if (direct_open(&direct)) close(open("bridge/direct", O_CREAT | O_WRONLY | O_CLOEXEC, 0644));
        }
        if (t >= next_frame) {
            next_frame = t + 50;
            struct stat current;
            if (!direct.map && !stat("bridge/frame", &current) &&
                (current.st_ino != displayed.st_ino || current.st_mtim.tv_sec != displayed.st_mtim.tv_sec ||
                 current.st_mtim.tv_nsec != displayed.st_mtim.tv_nsec) &&
                load_frame("bridge/frame", incoming)) {
                displayed = current;
                fresh = 1;
            }
        }
        if (fresh) {
            if (mostly_dark(incoming, WIDTH, HEIGHT)) {
                if (!dark_since) dark_since = t;
            } else dark_since = 0;
            /* Keep the last Android image during a brief black transition. */
            if (!dark_since || t - dark_since >= 600) {
                uint8_t *swap = base;
                base = incoming;
                incoming = swap;
                ui.recompose = 1;
            }
        }
        if (t >= geometry_at) {
            geometry_at = t + 250;
            if (ioctl(fd, FBIOGET_VSCREENINFO, &current_var) || memcmp(&var, &current_var, sizeof(var)) ||
                ioctl(fd, FBIOGET_FSCREENINFO, &current_fix) || fix.smem_start != current_fix.smem_start ||
                fix.smem_len != current_fix.smem_len || fix.line_length != current_fix.line_length) break;
        }
        if (t >= next_status) {
            next_status = t + 1000;
            if (ui_poll_status(&ui) && ui.cc_open) ui.recompose = 1;
            int plugged = plugged_in();
            if (plugged != was_plugged) {
                char type[32] = "";
                FILE *c = fopen("/sys/class/power_supply/bd71827_bat/charger_type", "r");
                if (c) { if (!fgets(type, sizeof(type), c)) type[0] = 0; fclose(c); }
                type[strcspn(type, "\n")] = 0;
                printf("USB_PLUG plugged=%d charger_type=%s\n", plugged, type);
                /* A wall charger (DCP) only charges; anything else may be a computer. */
                ui.usb_prompt = plugged && strcmp(type, "DCP");
                if (ui.usb_prompt && ui.cc_open) cc_set_open(&ui, 0);
                ui.recompose = ui.immediate = 1;
                was_plugged = plugged;
            }
        }
        ui_channel(&ui, 0);
        if (ui.recompose) {
            memcpy(frame, base, ROW * HEIGHT);
            if (ui.usb_prompt) draw_usb(frame);
            if (ui.cc_open) draw_cc(frame, &ui);
            ui.recompose = 0;
            dirty = 1;
            changed_at = t;
            if (!settle_started) settle_started = t;
        }
        if (pending_refresh) goto idle;
        if (dirty) {
            if (!ui.immediate && t - changed_at < SETTLE_QUIET_MS && t - settle_started < SETTLE_MAX_MS) goto idle;
        } else if (nickel.guardian || t < repair_at) {
            goto idle;
        }
        repair_at = t + 250;
        int was_dirty = dirty;
        dirty = 0;
        settle_started = 0;
        ui.immediate = 0;
        int64_t reconcile_start = now_ms();
        band_count = was_dirty ? reconcile_bands_written(fb, drawn, frame, WIDTH, HEIGHT, bands, 4)
                               : reconcile_bands(fb, before, drawn, frame, WIDTH, HEIGHT, bands, 4);
        if (band_count) printf("RECONCILE_MS=%lld\n", (long long)(now_ms() - reconcile_start));
        band_next = 1;
        struct damage changed = band_count ? bands[0] : (struct damage){0};
        /* Separate areas (WeRead's top and bottom menu bars, a field and the keyboard) are one non-flashing update
           of their union, so they appear together; refreshed one after another each waited for the last. Only the
           area that really changed decides whether this counts as a page change. */
        unsigned covered = 0;
        for (int i = 0; i < band_count; ++i) {
            covered += (bands[i].right - bands[i].left) * (bands[i].bottom - bands[i].top);
            if (bands[i].left < changed.left) changed.left = bands[i].left;
            if (bands[i].right > changed.right) changed.right = bands[i].right;
            changed.bottom = bands[i].bottom;
            changed.color |= bands[i].color;
        }
        int ime_band = -1;
        if (ui.ime_visible && !ui.cc_open && !ui.usb_prompt && !(view_rot & 1) && band_count > 1) {
            /* A coloured editor cursor and the monochrome candidate strip can change together. */
            for (int i = 0; i < band_count; ++i) {
                struct damage b = bands[i];
                int keyboard = view_rot == 0 ? b.top >= HEIGHT * 3 / 5 : b.bottom <= HEIGHT * 2 / 5;
                if (keyboard && !b.color && (b.right - b.left) * (b.bottom - b.top) <= WIDTH * HEIGHT / 6) {
                    ime_band = i;
                    break;
                }
            }
        }
        if (ime_band < 0) band_count = 0;
        if (ui.flash_now) {
            /* Requested from the control centre, and once when Android first appears. */
            ui.flash_now = 0;
            ghost = 0;
            ++flashes;
            band_count = 0;
            changed = (struct damage){0, 0, WIDTH, HEIGHT, 1};
            lease_refresh(nickel, 1);
            refresh_started = now_ms();
            pending_refresh = submit_region(changed, var.rotate, frame, 1, 1);
        } else if (ime_band >= 0) {
            struct damage candidate = bands[ime_band];
            memmove(bands + 1, bands, (size_t)ime_band * sizeof(bands[0]));
            bands[0] = candidate;
            lease_refresh(nickel, 1);
            refresh_started = now_ms();
            pending_refresh = submit_region_mode(candidate, var.rotate, frame, 0, 0, 1);
        } else if (changed.right) {
            unsigned area = covered;
            int large = area * 2 >= WIDTH * HEIGHT;
            /* Like Kobo's reader: every flash_every screens of large changes, the next one flashes. */
            int flash = large && ui.flash_every && ghost >= ui.flash_every * 100;
            if (flash) { ghost = 0; ++flashes; }
            else if (large) ghost += (int)((uint64_t)area * 100 / (WIDTH * HEIGHT));
            lease_refresh(nickel, 1);
            refresh_started = now_ms();
            unsigned box = (changed.right - changed.left) * (changed.bottom - changed.top);
            int fast = ui.ime_visible && !ui.cc_open && !ui.usb_prompt && box <= WIDTH * HEIGHT / 6;
            pending_refresh = submit_region_mode(changed, var.rotate, frame, large, flash, fast);
        }
        if (pending_refresh < 0) { display_error = 1; break; }
    idle:
        /* Page keys can only be polled (Nickel holds their grab), so idle still wakes every 40 ms. */
        poll(&touch_fd, 1, pending_refresh || dirty || direct_first || touch.reported || ui.mode != G_IDLE ? 10 : 40);
    }
    if (pending_refresh > 0) {
        int status;
        if (waitpid(pending_refresh, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status)) display_error = 1;
        else ++updates;
    }
    printf("ANDROID_HOME_FRAME_UPDATES=%d flashes=%d\n", updates, flashes);
restore:
    ui_restore_light(&ui);
    if (pw.wifi_child > 0) waitpid(pw.wifi_child, NULL, 0);
    ui_poll_status(&ui);
    /* Kobo gets Wi-Fi back as it was when Android started; host.sh switches it on after the panel exits. */
    if (pw.wifi_initial && !ui.wifi) { int w = open("bridge/wifi-restore", O_CREAT | O_WRONLY | O_CLOEXEC, 0600); if (w >= 0) close(w); }
    if (pw.key_fd >= 0) close(pw.key_fd);
    lease_refresh(nickel, 0);
    if (keys >= 0) close(keys);
    if (ui.input_fd >= 0) close(ui.input_fd);
    if (nickel.guardian) {
        close(touch_fd.fd);
        puts("INPUT_RELEASE=PASS");
        nudge_nickel();
        int recovered = finish_lease(nickel);
        return shown && !display_error && recovered ? 0 : 5;
    }
    if (ioctl(fd, FBIOGET_FSCREENINFO, &current_fix) || ioctl(fd, FBIOGET_VSCREENINFO, &current_var) ||
        memcmp(&var, &current_var, sizeof(var)) || fix.smem_len != current_fix.smem_len) {
        puts("RESTORE=SKIP_GEOMETRY_CHANGED");
        return 4;
    }
    memcpy(fb, before, ROW * HEIGHT);
    int refreshed = refresh();
    printf("RESTORE=%s\n", refreshed ? "PASS" : "FAIL");
    close(touch_fd.fd);
    puts("INPUT_RELEASE=PASS");
    return shown && !display_error && refreshed ? 0 : 5;
}
