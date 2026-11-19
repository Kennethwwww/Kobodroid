/* Fixed-size transport for the Kobo Android probe. */
#define GUEST_W 1264U
#define GUEST_H 1680U
#define PANEL_W 1264U
#define PANEL_H 1680U

static int load_android_frame(const char *path, uint8_t *pixels) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t header[3];
    int ok = fread(header, sizeof(header), 1, f) == 1 &&
             header[0] == GUEST_W && header[1] == GUEST_H &&
             (header[2] == 1 || header[2] == 2 || header[2] == 4);
    unsigned bpp = ok && header[2] == 4 ? 2 : 4;
    size_t length = GUEST_W * GUEST_H * bpp;
    uint8_t *raw = malloc(length);
    if (ok && raw && fread(raw, 1, length, f) == length && fgetc(f) == EOF) {
        for (unsigned y = 0; y < PANEL_H - 50; ++y) {
            for (unsigned x = 0; x < PANEL_W; ++x) {
                const uint8_t *src = raw + bpp * ((y * GUEST_H / (PANEL_H - 50)) * GUEST_W + x * GUEST_W / PANEL_W);
                uint8_t *dst = pixels + 4 * (y * PANEL_W + x);
                if (bpp == 2) {
                    unsigned rgb = src[0] | (src[1] << 8);
                    dst[0] = ((rgb >> 11) & 31) * 255 / 31;
                    dst[1] = ((rgb >> 5) & 63) * 255 / 63;
                    dst[2] = (rgb & 31) * 255 / 31;
                } else {
                    memcpy(dst, src, 3);
                }
                dst[3] = 255;
            }
        }
    } else {
        ok = 0;
    }
    free(raw);
    fclose(f);
    return ok;
}

static int send_touch(struct pollfd inputs[2], int count, int rotation, unsigned step) {
    int x = -1, y = -1, down = 0;
    int64_t end = now_ms() + 35000;
    while (now_ms() < end && access("bridge/stop", F_OK)) {
        if (poll(inputs, count, 200) <= 0) continue;
        for (int i = 0; i < count; ++i) {
            if (!(inputs[i].revents & POLLIN)) continue;
            struct input_event ev;
            while (read(inputs[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
                if (now_ms() >= end) return 0;
                if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID && ev.value >= 0) {
                    x = y = -1;
                    down = 1;
                } else if (ev.type == EV_ABS && ev.code == ABS_MT_POSITION_X) {
                    y = ev.value;
                } else if (ev.type == EV_ABS && ev.code == ABS_MT_POSITION_Y) {
                    x = ev.value;
                } else if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID &&
                           ev.value < 0 && down) {
                    down = 0;
                    if (x < 0 || x >= 1264 || y < 0 || y >= 1680) continue;
                    y = 1679 - y;
                    if (rotation == FB_ROTATE_CCW) { x = 1263 - x; y = 1679 - y; }
                    x -= (1264 - PANEL_W) / 2;
                    y -= (1680 - PANEL_H) / 2;
                    if (x < 0 || y < 0 || x >= (int)PANEL_W || y >= (int)PANEL_H) continue;
                    unsigned gx = x * GUEST_W / PANEL_W, gy = y * GUEST_H / (PANEL_H - 50);
                    char path[32];
                    snprintf(path, sizeof(path), "bridge/tap.%u", step);
                    FILE *f = fopen("bridge/tap.tmp", "w");
                    if (!f) return 0;
                    fprintf(f, "%u %u\n", gx, gy);
                    if (fclose(f) || rename("bridge/tap.tmp", path)) return 0;
                    printf("PHYSICAL_TAP step=%u x=%u y=%u\n", step, gx, gy);
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* Poll the kernel's device-wide bitmap, including while another client
 * receives the exclusive event stream. Only the two page buttons qualify. */
static int page_key_fd(void) {
    for (int i = 0; i < 32; ++i) {
        char path[64], name[128] = "";
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0 &&
            !strcmp(name, "gpio-keys")) return fd;
        close(fd);
    }
    return -1;
}

static unsigned page_key_state(int fd) {
    /* Kernel unsigned-long bitmap ABI on this little-endian ARM device. */
    unsigned char bits[(KEY_MAX + 8) / 8] = {0};
    if (ioctl(fd, EVIOCGKEY(sizeof(bits)), bits) < 0) return 4;
    return ((bits[193 / 8] >> (193 % 8)) & 1) |
           (((bits[194 / 8] >> (194 % 8)) & 1) << 1);
}

static unsigned send_page_key(int fd, unsigned step, unsigned exclude) {
    int64_t end = now_ms() + 35000;
    unsigned pressed = 0;
    int armed = 0;
    while (now_ms() < end && access("bridge/stop", F_OK)) {
        unsigned state = page_key_state(fd);
        if (state == 4) { perror("EVIOCGKEY"); return 0; }
        if (state == 0) {
            if (pressed) {
                unsigned code = pressed == 1 ? 92 : 93;
                char path[32];
                snprintf(path, sizeof(path), "bridge/key.%u", step);
                FILE *file = fopen("bridge/key.tmp", "w");
                if (!file) return 0;
                fprintf(file, "%u\n", code);
                if (fclose(file) || rename("bridge/key.tmp", path)) return 0;
                printf("PHYSICAL_PAGE_KEY step=%u linux=%u android=%u\n",
                       step, pressed == 1 ? 193 : 194, code);
                return pressed;
            }
            armed = 1;
        } else if (!armed || state == 3 || state == exclude || (pressed && state != pressed)) {
            pressed = 0;
            armed = 0;
        } else {
            pressed = state;
        }
        usleep(15000);
    }
    puts("PAGE_KEY_TIMEOUT");
    return 0;
}
