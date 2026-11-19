/* One outstanding command. Publish a complete command with atomic rename. */
static int publish_input(const char *kind, int x, int y, int end_x, int end_y) {
    if (!access("bridge/request", F_OK)) return 0;
    FILE *out = fopen("bridge/request.tmp", "w");
    if (!out) return 0;
    if (!strcmp(kind, "tap")) fprintf(out, "tap %d %d\n", x, y);
    else if (!strcmp(kind, "swipe")) fprintf(out, "swipe %d %d %d %d\n", x, y, end_x, end_y);
    else if (!strcmp(kind, "key")) fprintf(out, "key %d\n", x);
    else fputs("exit\n", out);
    return fclose(out) == 0 && rename("bridge/request.tmp", "bridge/request") == 0;
}

static int publish_request(const char *kind, int x, int y) {
    return publish_input(kind, x, y, 0, 0);
}

struct touch_state {
    int x, y, down;
};

static int panel_position(int rotation, int *x, int *y) {
    if (*x < 0 || *x >= 1264 || *y < 0 || *y >= 1680) return 0;
    *y = 1679 - *y;
    if (rotation == FB_ROTATE_CCW) { *x = 1263 - *x; *y = 1679 - *y; }
    *x -= (1264 - PANEL_W) / 2;
    *y -= (1680 - PANEL_H) / 2;
    return *x >= 0 && *x < (int)PANEL_W && *y >= 0 && *y < (int)PANEL_H;
}

static int footer_button(int x) {
    if (x < (int)PANEL_W / 3) return 0;
    return x < (int)PANEL_W * 2 / 3 ? 1 : 2;
}

/* Parsed like the hardware-verified interactive probe: slots are ignored and a repeated tracking id only
   restarts the touch. (The V11 parser treated a repeated tracking id as a second finger and cancelled
   every tap on the Elan panel.) A release is a tap, or a footer button. */
static int read_session_touch(int fd, int rotation, struct touch_state *state) {
    struct input_event event;
    for (unsigned i = 0; i < 64 && read(fd, &event, sizeof(event)) == sizeof(event); ++i) {
        if (event.type != EV_ABS) continue;
        if (event.code == ABS_MT_TRACKING_ID && event.value >= 0) {
            if (!state->down) state->x = state->y = -1;
            state->down = 1;
        } else if (event.code == ABS_MT_POSITION_X) {
            state->y = event.value;
        } else if (event.code == ABS_MT_POSITION_Y) {
            state->x = event.value;
        } else if (event.code == ABS_MT_TRACKING_ID && state->down) {
            state->down = 0;
            int x = state->x, y = state->y;
            if (!panel_position(rotation, &x, &y)) continue;
            if (y >= (int)PANEL_H - 50) {
                int button = footer_button(x);
                if (button == 2) return 1;
                publish_request("key", button == 0 ? 4 : 3, 0);
            } else {
                publish_request("tap", x * GUEST_W / PANEL_W, y * GUEST_H / (PANEL_H - 50));
            }
        }
    }
    return 0;
}
