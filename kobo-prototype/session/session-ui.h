/* Kobodroid session UI, drawn by the panel around Android:
 * - edge gestures, with the thresholds of the Kindle bridge: swipe up from the bottom edge = Home, inward from a
 *   side = Back, down from the top edge = control centre;
 * - a control centre in the style of Kobo's quick settings: status, brightness and natural light sliders, and icon
 *   tiles for Wi-Fi, screen refresh, full-refresh frequency and Return to Kobo (behind a confirmation);
 * - the settings channel with the Kobodroid launcher (bridge/ui), which also draws Android's own status bar;
 * - the loading screen shown while Android starts.
 * Every other touch streams live to SessionInput through the bridge/input FIFO ("d|m|u x y", "k code"), so Android
 * itself recognizes drags, long presses and flings. Android fills the whole panel. */
#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <sys/socket.h>

#define ZH(s) s
#include "panel-font.h"
enum { FONT_L = 0, FONT_S = 1, FONT_XL = 2 };
enum { CC_H = 640 };

/* ---- Drawing on the RGBA panel image. Coordinates are in Android's current orientation (view_rot, Android's
   Surface rotation, reported by the launcher): drawings and touches follow the screen when it is turned. */
static int view_rot, view_ox;
#define VIEW_W ((view_rot & 1) ? (int)PANEL_H : (int)PANEL_W)
#define VIEW_H ((view_rot & 1) ? (int)PANEL_W : (int)PANEL_H)

static void px_fill(uint8_t *p, int x, int y, int w, int h, uint8_t v) {
    x += view_ox;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > VIEW_W) w = VIEW_W - x;
    if (y + h > VIEW_H) h = VIEW_H - y;
    if (w <= 0 || h <= 0) return;
    /* To the panel's portrait rectangle. Rotation 1 puts Android's top along the right edge. */
    int X = x, Y = y, Wd = w, Ht = h;
    if (view_rot == 1) { X = (int)PANEL_W - y - h; Y = x; Wd = h; Ht = w; }
    else if (view_rot == 2) { X = (int)PANEL_W - x - w; Y = (int)PANEL_H - y - h; }
    else if (view_rot == 3) { X = y; Y = (int)PANEL_H - x - w; Wd = h; Ht = w; }
    x = X; y = Y; w = Wd; h = Ht;
    for (int r = y; r < y + h; ++r) {
        uint8_t *d = p + 4 * ((size_t)r * PANEL_W + x);
        for (int c = 0; c < w; ++c, d += 4) { d[0] = d[1] = d[2] = v; d[3] = 255; }
    }
}

static void px_box(uint8_t *p, int x, int y, int w, int h, int t, uint8_t v) {
    px_fill(p, x, y, w, t, v);
    px_fill(p, x, y + h - t, w, t, v);
    px_fill(p, x, y, t, h, v);
    px_fill(p, x + w - t, y, t, h, v);
}

static void px_disc(uint8_t *p, int cx, int cy, int r, uint8_t v) {
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            if (dx * dx + dy * dy <= r * r) px_fill(p, cx + dx, cy + dy, 1, 1, v);
}

static uint32_t utf8_next(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t c = *p++;
    int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
    if (more) c &= 0x3f >> more;
    while (more-- && (*p & 0xc0) == 0x80) c = (c << 6) | (*p++ & 0x3f);
    *s = (const char *)p;
    return c;
}

static const struct kd_glyph *glyph(int cls, uint32_t cp) {
    for (size_t i = 0; i < sizeof(kd_glyphs) / sizeof(kd_glyphs[0]); ++i)
        if (kd_glyphs[i].cp == cp && kd_glyphs[i].cls == cls) return &kd_glyphs[i];
    return NULL;
}

static int text_width(const char *s, int cls) {
    int w = 0;
    while (*s) {
        const struct kd_glyph *g = glyph(cls, utf8_next(&s));
        w += g ? g->w : kd_glyph_h[cls] / 2;
    }
    return w;
}

/* align: 0 = x is the left edge, 1 = the centre, 2 = the right edge; y is the top of the line. */
static void text_at(uint8_t *p, int x, int y, const char *s, int cls, int align, uint8_t v) {
    if (align) x -= text_width(s, cls) / (align == 1 ? 2 : 1);
    int h = kd_glyph_h[cls];
    while (*s) {
        const struct kd_glyph *g = glyph(cls, utf8_next(&s));
        if (!g) { x += h / 2; continue; }
        int stride = (g->w + 7) / 8;
        const unsigned char *b = kd_glyph_bits + g->off;
        for (int r = 0; r < h; ++r)
            for (int c = 0; c < g->w; ++c)
                if (b[r * stride + c / 8] & (0x80 >> (c % 8))) px_fill(p, x + c, y + r, 1, 1, v);
        x += g->w;
    }
}

/* ---- Host state shown and changed by the UI. */
#define LIGHT_DIR "/sys/class/backlight/lm3630a_led/"

static int read_int(const char *path, int fallback) {
    FILE *f = fopen(path, "r");
    int v = fallback;
    if (f) {
        if (fscanf(f, "%d", &v) != 1) v = fallback;
        fclose(f);
    }
    return v;
}

static void write_int(const char *path, int v) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "%d\n", v);
    fclose(f);
}

struct ui {
    int cc_open, recompose, immediate, flash_now, flash_every, idle_minutes, exit;
    int wifi_busy, wifi_request;  /* wifi_request: -1 none, 0 off, 1 on */
    int usb_prompt, sleep_request, page_wake, ime_visible;
    int light, light0, light_max, warm, warm0, warm_max;
    int battery, charging, wifi, minute;
    char ip[INET_ADDRSTRLEN];
    int exit_armed, ack, auto_rotate, tilt_recheck, clean_seq;
    char ack_key[16];
    int64_t exit_armed_until, last_input, next_channel;
    /* Gesture in progress. */
    int mode, edge, sx, sy, lx, ly, slider;
    int64_t t0, last_move;
    int input_fd;
    int64_t input_retry;
};

static int usb_hit(int x, int y);

static const int flash_choices[4] = {0, 3, 6, 12};
/* Minutes without input before the Kobo sleeps (0 = never). */
static const int idle_choices[4] = {5, 15, 30, 0};

/* Panel settings persist next to the package (KOBODROID_SETTINGS): "flash=N idle=M". */
static void ui_load_settings(struct ui *ui) {
    const char *path = getenv("KOBODROID_SETTINGS");
    FILE *f = path ? fopen(path, "r") : NULL;
    int flash, idle, rotate, page_wake;
    if (!f) return;
    int n = fscanf(f, "flash=%d idle=%d rotate=%d pagewake=%d", &flash, &idle, &rotate, &page_wake);
    if (n >= 2) {
        for (int i = 0; i < 4; ++i) if (flash_choices[i] == flash) ui->flash_every = flash;
        for (int i = 0; i < 4; ++i) if (idle_choices[i] == idle) ui->idle_minutes = idle;
    }
    if (n >= 3) ui->auto_rotate = rotate != 0;
    if (n == 4) ui->page_wake = page_wake != 0;
    fclose(f);
}

static void ui_save_settings(const struct ui *ui) {
    const char *path = getenv("KOBODROID_SETTINGS");
    char temporary[512];
    if (!path || snprintf(temporary, sizeof(temporary), "%s.new", path) >= (int)sizeof(temporary)) return;
    FILE *f = fopen(temporary, "w");
    if (!f) return;
    fprintf(f, "flash=%d idle=%d rotate=%d pagewake=%d\n", ui->flash_every, ui->idle_minutes,
            ui->auto_rotate, ui->page_wake);
    if (!fclose(f)) rename(temporary, path);
}

static void ui_init(struct ui *ui) {
    memset(ui, 0, sizeof(*ui));
    ui->flash_every = 12;
    ui->idle_minutes = 15;
    ui->auto_rotate = 1;
    ui->wifi_request = -1;
    ui->input_fd = -1;
    ui->minute = -1;
    ui->light_max = read_int(LIGHT_DIR "max_brightness", 100);
    ui->warm_max = read_int(LIGHT_DIR "max_color", 10);
    ui->light = ui->light0 = read_int(LIGHT_DIR "brightness", -1);
    ui->warm = ui->warm0 = read_int(LIGHT_DIR "color", -1);
    ui->last_input = now_ms();
    ui_load_settings(ui);
}

/* Sets the front light and checks that it stays set: right after a resume the light controller can still be
   initialising and drop the value (the light stayed off after waking). Rewrites for up to 2 s. */
static void light_set_checked(int value, const char *when) {
    int seen = -1, writes = 0;
    for (int i = 0; i < 10; ++i) {
        seen = read_int(LIGHT_DIR "actual_brightness", -1);
        if (seen == value && i) break;
        if (seen != value) { write_int(LIGHT_DIR "brightness", value); ++writes; }
        usleep(200000);
    }
    printf("LIGHT_SET when=%s value=%d actual=%d writes=%d\n", when, value, read_int(LIGHT_DIR "actual_brightness", -1), writes);
}

/* Kobo keeps its own light settings; leave the hardware as Nickel set it. */
static void ui_restore_light(const struct ui *ui) {
    if (ui->light0 >= 0 && read_int(LIGHT_DIR "actual_brightness", -1) != ui->light0) light_set_checked(ui->light0, "exit");
    if (ui->warm0 >= 0 && ui->warm != ui->warm0) write_int(LIGHT_DIR "color", ui->warm0);
}

static int ui_poll_status(struct ui *ui) {
    int battery = read_int("/sys/class/power_supply/bd71827_bat/capacity", -1), charging = 0, wifi = 0;
    char line[160], ip[INET_ADDRSTRLEN] = "";
    FILE *f = fopen("/sys/class/power_supply/bd71827_bat/status", "r");
    if (f) {
        if (fgets(line, sizeof(line), f)) charging = !strncmp(line, "Charging", 8);
        fclose(f);
    }
    f = fopen("/proc/net/route", "r");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            char dev[16], dest[16];
            if (sscanf(line, "%15s %15s", dev, dest) == 2 && !strcmp(dev, "wlan0") && !strcmp(dest, "00000000"))
                wifi = 1;
        }
        fclose(f);
    }
    if (wifi) {
        int s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        struct ifreq req = {0};
        strncpy(req.ifr_name, "wlan0", IFNAMSIZ - 1);
        if (s >= 0 && !ioctl(s, SIOCGIFADDR, &req))
            inet_ntop(AF_INET, &((struct sockaddr_in *)&req.ifr_addr)->sin_addr, ip, sizeof(ip));
        if (s >= 0) close(s);
    }
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    int minute = tm.tm_hour * 60 + tm.tm_min;
    int changed = battery != ui->battery || charging != ui->charging || wifi != ui->wifi ||
                  minute != ui->minute || strcmp(ip, ui->ip);
    ui->battery = battery;
    ui->charging = charging;
    ui->wifi = wifi;
    ui->minute = minute;
    memcpy(ui->ip, ip, sizeof(ip));
    return changed;
}

/* ---- Icons, drawn at size s around (cx, cy). */
static void px_triangle(uint8_t *p, int x0, int y0, int x1, int y1, int x2, int y2, uint8_t v) {
    int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2), maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2), maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    for (int y = miny; y <= maxy; ++y)
        for (int x = minx; x <= maxx; ++x) {
            int a = (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0);
            int b = (x2 - x1) * (y - y1) - (y2 - y1) * (x - x1);
            int c = (x0 - x2) * (y - y2) - (y0 - y2) * (x - x2);
            if ((a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0)) px_fill(p, x, y, 1, 1, v);
        }
}

#include "panel-icons.h"

static void draw_icon(uint8_t *p, int cx, int cy, int size,
                      const struct icon_span *spans, size_t count, int ink, int paper) {
    int left = cx - size / 2, top = cy - size / 2;
    for (size_t i = 0; i < count; ++i) {
        const struct icon_span *span = &spans[i];
        int shade = (ink * span->coverage + paper * (4 - span->coverage)) / 4;
        px_fill(p, left + span->x, top + span->y, span->width, 1, shade);
    }
}

#define ICON_SPANS(name) name, sizeof(name) / sizeof((name)[0])

static void icon_battery(uint8_t *p, int x, int y, int percent, int charging) {
    px_box(p, x, y, 46, 24, 3, 0);
    px_fill(p, x + 46, y + 7, 4, 10, 0);
    if (percent > 0) px_fill(p, x + 6, y + 6, 34 * (percent > 100 ? 100 : percent) / 100, 12, 0);
    if (charging) px_triangle(p, x + 26, y + 2, x + 16, y + 14, x + 24, y + 14, 255);
}

/* ---- Control centre, dropped from the top edge over Android. */
enum { CC_NONE, CC_LIGHT, CC_LIGHT_MINUS, CC_LIGHT_PLUS, CC_WARM, CC_WARM_MINUS, CC_WARM_PLUS,
       CC_WIFI, CC_REFRESH, CC_FLASH, CC_ROTATE, CC_CLEAN, CC_EXIT, CC_OUTSIDE };
enum { SL_X0 = 150, SL_X1 = 1114, SL_LIGHT_Y = 210, SL_WARM_Y = 360, TILE_Y = 430, TILE_H = 180, TILE_GAP = 24 };
enum { TILES = 6 };
#define TILE_W ((int)(PANEL_W - 48 * 2 - TILE_GAP * (TILES - 1)) / TILES)
/* The control centre keeps its portrait layout, centred when the screen is landscape. */
#define CC_OX ((VIEW_W - (int)PANEL_W) / 2)

/* Natural light: the slider runs cool to warm, like Kobo's; the driver's color value rises towards cool. */
static int warm_position(const struct ui *ui) { return ui->warm_max - ui->warm; }

static void draw_slider(uint8_t *p, int y, int value, int max) {
    int x = max > 0 ? SL_X0 + (SL_X1 - SL_X0) * value / max : SL_X0;
    px_fill(p, SL_X0, y - 3, SL_X1 - SL_X0, 6, 0);
    px_fill(p, SL_X0, y - 7, x - SL_X0, 14, 0);
    px_disc(p, x, y, 24, 0);
    px_disc(p, x, y, 15, 255);
}

#define TILE_X ((PANEL_W - (TILES * TILE_W + (TILES - 1) * TILE_GAP)) / 2)

enum tile_state { TILE_ACTION, TILE_OFF, TILE_ON, TILE_BUSY };

static void draw_tile(uint8_t *p, int index, const char *label,
                      const struct icon_span *icon, size_t count, enum tile_state state) {
    int center = TILE_X + index * (TILE_W + TILE_GAP) + TILE_W / 2;
    int paper = state == TILE_ON ? 0 : state == TILE_BUSY ? 210 : 238;
    int ink = state == TILE_ON ? 255 : state == TILE_OFF ? 120 : 0;
    px_disc(p, center, TILE_Y + 62, 54, paper);
    draw_icon(p, center, TILE_Y + 62, 64, icon, count, ink, paper);
    text_at(p, center, TILE_Y + 120, label, FONT_S, 1, state == TILE_OFF ? 95 : 0);
}

static void draw_cc(uint8_t *p, const struct ui *ui) {
    char text[64];
    px_fill(p, 0, 0, VIEW_W, CC_H, 255);
    px_fill(p, 0, CC_H - 4, VIEW_W, 4, 0);
    view_ox = CC_OX;
    /* Status line. */
    snprintf(text, sizeof(text), "%02d:%02d", ui->minute / 60, ui->minute % 60);
    int status_y = 50, text_y = status_y - kd_digit_center_y[FONT_S];
    text_at(p, 48, text_y, text, FONT_S, 0, 0);
    int x = PANEL_W - 48 - 50;
    icon_battery(p, x, status_y - 12, ui->battery, ui->charging);
    if (ui->battery >= 0) {
        snprintf(text, sizeof(text), "%d%%", ui->battery);
        text_at(p, x - 14, text_y, text, FONT_S, 2, 0);
        x -= 14 + text_width(text, FONT_S);
    }
    draw_icon(p, x - 44, status_y, 40, ICON_SPANS(icon_wifi_40), ui->wifi ? 0 : 150, 255);
    /* Brightness and natural light. */
    if (ui->light >= 0) {
        draw_icon(p, 90, SL_LIGHT_Y, 40, ICON_SPANS(icon_sun_40), 150, 255);
        draw_icon(p, PANEL_W - 90, SL_LIGHT_Y, 40, ICON_SPANS(icon_sun_40), 0, 255);
        snprintf(text, sizeof(text), ZH("亮度") " %d%%", ui->light_max ? ui->light * 100 / ui->light_max : 0);
        text_at(p, SL_X0, SL_LIGHT_Y - 76, text, FONT_S, 0, 0);
        draw_slider(p, SL_LIGHT_Y, ui->light, ui->light_max);
    }
    if (ui->warm >= 0) {
        text_at(p, 90, SL_WARM_Y - kd_glyph_h[FONT_S] / 2, ZH("冷"), FONT_S, 1, 0);
        text_at(p, PANEL_W - 90, SL_WARM_Y - kd_glyph_h[FONT_S] / 2, ZH("暖"), FONT_S, 1, 0);
        text_at(p, SL_X0, SL_WARM_Y - 76, ZH("自然光"), FONT_S, 0, 0);
        draw_slider(p, SL_WARM_Y, warm_position(ui), ui->warm_max);
    }
    draw_tile(p, 0, ZH("Wi-Fi"), ICON_SPANS(icon_wifi_64),
              ui->wifi_busy ? TILE_BUSY : ui->wifi ? TILE_ON : TILE_OFF);
    draw_tile(p, 1, ZH("刷新"), ICON_SPANS(icon_refresh_64), TILE_ACTION);
    draw_tile(p, 2, ZH("全刷"), ICON_SPANS(icon_flash_64), ui->flash_every ? TILE_ON : TILE_OFF);
    draw_tile(p, 3, ZH("旋转"), ICON_SPANS(icon_rotate_64), ui->auto_rotate ? TILE_ON : TILE_OFF);
    draw_tile(p, 4, ZH("清理"), ICON_SPANS(icon_broom_64), TILE_ACTION);
    draw_tile(p, 5, ui->exit_armed ? ZH("确认") : ZH("退出"), ICON_SPANS(icon_exit_64),
              ui->exit_armed ? TILE_ON : TILE_ACTION);
    view_ox = 0;
}

static int cc_hit(int x, int y) {
    if (y >= CC_H) return CC_OUTSIDE;
    x -= CC_OX;
    if (abs(y - SL_LIGHT_Y) <= 55) return x < SL_X0 - 30 ? CC_LIGHT_MINUS : x > SL_X1 + 30 ? CC_LIGHT_PLUS : CC_LIGHT;
    if (abs(y - SL_WARM_Y) <= 55) return x < SL_X0 - 30 ? CC_WARM_MINUS : x > SL_X1 + 30 ? CC_WARM_PLUS : CC_WARM;
    if (y >= TILE_Y && y < TILE_Y + TILE_H && x >= TILE_X) {
        int i = (x - TILE_X) / (TILE_W + TILE_GAP);
        if (i < TILES && (x - TILE_X) % (TILE_W + TILE_GAP) < TILE_W) return CC_WIFI + i;
    }
    return CC_NONE;
}

static int slider_value(int x, int max) {
    x -= CC_OX;
    if (x < SL_X0) x = SL_X0;
    if (x > SL_X1) x = SL_X1;
    return ((x - SL_X0) * max + (SL_X1 - SL_X0) / 2) / (SL_X1 - SL_X0);
}

static void set_light(struct ui *ui, int light, int warm) {
    if (light < 0) light = 0;
    if (light > ui->light_max) light = ui->light_max;
    if (warm < 0) warm = 0;
    if (warm > ui->warm_max) warm = ui->warm_max;
    if (ui->light >= 0 && light != ui->light) { ui->light = light; write_int(LIGHT_DIR "brightness", light); ui->recompose = 1; }
    if (ui->warm >= 0 && warm != ui->warm) { ui->warm = warm; write_int(LIGHT_DIR "color", warm); ui->recompose = 1; }
}

static void set_warm_position(struct ui *ui, int position) { set_light(ui, ui->light, ui->warm_max - position); }

static void cc_set_open(struct ui *ui, int open) {
    if (open) ui_poll_status(ui);
    ui->cc_open = open;
    ui->exit_armed = 0;
    ui->recompose = ui->immediate = 1;
}

static void cycle_flash(struct ui *ui) {
    for (int i = 0; i < 4; ++i)
        if (flash_choices[i] == ui->flash_every) { ui->flash_every = flash_choices[(i + 1) % 4]; break; }
    ui_save_settings(ui);
}

static void ui_write_state(const struct ui *ui);

static void cc_tap(struct ui *ui, int id) {
    int armed = ui->exit_armed;
    ui->exit_armed = 0;
    ui->recompose = ui->immediate = 1;
    switch (id) {
    case CC_OUTSIDE: cc_set_open(ui, 0); break;
    case CC_LIGHT_MINUS: set_light(ui, ui->light - ui->light_max / 10, ui->warm); break;
    case CC_LIGHT_PLUS: set_light(ui, ui->light + ui->light_max / 10, ui->warm); break;
    case CC_WARM_MINUS: set_warm_position(ui, warm_position(ui) - 1); break;
    case CC_WARM_PLUS: set_warm_position(ui, warm_position(ui) + 1); break;
    case CC_WIFI: if (!ui->wifi_busy) ui->wifi_request = !ui->wifi; break;
    case CC_REFRESH: cc_set_open(ui, 0); ui->flash_now = 1; break;
    case CC_FLASH: cycle_flash(ui); break;
    case CC_ROTATE:
        /* Follow the Kobo's tilt, or keep the screen as it is now. */
        ui->auto_rotate = !ui->auto_rotate;
        ui->tilt_recheck = ui->auto_rotate;
        printf("ROTATION_AUTO=%d\n", ui->auto_rotate);
        ui_save_settings(ui);
        break;
    case CC_CLEAN:
        ++ui->clean_seq;
        ui_write_state(ui);
        printf("CC_CLEANUP_REQUEST=%d\n", ui->clean_seq);
        break;
    case CC_EXIT:
        if (armed) { ui->exit = 1; break; }
        ui->exit_armed = 1;
        ui->exit_armed_until = now_ms() + 3000;
        break;
    }
}

/* ---- Settings channel with the launcher. It writes bridge/ui/ui.request ("id key value"); the panel applies
   it and answers in bridge/ui/ui.state. Keys: flash and idle (choice index), cc (open the control centre), exit. */
static void ui_write_state(const struct ui *ui) {
    int flash = 0, idle = 0;
    for (int i = 0; i < 4; ++i) {
        if (flash_choices[i] == ui->flash_every) flash = i;
        if (idle_choices[i] == ui->idle_minutes) idle = i;
    }
    FILE *f = fopen("bridge/ui/ui.state.new", "w");
    if (!f) return;
    fprintf(f, "flash=%d idle=%d wifi=%d wifi_busy=%d ip=%s battery=%d charging=%d pagewake=%d clean_seq=%d", flash, idle, ui->wifi,
            ui->wifi_busy != 0, ui->ip[0] ? ui->ip : "-", ui->battery, ui->charging, ui->page_wake, ui->clean_seq);
    if (ui->ack_key[0]) fprintf(f, " %s_ack=%d %s_error=0", ui->ack_key, ui->ack, ui->ack_key);
    fputc('\n', f);
    if (!fclose(f)) { chmod("bridge/ui/ui.state.new", 0644); rename("bridge/ui/ui.state.new", "bridge/ui/ui.state"); }
}

static void ui_channel(struct ui *ui, int status_changed) {
    int64_t t = now_ms();
    if (t < ui->next_channel && !status_changed) return;
    ui->next_channel = t + 250;
    int ime = open("bridge/ui/ime.visible", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    char visible = '0';
    if (ime >= 0) {
        if (read(ime, &visible, 1) != 1) visible = '0';
        close(ime);
    }
    ui->ime_visible = visible == '1';
    char line[64], key[16];
    int id, value, answered = status_changed;
    FILE *f = fopen("bridge/ui/ui.request", "r");
    if (f) {
        if (fgets(line, sizeof(line), f) && sscanf(line, "%d %15s %d", &id, key, &value) == 3) {
            printf("UI_REQUEST key=%s value=%d\n", key, value);
            if (!strcmp(key, "flash") && value >= 0 && value < 4) { ui->flash_every = flash_choices[value]; ui_save_settings(ui); }
            else if (!strcmp(key, "idle") && value >= 0 && value < 4) { ui->idle_minutes = idle_choices[value]; ui_save_settings(ui); }
            else if (!strcmp(key, "pagewake") && (value == 0 || value == 1)) {
                ui->page_wake = value;
                ui_save_settings(ui);
            }
            else if (!strcmp(key, "cc")) cc_set_open(ui, value != 0);
            else if (!strcmp(key, "wifi") && (value == 0 || value == 1) && !ui->wifi_busy) ui->wifi_request = value;
            else if (!strcmp(key, "exit") && value == 1) ui->exit = 1;
            else if (!strcmp(key, "sleep") && value == 1) ui->sleep_request = 1;
            ui->ack = id;
            snprintf(ui->ack_key, sizeof(ui->ack_key), "%s", key);
            ui->recompose = 1;
        }
        fclose(f);
        unlink("bridge/ui/ui.request");
        answered = 1;
    }
    /* The display's actual rotation, from the launcher. */
    FILE *r = fopen("bridge/ui/display.rotation", "r");
    int rotation;
    if (r) {
        if (fscanf(r, "%d", &rotation) == 1 && rotation >= 0 && rotation <= 3 && rotation != view_rot) {
            printf("ROTATION view=%d\n", rotation);
            view_rot = rotation;
            ui->recompose = ui->immediate = 1;
        }
        fclose(r);
    }
    if (answered || access("bridge/ui/ui.state", F_OK)) ui_write_state(ui);
}

/* ---- Touch stream to SessionInput, in Android coordinates (the panel's). */
/* Called every loop: the FIFO opens once SessionInput is reading it, so the first touches are not lost. */
static void stream_connect(struct ui *ui) {
    if (ui->input_fd >= 0 || now_ms() < ui->input_retry) return;
    ui->input_retry = now_ms() + 100;
    if (mkfifo("bridge/input", 0600) && errno != EEXIST) perror("bridge/input");
    ui->input_fd = open("bridge/input", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (ui->input_fd >= 0) puts("SESSION_INPUT_CONNECTED=1");
}

static void stream_send(struct ui *ui, int important, const char *fmt, ...) {
    stream_connect(ui);
    if (ui->input_fd < 0) return;
    char line[48];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    for (int attempt = 0; attempt < (important ? 20 : 1); ++attempt) {
        if (write(ui->input_fd, line, n) == n) return;
        if (errno == EPIPE) { close(ui->input_fd); ui->input_fd = -1; return; }
        usleep(2000);
    }
}

static int guest_y(int y) {
    return y < 0 ? 0 : y >= VIEW_H ? VIEW_H - 1 : y;
}

static void stream_key(struct ui *ui, int code) { stream_send(ui, 1, "k %d\n", code); }

/* ---- Gestures (Kindle bridge thresholds). An edge touch is held back until it is clearly a swipe inward, or
   clearly not one: moved EDGE_SLOP across the swipe axis, or held EDGE_HOLD_MS (then it goes to Android from its
   start point, so long presses near an edge still work). Released earlier: a flick if it had moved EDGE_FLICK
   inward, else a tap at its start point. */
enum { EDGE_TB = 60, EDGE_LR = 96, EDGE_SWIPE = 60, EDGE_FLICK = 30, EDGE_SLOP = 40, EDGE_HOLD_MS = 600 };
enum { EDGE_TOP = 1, EDGE_BOTTOM, EDGE_LEFT, EDGE_RIGHT };
enum { G_IDLE, G_EDGE, G_ANDROID, G_PANEL, G_DONE, G_TAP };
enum { TAP_SLOP = 12, TAP_HOLD_MS = 150 };

static int edge_of(int x, int y) {
    return y < EDGE_TB * 2 / 3 ? EDGE_TOP : y >= VIEW_H - EDGE_TB ? EDGE_BOTTOM :
           x < EDGE_LR ? EDGE_LEFT : x >= VIEW_W - EDGE_LR ? EDGE_RIGHT : 0;
}

/* 1 = recognised swipe, -1 = not an edge gesture, 0 = undecided */
static int edge_progress(int edge, int dx, int dy, int released) {
    int along = edge == EDGE_TOP ? dy : edge == EDGE_BOTTOM ? -dy : edge == EDGE_LEFT ? dx : -dx;
    int across = edge <= EDGE_BOTTOM ? abs(dx) : abs(dy);
    if (along >= (released ? EDGE_FLICK : EDGE_SWIPE) && across < along) return 1;
    return across >= EDGE_SLOP && across > along ? -1 : 0;
}

static void edge_action(struct ui *ui, int edge) {
    printf("GESTURE edge=%d\n", edge);
    if (edge == EDGE_TOP) cc_set_open(ui, 1);
    else stream_key(ui, edge == EDGE_BOTTOM ? 3 : 4);
}

static void android_down(struct ui *ui, int x, int y) {
    ui->mode = G_ANDROID;
    ui->lx = x;
    ui->ly = y;
    ui->last_move = now_ms();
    stream_send(ui, 1, "d %d %d\n", x, guest_y(y));
}

static void gesture_down(struct ui *ui, int x, int y) {
    ui->sx = ui->lx = x;
    ui->sy = ui->ly = y;
    ui->t0 = now_ms();
    ui->slider = 0;
    if (ui->usb_prompt) {
        ui->mode = G_PANEL;
        return;
    }
    if (ui->cc_open) {
        ui->mode = G_PANEL;
        int id = cc_hit(x, y);
        if (id == CC_LIGHT || id == CC_WARM) {
            ui->slider = id;
            if (id == CC_LIGHT) set_light(ui, slider_value(x, ui->light_max), ui->warm);
            else set_warm_position(ui, slider_value(x, ui->warm_max));
            ui->immediate = 1;
        }
        return;
    }
    ui->edge = edge_of(x, y);
    ui->mode = ui->edge ? G_EDGE : G_TAP;
}

static void gesture_move(struct ui *ui, int x, int y) {
    if (ui->mode == G_EDGE) {
        int p = edge_progress(ui->edge, x - ui->sx, y - ui->sy, 0);
        if (p > 0) { ui->mode = G_DONE; edge_action(ui, ui->edge); }
        else if (p < 0) { android_down(ui, ui->sx, ui->sy); gesture_move(ui, x, y); }
    } else if (ui->mode == G_TAP) {
        if (abs(x - ui->sx) + abs(y - ui->sy) >= TAP_SLOP) { android_down(ui, ui->sx, ui->sy); gesture_move(ui, x, y); }
    } else if (ui->mode == G_ANDROID) {
        if (abs(x - ui->lx) + abs(y - ui->ly) < 3 || now_ms() - ui->last_move < 12) return;
        ui->lx = x;
        ui->ly = y;
        ui->last_move = now_ms();
        stream_send(ui, 0, "m %d %d\n", x, guest_y(y));
    } else if (ui->mode == G_PANEL && ui->slider) {
        if (ui->slider == CC_LIGHT) set_light(ui, slider_value(x, ui->light_max), ui->warm);
        else set_warm_position(ui, slider_value(x, ui->warm_max));
        ui->immediate = 1;
    }
}

static void gesture_up(struct ui *ui, int x, int y) {
    if (ui->mode == G_EDGE) {
        if (edge_progress(ui->edge, x - ui->sx, y - ui->sy, 1) > 0) edge_action(ui, ui->edge);
        else {
            stream_send(ui, 1, "d %d %d\n", ui->sx, guest_y(ui->sy));
            stream_send(ui, 1, "u %d %d\n", ui->sx, guest_y(ui->sy));
        }
    } else if (ui->mode == G_TAP) {
        stream_send(ui, 1, "d %d %d\n", ui->sx, guest_y(ui->sy));
        stream_send(ui, 1, "u %d %d\n", ui->sx, guest_y(ui->sy));
    } else if (ui->mode == G_ANDROID) {
        stream_send(ui, 1, "u %d %d\n", x, guest_y(y));
    } else if (ui->mode == G_PANEL && ui->usb_prompt) {
        int choice = abs(x - ui->sx) < 40 && abs(y - ui->sy) < 40 ? usb_hit(ui->sx, ui->sy) : 0;
        if (choice) {
            printf("USB_PROMPT choice=%s\n", choice == 2 ? "kobo" : "android");
            ui->usb_prompt = 0;
            ui->recompose = ui->immediate = 1;
            if (choice == 2) ui->exit = 1;
        }
    } else if (ui->mode == G_PANEL && !ui->slider) {
        if (ui->sy - y >= EDGE_SWIPE) cc_set_open(ui, 0);
        else if (abs(x - ui->sx) < 40 && abs(y - ui->sy) < 40) {
            cc_tap(ui, cc_hit(ui->sx, ui->sy));
        }
    }
    ui->mode = G_IDLE;
}

static void ui_tick(struct ui *ui) {
    int64_t t = now_ms();
    if (ui->mode == G_EDGE && t - ui->t0 >= EDGE_HOLD_MS) android_down(ui, ui->sx, ui->sy);
    if (ui->mode == G_TAP && t - ui->t0 >= TAP_HOLD_MS) android_down(ui, ui->sx, ui->sy);
    if (ui->exit_armed && t >= ui->exit_armed_until) {
        ui->exit_armed = 0;
        ui->recompose = ui->immediate = 1;
    }
}

/* Raw Elan events. Slots are ignored and a repeated tracking id only continues the touch: the panel repeats it
   every frame (V11 treated that as a second finger and lost every tap). Positions are delivered per SYN_REPORT. */
struct touch_slot { int x, y, active, px, py; };
struct touch_state { int slot, reported, primary, multi, sent; struct touch_slot s[2]; int64_t last_multi; };

static int panel_position(int rotation, int *x, int *y) {
    if (*x < 0 || *x >= 1264 || *y < 0 || *y >= 1680) return 0;
    *y = 1679 - *y;
    if (rotation == FB_ROTATE_CCW) { *x = 1263 - *x; *y = 1679 - *y; }
    /* Into Android's current orientation (the inverse of px_fill's mapping). */
    int px = *x, py = *y;
    if (view_rot == 1) { *x = py; *y = 1263 - px; }
    else if (view_rot == 2) { *x = 1263 - px; *y = 1679 - py; }
    else if (view_rot == 3) { *x = 1679 - py; *y = px; }
    return 1;
}

/* Two fingers stream as "M n id x y [id x y]" frames until both are up; Android then sees pointer down, move and
   pointer up events, so pinch zoom works. One finger keeps the single-touch gestures above. */
static void stream_multi(struct ui *ui, struct touch_state *t, int force) {
    char line[64];
    int n = 0, len = 0;
    for (int i = 0; i < 2; ++i) if (t->s[i].active) ++n;
    if (!force && n == t->sent && now_ms() - t->last_multi < 12) return;
    len = snprintf(line, sizeof(line), "M %d", n);
    for (int i = 0; i < 2; ++i)
        if (t->s[i].active) len += snprintf(line + len, sizeof(line) - len, " %d %d %d", i, t->s[i].px, guest_y(t->s[i].py));
    stream_send(ui, n != t->sent, "%s\n", line);
    t->sent = n;
    t->last_multi = now_ms();
}

static void read_touch(int fd, int rotation, struct touch_state *t, struct ui *ui) {
    struct input_event event;
    for (unsigned i = 0; i < 64 && read(fd, &event, sizeof(event)) == sizeof(event); ++i) {
        struct touch_slot *s = t->slot >= 0 && t->slot < 2 ? &t->s[t->slot] : NULL;
        if (event.type == EV_ABS && event.code == ABS_MT_SLOT) {
            t->slot = event.value;
        } else if (event.type == EV_ABS && event.code == ABS_MT_TRACKING_ID && s) {
            if (event.value >= 0 && !s->active) { s->active = 1; s->x = s->y = -1; }
            else if (event.value < 0) s->active = 0;
        } else if (event.type == EV_ABS && event.code == ABS_MT_POSITION_X && s) {
            s->y = event.value;
        } else if (event.type == EV_ABS && event.code == ABS_MT_POSITION_Y && s) {
            s->x = event.value;
        } else if (event.type == EV_SYN && event.code == SYN_REPORT) {
            int n = 0;
            for (int k = 0; k < 2; ++k) {
                struct touch_slot *c = &t->s[k];
                if (!c->active) continue;
                int x = c->x, y = c->y;
                if (x < 0 || y < 0 || !panel_position(rotation, &x, &y)) continue;
                c->px = x;
                c->py = y;
                ++n;
            }
            if (n) {
                if (now_ms() - ui->last_input >= 500) { int w = open("bridge/wake", O_CREAT | O_WRONLY | O_CLOEXEC, 0644); if (w >= 0) { futimens(w, NULL); close(w); } }
                ui->last_input = now_ms();
            }
            if (!t->reported) {
                if (!n) continue;
                t->primary = t->s[0].active ? 0 : 1;
                t->reported = 1;
                t->multi = 0;
                gesture_down(ui, t->s[t->primary].px, t->s[t->primary].py);
                continue;
            }
            struct touch_slot *p = &t->s[t->primary];
            if (!n) {
                if (t->multi) {
                    stream_send(ui, 1, "u %d %d\n", p->px, guest_y(p->py));
                    ui->mode = G_IDLE;
                } else {
                    gesture_up(ui, p->px, p->py);
                }
                t->reported = t->multi = t->sent = 0;
                continue;
            }
            if (n == 2 && !t->multi && ui->mode != G_PANEL && ui->mode != G_DONE) {
                if (ui->mode != G_ANDROID) android_down(ui, ui->sx, ui->sy);
                t->multi = 1;
                t->sent = 1;
                printf("GESTURE multi=1\n");
            }
            if (t->multi) { stream_multi(ui, t, 0); continue; }
            if (!p->active) { t->primary = !t->primary; p = &t->s[t->primary]; }
            gesture_move(ui, p->px, p->py);
        }
    }
}

/* ---- USB cable: like Kobo, ask whether to hand the storage to the computer. */
enum { USB_W = 900, USB_H = 420, USB_BTN_W = (USB_W - 48 * 3) / 2 };
#define USB_X ((VIEW_W - USB_W) / 2)
#define USB_Y ((VIEW_H - USB_H) / 2)
#define USB_BTN_Y (USB_Y + USB_H - 150)

static void round_rect(uint8_t *p, int x, int y, int w, int h, int radius, uint8_t shade) {
    px_fill(p, x, y + radius, w, h - 2 * radius, shade);
    for (int row = 0; row < radius; ++row) {
        int dy = radius - row;
        int inset = radius - (int)sqrt(radius * radius - dy * dy);
        px_fill(p, x + inset, y + row, w - 2 * inset, 1, shade);
        px_fill(p, x + inset, y + h - 1 - row, w - 2 * inset, 1, shade);
    }
}

static void draw_usb(uint8_t *p) {
    round_rect(p, USB_X, USB_Y, USB_W, USB_H, 36, 140);
    round_rect(p, USB_X + 2, USB_Y + 2, USB_W - 4, USB_H - 4, 34, 255);
    int cx = VIEW_W / 2;
    px_disc(p, cx, USB_Y + 105, 54, 238);
    draw_icon(p, cx, USB_Y + 105, 64, ICON_SPANS(icon_usb_64), 0, 238);
    text_at(p, cx, USB_Y + 177, ZH("已连接 USB"), FONT_L, 1, 0);

    int x = USB_X + 48;
    round_rect(p, x, USB_BTN_Y, USB_BTN_W, 110, 28, 238);
    text_at(p, x + USB_BTN_W / 2, USB_BTN_Y + (110 - kd_glyph_h[FONT_L]) / 2, ZH("继续使用"), FONT_L, 1, 0);
    x += USB_BTN_W + 48;
    round_rect(p, x, USB_BTN_Y, USB_BTN_W, 110, 28, 0);
    text_at(p, x + USB_BTN_W / 2, USB_BTN_Y + (110 - kd_glyph_h[FONT_L]) / 2, ZH("返回 Kobo"), FONT_L, 1, 255);
}

/* 0 = outside the buttons, 1 = keep using Android, 2 = return to Kobo. */
static int usb_hit(int x, int y) {
    if (y < USB_BTN_Y || y >= USB_BTN_Y + 110) return 0;
    if (x >= USB_X + 48 && x < USB_X + 48 + USB_BTN_W) return 1;
    if (x >= USB_X + 96 + USB_BTN_W && x < USB_X + 96 + 2 * USB_BTN_W) return 2;
    return 0;
}

/* ---- Sleep screen. */
static int draw_sleep_wallpaper(uint8_t *p) {
    enum { WALLPAPER_MAX_EDGE = 840 };
    int fd = open("bridge/ui/sleep-wallpaper.rgba", O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return 0;
    struct stat info;
    uint8_t header[4];
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || read(fd, header, sizeof(header)) != sizeof(header)) {
        close(fd);
        return 0;
    }
    unsigned w = header[0] * 256U + header[1], h = header[2] * 256U + header[3];
    if (!w || !h || w > WALLPAPER_MAX_EDGE || h > WALLPAPER_MAX_EDGE ||
        info.st_size != (off_t)(sizeof(header) + w * h * 4)) {
        close(fd);
        return 0;
    }
    size_t size = w * h * 4;
    uint8_t *image = malloc(size);
    if (!image) { close(fd); return 0; }
    size_t offset = 0;
    while (offset < size) {
        ssize_t n = read(fd, image + offset, size - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        offset += (size_t)n;
    }
    close(fd);
    if (offset != size) { free(image); return 0; }

    int width = VIEW_W, height = (int)(h * width / w);
    if (height > VIEW_H) { height = VIEW_H; width = (int)(w * height / h); }
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    int left = (VIEW_W - width) / 2, top = (VIEW_H - height) / 2;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const uint8_t *src = image + 4 * ((size_t)(y * h / height) * w + x * w / width);
            unsigned alpha = src[3];
            if (!alpha) continue;
            int px = left + x, py = top + y, tmp = px;
            if (view_rot == 1) { px = PANEL_W - 1 - py; py = tmp; }
            else if (view_rot == 2) { px = PANEL_W - 1 - px; py = PANEL_H - 1 - py; }
            else if (view_rot == 3) { px = py; py = PANEL_H - 1 - tmp; }
            uint8_t *dst = p + 4 * ((size_t)py * PANEL_W + px);
            for (int c = 0; c < 3; ++c) dst[c] = (src[c] * alpha + dst[c] * (255 - alpha) + 127) / 255;
            dst[3] = 255;
        }
    }
    free(image);
    puts("SLEEP_WALLPAPER=APPLIED");
    return 1;
}

static void draw_sleep(uint8_t *p) {
    if (draw_sleep_wallpaper(p)) return;
    int cx = VIEW_W / 2, cy = VIEW_H / 2 - 80;
    px_fill(p, 0, 0, VIEW_W, VIEW_H, 255);
    px_disc(p, cx, cy, 68, 238);
    draw_icon(p, cx, cy, 64, ICON_SPANS(icon_moon_64), 0, 238);
    text_at(p, cx, cy + 96, ZH("休眠"), FONT_L, 1, 0);
    text_at(p, cx, cy + 162, ZH("按电源键唤醒"), FONT_S, 1, 95);
}

/* ---- Loading screen: name, phase line and a Kobo-style progress bar. */
static const char LOADING_START[] = ZH("正在启动 Android");
static const char LOADING_FAILED[] = ZH("启动失败，正在返回 Kobo");
enum { BAR_X = 232, BAR_W = 800, BAR_Y = 880, BAR_H = 22 };

static void draw_loading_bar(uint8_t *p, int percent) {
    char text[8];
    px_fill(p, BAR_X - 8, BAR_Y - 8, BAR_W + 16, BAR_H + 90, 255);
    px_box(p, BAR_X, BAR_Y, BAR_W, BAR_H, 3, 0);
    px_fill(p, BAR_X + 6, BAR_Y + 6, (BAR_W - 12) * percent / 100, BAR_H - 12, 0);
    snprintf(text, sizeof(text), "%d%%", percent);
    text_at(p, PANEL_W / 2, BAR_Y + 40, text, FONT_S, 1, 0);
}

static void draw_loading(uint8_t *p, int percent, const char *line) {
    px_fill(p, 0, 0, PANEL_W, PANEL_H, 255);
    text_at(p, PANEL_W / 2, 620, ZH("Kobodroid"), FONT_XL, 1, 0);
    text_at(p, PANEL_W / 2, 760, line, FONT_L, 1, 0);
    draw_loading_bar(p, percent);
}
