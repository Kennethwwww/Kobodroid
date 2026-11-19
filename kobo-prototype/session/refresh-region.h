static int mostly_dark(const uint8_t *pixels, unsigned width, unsigned height) {
    unsigned dark = 0, samples = 0;
    for (unsigned y = 0; y < height; y += 24) {
        for (unsigned x = 0; x < width; x += 24) {
            const uint8_t *p = pixels + 4 * ((size_t)y * width + x);
            dark += p[0] < 16 && p[1] < 16 && p[2] < 16;
            ++samples;
        }
    }
    return dark * 100 >= samples * 98;
}

struct damage {
    unsigned left, top, right, bottom;
    int color;
};

static int chromatic(const uint8_t *pixel) {
    int r = pixel[0], g = pixel[1], b = pixel[2];
    return abs(r - g) > 10 || abs(g - b) > 10 || abs(r - b) > 10;
}

static struct damage reconcile(uint8_t *fb, uint8_t *underlay, uint8_t *previous,
                               const uint8_t *desired, unsigned width, unsigned height) {
    struct damage changed = {width, height, 0, 0, 0};
    size_t row = (size_t)width * 4;
    for (unsigned y = 0; y < height; ++y) {
        size_t start = y * row;
        if (!memcmp(fb + start, previous + start, row) &&
            !memcmp(previous + start, desired + start, row)) continue;
        for (unsigned x = 0; x < width; ++x) {
            size_t offset = 4U * ((size_t)y * width + x);
            if (memcmp(fb + offset, previous + offset, 4))
                memcpy(underlay + offset, fb + offset, 4);
            if (!memcmp(fb + offset, desired + offset, 4)) continue;
            if (!changed.color)
                changed.color = chromatic(fb + offset) || chromatic(desired + offset);
            memcpy(fb + offset, desired + offset, 4);
            if (x < changed.left) changed.left = x;
            if (y < changed.top) changed.top = y;
            if (x + 1 > changed.right) changed.right = x + 1;
            if (y + 1 > changed.bottom) changed.bottom = y + 1;
        }
        memcpy(previous + start, desired + start, row);
    }
    return changed;
}

/* Like reconcile, but changes separated by at least BAND_GAP unchanged rows become separate bands (at most max),
   so typing that touches a field at the top and a keyboard at the bottom refreshes two small areas instead of
   most of the screen. */
enum { BAND_GAP = 48 };
static int reconcile_bands(uint8_t *fb, uint8_t *underlay, uint8_t *previous, const uint8_t *desired,
                           unsigned width, unsigned height, struct damage *bands, int max) {
    int count = 0;
    size_t row = (size_t)width * 4;
    for (unsigned y = 0; y < height; ++y) {
        size_t start = y * row;
        if (!memcmp(fb + start, previous + start, row) &&
            !memcmp(previous + start, desired + start, row)) continue;
        /* Whole pixels as 32-bit words: per-pixel memcmp made a full page take about 80 ms. */
        uint32_t *f = (uint32_t *)(fb + start), *u = (uint32_t *)(underlay + start);
        const uint32_t *p = (const uint32_t *)(previous + start), *d = (const uint32_t *)(desired + start);
        for (unsigned x = 0; x < width; ++x) {
            if (f[x] != p[x]) u[x] = f[x];
            if (f[x] == d[x]) continue;
            if (!count || (y >= bands[count - 1].bottom + BAND_GAP && count < max))
                bands[count++] = (struct damage){width, y, 0, y + 1, 0};
            struct damage *b = &bands[count - 1];
            if (!b->color) b->color = chromatic((const uint8_t *)&f[x]) || chromatic((const uint8_t *)&d[x]);
            f[x] = d[x];
            if (x < b->left) b->left = x;
            if (x + 1 > b->right) b->right = x + 1;
            if (y + 1 > b->bottom) b->bottom = y + 1;
        }
        memcpy(previous + start, desired + start, row);
    }
    return count;
}

/* reconcile_bands for a known framebuffer: previous holds what the panel last wrote, and nothing else draws while
   Nickel is paused, so the framebuffer (uncached device memory, slow to read: a full page took about 80 ms) is only
   written. The periodic repair keeps using reconcile_bands, which also notices foreign drawing. */
static int reconcile_bands_written(uint8_t *fb, uint8_t *previous, const uint8_t *desired, unsigned width,
                                   unsigned height, struct damage *bands, int max) {
    int count = 0;
    size_t row = (size_t)width * 4;
    for (unsigned y = 0; y < height; ++y) {
        size_t start = y * row;
        if (!memcmp(previous + start, desired + start, row)) continue;
        const uint32_t *p = (const uint32_t *)(previous + start), *d = (const uint32_t *)(desired + start);
        for (unsigned x = 0; x < width; ++x) {
            if (p[x] == d[x]) continue;
            if (!count || (y >= bands[count - 1].bottom + BAND_GAP && count < max))
                bands[count++] = (struct damage){width, y, 0, y + 1, 0};
            struct damage *b = &bands[count - 1];
            if (!b->color) b->color = chromatic((const uint8_t *)&p[x]) || chromatic((const uint8_t *)&d[x]);
            if (x < b->left) b->left = x;
            if (x + 1 > b->right) b->right = x + 1;
            if (y + 1 > b->bottom) b->bottom = y + 1;
        }
        /* Whole rows in one copy: scattered pixel stores to the framebuffer are slow too. */
        memcpy(fb + start, desired + start, row);
        memcpy(previous + start, desired + start, row);
    }
    return count;
}

static pid_t submit_region_mode(struct damage changed, int rotation, const uint8_t *frame, int large, int flash, int fast) {
    changed.left &= ~7U;
    changed.top &= ~7U;
    changed.right = (changed.right + 7U) & ~7U;
    changed.bottom = (changed.bottom + 7U) & ~7U;
    /* A dirty bounding box can contain unchanged color pixels. */
    for (unsigned y = changed.top; !changed.color && y < changed.bottom; ++y) {
        const uint32_t *px = (const uint32_t *)frame + (size_t)y * PANEL_W;
        for (unsigned x = changed.left; x < changed.right; ++x) {
            uint32_t v = px[x];
            if (((v ^ (v >> 8)) & 0xffff) == 0) continue;  /* r == g == b: gray, the common case */
            if (chromatic((const uint8_t *)&px[x])) { changed.color = 1; break; }
        }
    }
    /* A flashing update clears accumulated ghosting as part of the page change itself. */
    const char *waveform = flash ? (changed.color ? "GCC16" : "GC16") :
                           changed.color ? "GLRC16" : fast ? "DU" : large ? "REAGL" : "GL16";
    printf("REFRESH_SUBMIT top=%u left=%u width=%u height=%u wave=%s flash=%d at=%lld\n", changed.top, changed.left,
           changed.right - changed.left, changed.bottom - changed.top, waveform, flash, (long long)now_ms());
    pid_t pid = fork();
    if (pid == 0) {
        char rect[96];
        snprintf(rect, sizeof(rect), "top=%u,left=%u,width=%u,height=%u",
                 changed.top, changed.left, changed.right - changed.left,
                 changed.bottom - changed.top);
        const char *limit = getenv("KOBO_RUN_LIMIT");
        const char *fbink = "/mnt/onboard/.adds/koreader/fbink";
        if (rotation == FB_ROTATE_CW)
            execl(limit, "run-limit", "12", fbink, "-w", flash ? "-f" : "-q", "-W", waveform, "-s", rect, NULL);
        else
            execl(limit, "run-limit", "12", fbink, "-w", "-W", "GCC16", "--refresh", NULL);
        _exit(127);
    }
    return pid;
}


static pid_t submit_region(struct damage changed, int rotation, const uint8_t *frame, int large, int flash) {
    return submit_region_mode(changed, rotation, frame, large, flash, 0);
}
