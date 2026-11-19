#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include "kd-frame-protocol.h"

enum { WIDTH = 1264, HEIGHT = 1680, PIXELS = WIDTH * HEIGHT * 2 };

static uint64_t find_frame(int process) {
    int fd = openat(process, "maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    FILE *maps = fdopen(fd, "r");
    if (!maps) { close(fd); return 0; }
    char line[512], access[5];
    uint64_t address = 0;
    while (fgets(line, sizeof(line), maps)) {
        unsigned long long start, end;
        if (sscanf(line, "%llx-%llx %4s", &start, &end, access) == 3 &&
            !strcmp(access, "rw-s") && strstr(line, "kindleroid-framebuffer") &&
            end - start == kd_frame_offset(PIXELS) + KD_FRAME_PAGE) {
            address = start;
            break;
        }
    }
    fclose(maps);
    return address;
}

static int64_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int valid_meta(const struct kd_frame_meta *meta) {
    return meta->magic == KD_FRAME_MAGIC && meta->version == KD_FRAME_VERSION &&
           meta->header_bytes == sizeof(*meta) && meta->width == WIDTH &&
           meta->height == HEIGHT && meta->stride == WIDTH * 2 &&
           meta->pixel_bytes == PIXELS && meta->sequence && !(meta->sequence & 1);
}

static int publish(const uint8_t *pixels) {
    uint32_t header[] = {WIDTH, HEIGHT, 4};
    FILE *f = fopen("/bridge/frame.tmp", "wb");
    if (!f) return 0;
    int written = fwrite(header, sizeof(header), 1, f) == 1 &&
                  fwrite(pixels, PIXELS, 1, f) == 1;
    if (fclose(f) || !written) return 0;
    return rename("/bridge/frame.tmp", "/bridge/frame") == 0;
}

int main(int argc, char **argv) {
    if (argc != 2 || !argv[1][0] || strspn(argv[1], "0123456789") != strlen(argv[1])) return 2;
    char path[64];
    if (snprintf(path, sizeof(path), "/proc/%s", argv[1]) >= (int)sizeof(path)) return 2;
    int process = open(path, O_DIRECTORY | O_RDONLY | O_CLOEXEC);
    if (process < 0) return 3;
    int comm = openat(process, "comm", O_RDONLY | O_CLOEXEC);
    char name[32] = {0};
    if (comm < 0 || read(comm, name, sizeof(name) - 1) < 0 ||
        strcmp(name, "surfaceflinger\n")) return 3;
    close(comm);
    uint64_t start = 0;
    for (unsigned i = 0; i < 80 && !start; ++i) {
        start = find_frame(process);
        if (!start) usleep(50000);
    }
    if (!start) { puts("FRAME_MAP_NOT_FOUND"); return 4; }
    int mem = openat(process, "mem", O_RDONLY | O_CLOEXEC);
    close(process);
    if (mem < 0) { perror("surfaceflinger mem"); return 5; }

    uint8_t *current = malloc(PIXELS), *previous = malloc(PIXELS);
    if (!current || !previous) return 6;
    off_t meta_offset = start + kd_frame_offset(PIXELS);
    unsigned sequence = 0, published = 0, duplicates = 0;
    int64_t ready_deadline = monotonic_ms() + 12000, last_change = monotonic_ms();
    while (access("/bridge/stop", F_OK) && access("/bridge/result", F_OK)) {
        /* The panel maps SurfaceFlinger's framebuffer itself once it can (bridge/direct); stand by meanwhile. */
        if (published && !access("/bridge/direct", F_OK)) { usleep(500000); continue; }
        struct kd_frame_meta before, after;
        if (pread(mem, &before, sizeof(before), meta_offset) != sizeof(before)) {
            puts("FRAME_STREAM_SOURCE_LOST=1");
            return 8;
        }
        if (valid_meta(&before) && before.sequence != sequence &&
            pread(mem, current, PIXELS, start) == PIXELS &&
            pread(mem, &after, sizeof(after), meta_offset) == sizeof(after) &&
            !memcmp(&before, &after, sizeof(before))) {
            sequence = before.sequence;
            if (!published || memcmp(current, previous, PIXELS)) {
                if (!publish(current)) { perror("frame publish"); return 7; }
                uint8_t *spare = previous;
                previous = current;
                current = spare;
                last_change = monotonic_ms();
                if (++published == 1)
                    printf("FRAME_STREAM_READY bytes=%u\n", PIXELS);
                fflush(stdout);
            } else ++duplicates;
        }
        if (!published && monotonic_ms() >= ready_deadline) return 9;
        /* 40 ms while frames change or within 3 s of input (bridge/wake), else 160 ms. */
        struct stat wake;
        int64_t now = monotonic_ms(), woke = 0;
        if (!stat("/bridge/wake", &wake)) woke = (int64_t)wake.st_mtim.tv_sec * 1000 + wake.st_mtim.tv_nsec / 1000000;
        struct timespec real;
        clock_gettime(CLOCK_REALTIME, &real);
        int64_t since_input = (int64_t)real.tv_sec * 1000 + real.tv_nsec / 1000000 - woke;
        usleep(now - last_change < 3000 || since_input < 3000 ? 40000 : 160000);
    }
    printf("FRAME_STREAM_STOP frames=%u duplicates=%u\n", published, duplicates);
    free(current);
    free(previous);
    close(mem);
    return 0;
}
