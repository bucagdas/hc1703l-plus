/* HC1703L Plus - apply motion settings to a copy of devParam.dat. MIT License.
 *
 * Runs on the camera at boot, before p2pcam starts. It never touches the input file: it writes an edited
 * copy that debug_cmd.sh bind-mounts over /home/devParam.dat, so nothing is written to flash and removing
 * the SD card restores the original behaviour.
 *
 *   mdcfg apply <devParam.dat> <out> <md.conf>     exit 0 on success, the output has a valid checksum
 *   mdcfg live  <p2pcam pid> <md.conf>             apply sensitivity and tracking to the running p2pcam, RAM only
 *   mdcfg image <p2pcam pid>                       print the selected image mode: 0 auto, 1 day, 2 night
 *   mdcfg reboot <p2pcam pid> [0|1]                nightly restart: optionally set it, then print "<on> <time>"
 *   mdcfg show  <devParam.dat>                     print the motion fields (never the Wi-Fi credentials)
 *
 * md.conf lines (unknown keys are ignored):
 *   zones=x,y,w,h;x,y,w,h     up to 25 rectangles on a 10x10 grid; empty = whole frame
 *   tracking=0|1              PTZ follows motion
 *   motion=0|1                motion events on or off
 *   human=0|1                 person events on or off
 *
 * Build: zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -Os -static -s -o mdcfg mdcfg.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#define FILE_SIZE 1004
#define MAGIC 0x20150910u
#define OFF_TRACK 0x2d8
#define OFF_ZONES 0x314
#define OFF_MOTION 0x388
#define OFF_HUMAN 0x38c
#define OFF_ZSTATE 0x3d5
#define OFF_IMAGE 0xfa
#define MAX_ZONES 25
#define EVENT_OFF 2

/* Addresses in the supported p2pcam build. live() refuses to write unless the first two check out. */
#define VA_SIG    0x22a484u  /* "support_ymv" in the read-only data */
#define VA_DEVPTR 0x39ddc0u  /* pointer to the RAM copy of devParam.dat, same layout as the file */
#define VA_THRESH 0x3f1b0cu  /* moving blocks needed for an event */
/* Nightly restart (stock FUN_00042030, checked every 6 s): after about 22 h of uptime, between 02:00 and 04:00
 * local time, once 30 s pass with no recording. VA_AUTOREBOOT is the hwcfg "auto_reboot" field (default 1), read on
 * every check and nowhere else. VA_REBOOT_AT is the chosen time in seconds after 02:00, picked at random once at
 * start-up (0..7199). */
#define VA_AUTOREBOOT 0x3a4bf8u
#define VA_REBOOT_AT  0x3a55a8u

static uint8_t b[FILE_SIZE];

static uint32_t rd32(int o) { return b[o] | b[o+1] << 8 | b[o+2] << 16 | (uint32_t)b[o+3] << 24; }
static void wr32(int o, uint32_t v) { b[o] = v; b[o+1] = v >> 8; b[o+2] = v >> 16; b[o+3] = v >> 24; }

static uint32_t checksum(void)
{
    uint32_t s = 0;
    for (int i = 12; i + 1 < FILE_SIZE; i += 2) s += b[i] | b[i+1] << 8;
    return s;
}

static int load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(b, 1, sizeof b, f);
    int extra = fgetc(f);
    fclose(f);
    if (n != FILE_SIZE || extra != EOF) return -1;
    if (rd32(0) != MAGIC || rd32(8) != FILE_SIZE - 12 || rd32(4) != checksum()) return -1;
    return 0;
}

/* Returns the zone count, or -1 on a malformed list. Nothing is stored on error. */
static int parse_zones(const char *s, uint8_t out[][4])
{
    int n = 0;
    while (*s) {
        int x, y, w, h, used = 0;
        if (sscanf(s, "%d,%d,%d,%d%n", &x, &y, &w, &h, &used) != 4) return -1;
        if (x < 0 || y < 0 || x > 9 || y > 9 || w < 1 || h < 1 || x + w > 10 || y + h > 10) return -1;
        if (n == MAX_ZONES) return -1;
        out[n][0] = x; out[n][1] = y; out[n][2] = w; out[n][3] = h;
        n++;
        s += used;
        if (*s == ';') s++;
        else if (*s) return -1;
    }
    return n;
}

static int apply(const char *in, const char *out, const char *conf)
{
    char line[256];
    if (load(in)) { fprintf(stderr, "mdcfg: bad input\n"); return 1; }
    FILE *f = fopen(conf, "r");
    if (!f) { fprintf(stderr, "mdcfg: no conf\n"); return 1; }
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *v = strchr(line, '=');
        if (!v) continue;
        *v++ = 0;
        if (!strcmp(line, "zones")) {
            uint8_t z[MAX_ZONES][4];
            int n = parse_zones(v, z);
            if (n < 0) { fclose(f); fprintf(stderr, "mdcfg: bad zones\n"); return 1; }
            memset(b + OFF_ZONES, 0, 4 + 4 * MAX_ZONES);
            wr32(OFF_ZONES, n);
            memcpy(b + OFF_ZONES + 4, z, 4 * n);
            if (n) b[OFF_ZSTATE] = 1;
        } else if (!strcmp(line, "tracking")) {
            wr32(OFF_TRACK, atoi(v) ? 1 : 0);
        } else if (!strcmp(line, "motion")) {
            wr32(OFF_MOTION, atoi(v) ? 0 : EVENT_OFF);
        } else if (!strcmp(line, "human")) {
            wr32(OFF_HUMAN, atoi(v) ? 0 : EVENT_OFF);
        }
    }
    fclose(f);
    wr32(4, checksum());
    f = fopen(out, "wb");
    if (!f) return 1;
    size_t n = fwrite(b, 1, sizeof b, f);
    if (fclose(f) || n != sizeof b) { remove(out); return 1; }
    return 0;
}

static int mem_put(int fd, uint32_t va, int32_t v)
{
    return pwrite(fd, &v, 4, va) == 4 ? 0 : -1;
}

/* Open the memory of p2pcam and prove it is the supported build. Returns the fd, or -1. */
static int open_mem(int pid, int flags, uint32_t *dev)
{
    char path[32], sig[12];
    uint32_t magic = 0;
    snprintf(path, sizeof path, "/proc/%d/mem", pid);
    int fd = open(path, flags);
    if (fd < 0) { perror(path); return -1; }
    if (pread(fd, sig, sizeof sig, VA_SIG) != sizeof sig || memcmp(sig, "support_ymv", sizeof sig) ||
        pread(fd, dev, 4, VA_DEVPTR) != 4 || pread(fd, &magic, 4, *dev) != 4 || magic != MAGIC) {
        fprintf(stderr, "mdcfg: unsupported p2pcam build\n");
        close(fd);
        return -1;
    }
    return fd;
}

static int image(int pid)
{
    uint32_t dev = 0;
    uint8_t v = 0;
    int fd = open_mem(pid, O_RDONLY, &dev);
    if (fd < 0) return 1;
    int ok = pread(fd, &v, 1, dev + OFF_IMAGE) == 1 && v <= 2;
    close(fd);
    if (!ok) return 1;
    printf("%d\n", v);
    return 0;
}

/* Prints "<on> <seconds after 02:00>". With set >= 0 it writes the switch first, but only if both values look sane. */
static int reboot_cfg(int pid, int set)
{
    uint32_t dev = 0, on = 0, at = 0;
    int fd = open_mem(pid, set < 0 ? O_RDONLY : O_RDWR, &dev);
    if (fd < 0) return 1;
    int ok = pread(fd, &on, 4, VA_AUTOREBOOT) == 4 && pread(fd, &at, 4, VA_REBOOT_AT) == 4 && on <= 1 && at < 7200;
    if (ok && set >= 0) ok = mem_put(fd, VA_AUTOREBOOT, set) == 0 && pread(fd, &on, 4, VA_AUTOREBOOT) == 4 && on == (uint32_t)set;
    close(fd);
    if (!ok) { fprintf(stderr, "mdcfg: nightly restart not readable or not written\n"); return 1; }
    printf("%u %u\n", on, at);
    return 0;
}

/* Sensitivity and tracking only: p2pcam reads both from memory every time. Zones are left out on purpose:
 * zones written to memory were not picked up during motion, so they stay boot-only.
 * Everything is checked before the first write. */
static int live(int pid, const char *conf)
{
    static const int thresh[] = { 15, 4, 1 };
    char line[256];
    int sens = -1, track = -1;
    FILE *f = fopen(conf, "r");
    if (!f) { fprintf(stderr, "mdcfg: no conf\n"); return 1; }
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *v = strchr(line, '=');
        if (!v) continue;
        *v++ = 0;
        if (!strcmp(line, "sensitivity")) sens = atoi(v);
        else if (!strcmp(line, "tracking")) track = atoi(v) ? 1 : 0;
    }
    fclose(f);
    if (sens < 0 || sens > 2 || track < 0) { fprintf(stderr, "mdcfg: bad conf\n"); return 1; }

    uint32_t dev = 0;
    int fd = open_mem(pid, O_RDWR, &dev);
    if (fd < 0) return 1;
    int bad = mem_put(fd, VA_THRESH, thresh[sens]) | mem_put(fd, dev + OFF_TRACK, track);
    close(fd);
    if (bad) { fprintf(stderr, "mdcfg: write failed\n"); return 1; }
    return 0;
}

static int show(const char *in)
{
    if (load(in)) { fprintf(stderr, "mdcfg: bad input\n"); return 1; }
    int n = (int)rd32(OFF_ZONES);
    printf("tracking=%d\nmotion=%d\nhuman=%d\nzones=", (int)rd32(OFF_TRACK),
           rd32(OFF_MOTION) != EVENT_OFF, rd32(OFF_HUMAN) != EVENT_OFF);
    for (int i = 0; i < n && i < MAX_ZONES; i++) {
        uint8_t *z = b + OFF_ZONES + 4 + 4 * i;
        printf("%s%d,%d,%d,%d", i ? ";" : "", z[0], z[1], z[2], z[3]);
    }
    putchar('\n');
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 5 && !strcmp(argv[1], "apply")) return apply(argv[2], argv[3], argv[4]);
    if (argc == 3 && !strcmp(argv[1], "show")) return show(argv[2]);
    if (argc == 4 && !strcmp(argv[1], "live")) return live(atoi(argv[2]), argv[3]);
    if (argc == 3 && !strcmp(argv[1], "image")) return image(atoi(argv[2]));
    if (argc == 3 && !strcmp(argv[1], "reboot")) return reboot_cfg(atoi(argv[2]), -1);
    if (argc == 4 && !strcmp(argv[1], "reboot") && (!strcmp(argv[3], "0") || !strcmp(argv[3], "1")))
        return reboot_cfg(atoi(argv[2]), argv[3][0] - '0');
    fprintf(stderr, "usage: mdcfg apply <in> <out> <conf> | mdcfg live <pid> <conf> | mdcfg image <pid> |"
                    " mdcfg reboot <pid> [0|1] | mdcfg show <in>\n");
    return 2;
}
