#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "kd-frame-protocol.h"

enum { WIDTH = 1264, HEIGHT = 1630, PIXELS = WIDTH * HEIGHT * 2 };

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

/* The probe draws three 168x200 tiles and turns one green per touch.
 * Match the published step, allowing for RGB565 quantization and small overlays. */
static int matches_probe_frame(const uint8_t *pixels, unsigned step) {
    unsigned green = 0, red = 0, blue = 0;
    for (size_t i = 0; i < PIXELS; i += 2) {
        unsigned rgb = pixels[i] | (pixels[i+1] << 8);
        unsigned r = ((rgb >> 11) & 31) * 255 / 31;
        unsigned g = ((rgb >> 5) & 63) * 255 / 63;
        unsigned b = (rgb & 31) * 255 / 31;
        green += g > 80 && g > r + 35 && g > b + 20;
        red += r > 130 && r > g + 90 && r > b + 70;
        blue += b > 110 && b > r + 55 && b > g + 35;
    }
    unsigned expected = step * 168 * 200;
    return green + 5000 >= expected && green <= expected + 5000 &&
           (step || red > 25000) && (step == 3 || blue > 25000);
}

int main(int argc, char **argv) {
    if ((argc != 4 && argc != 5) || !argv[1][0] || strspn(argv[1], "0123456789") != strlen(argv[1])) return 2;
    int any = !strcmp(argv[3], "any");
    if (!any && (strlen(argv[3]) != 1 || argv[3][0] < '0' || argv[3][0] > '3')) return 2;
    unsigned step = any ? 0 : argv[3][0] - '0';
    unsigned previous_sequence = 0;
    if (argc == 5) {
        FILE *prior = fopen(argv[4], "r");
        if (prior) {
            if (fscanf(prior, "%u", &previous_sequence) != 1) previous_sequence = 0;
            fclose(prior);
        }
    }
    char path[64];
    if (snprintf(path, sizeof(path), "/proc/%s", argv[1]) >= (int)sizeof(path)) return 2;
    int process = open(path, O_DIRECTORY | O_RDONLY | O_CLOEXEC);
    if (process < 0) { perror("surfaceflinger process"); return 3; }
    int comm = openat(process, "comm", O_RDONLY | O_CLOEXEC);
    char name[32] = {0};
    if (comm < 0 || read(comm, name, sizeof(name) - 1) < 0 ||
        strcmp(name, "surfaceflinger\n")) return 3;
    close(comm);
    uint64_t start = find_frame(process);
    if (!start) { puts("FRAME_MAP_NOT_FOUND"); return 4; }
    int mem = openat(process, "mem", O_RDONLY | O_CLOEXEC);
    close(process);
    if (mem < 0) { perror("surfaceflinger mem"); return 5; }
    uint8_t *pixels = malloc(PIXELS);
    if (!pixels) return 6;
    off_t offset = start + kd_frame_offset(PIXELS);
    for (int attempt = 0; attempt < 160; ++attempt) {
        struct kd_frame_meta before, after;
        if (pread(mem, &before, sizeof(before), offset) == sizeof(before) &&
            before.magic == KD_FRAME_MAGIC && before.version == KD_FRAME_VERSION &&
            before.header_bytes == sizeof(before) && before.width == WIDTH &&
            before.height == HEIGHT && before.stride == WIDTH * 2 &&
            before.pixel_bytes == PIXELS && before.sequence && !(before.sequence & 1) &&
            pread(mem, pixels, PIXELS, start) == PIXELS &&
            pread(mem, &after, sizeof(after), offset) == sizeof(after) &&
            !memcmp(&before, &after, sizeof(before)) &&
            (any || matches_probe_frame(pixels, step))) {
            if (previous_sequence && previous_sequence == before.sequence) return 10;
            uint32_t header[] = {WIDTH, HEIGHT, 4};
            FILE *out = fopen(argv[2], "wb");
            if (!out) return 7;
            int ok = fwrite(header, sizeof(header), 1, out) == 1 &&
                     fwrite(pixels, PIXELS, 1, out) == 1;
            if (fclose(out) || !ok) return 7;
            if (argc == 5) {
                FILE *sequence = fopen(argv[4], "w");
                if (sequence) {
                    fprintf(sequence, "%u\n", before.sequence);
                    fclose(sequence);
                }
            }
            printf("FRAME_SNAPSHOT step=%u sequence=%u bytes=%u\n", step, before.sequence, PIXELS);
            free(pixels);
            close(mem);
            return 0;
        }
        usleep(100000);
    }
    puts("FRAME_SNAPSHOT_TIMEOUT");
    return 8;
}
