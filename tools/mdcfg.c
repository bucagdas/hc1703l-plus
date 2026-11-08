/* HC1703L Plus - apply motion settings to a copy of devParam.dat. MIT License.
 *
 * Runs on the camera at boot, before p2pcam starts. It never touches the input file: it writes an edited
 * copy that debug_cmd.sh bind-mounts over /home/devParam.dat, so nothing is written to flash and removing
 * the SD card restores the original behaviour.
 *
 *   mdcfg apply <devParam.dat> <out> <md.conf>     exit 0 on success, the output has a valid checksum
 *   mdcfg live  <p2pcam pid> <conf>                apply the live settings below to the running p2pcam, RAM only
 *   mdcfg state <p2pcam pid>                       print the settings p2pcam runs with now, in conf format
 *   mdcfg image <p2pcam pid>                       print the selected image mode: 0 auto, 1 day, 2 night
 *   mdcfg reboot <p2pcam pid> [0|1]                nightly restart: optionally set it, then print "<on> <time>"
 *   mdcfg show  <devParam.dat>                     print the motion fields (never the Wi-Fi credentials)
 *
 * Conf lines (md.conf and cam.conf; unknown keys are ignored). B = applied by "apply" at boot, L = by "live":
 *   zones=x,y,w,h;x,y,w,h     B   up to 25 rectangles on a 10x10 grid; empty = whole frame
 *   tracking=0|1              BL  PTZ follows motion
 *   motion=0|1                B   motion events on or off
 *   human=0|1                 B   person events on or off
 *   sensitivity=0|1|2         L   moving blocks needed for an event (boot: hwcfg motion_level, see debug_cmd.sh)
 *   person=0|10..90           L   person score threshold in %; 0 = follow sensitivity like stock (70/40/10)
 *   night=50..2000            L   go to night below this exposure score (boot: hwcfg agcnight_value)
 *   day=500..20000            L   back to day above this score while the IR light is on (boot: agcday_value)
 *   hourly=0|1                L   hourly forced-day probe 04:00-16:59 (patch 17; boot: hwcfg only_wireless_bind)
 *   recmode=0|1|2             BL  SD recording: off, events, continuous + events (live only between 0 and 1)
 *   volume=1..10              BL  speaker level
 *   nightcolor=0|1|2          B   night: IR black and white, colour + white light on motion, colour
 *   rotate=0|1                B   1 = picture turned 180 degrees
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
#define OFF_FLIP 0xfc
#define OFF_RECMODE 0x2c4
#define OFF_NIGHTCOLOR 0x2cc
#define OFF_VOLUME 0x3bc
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
/* Day/night (icrCtrlThd re-reads both on every poll): night when the exposure score is below -AGC_NIGHT,
 * back to day when it is above AGC_DAY while the IR light is on. */
#define VA_AGC_NIGHT  0x3a4c54u
#define VA_AGC_DAY    0x3a4c5cu
#define VA_NOPROBE    0x3a52c4u  /* non-zero: patch 17 skips the hourly forced-day probe */
#define VA_PERSON     0x3a1418u  /* float person score threshold, set once at start-up from motion_level */
#define VA_OSDPOS     0x39ff78u  /* time stamp corner in use: -1 off, 0..3 */
#define VA_VOICE_VOL  0x3a4c40u  /* hwcfg voice_volume, used when devParam 0x3bc is 0 */

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
        } else if (!strcmp(line, "recmode")) {
            int m = atoi(v);
            if (m < 0 || m > 2) { fclose(f); fprintf(stderr, "mdcfg: bad recmode\n"); return 1; }
            wr32(OFF_RECMODE, m);
        } else if (!strcmp(line, "volume")) {
            int n = atoi(v);
            if (n < 1 || n > 10) { fclose(f); fprintf(stderr, "mdcfg: bad volume\n"); return 1; }
            wr32(OFF_VOLUME, n * 10);
        } else if (!strcmp(line, "nightcolor")) {
            int m = atoi(v);
            if (m < 0 || m > 2) { fclose(f); fprintf(stderr, "mdcfg: bad nightcolor\n"); return 1; }
            wr32(OFF_NIGHTCOLOR, m);
        } else if (!strcmp(line, "rotate")) {
            b[OFF_FLIP] = atoi(v) ? 3 : 0;
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

static int rd32m(int fd, uint32_t va, int32_t *v) { return pread(fd, v, 4, va) == 4 ? 0 : -1; }

/* Patch 17 in place? Without it the hourly flag has no effect. */
static int probe_patched(int fd)
{
    static const uint8_t bl[4] = { 0xdc, 0xf0, 0xd3, 0xfd };
    uint8_t c[4];
    return pread(fd, c, 4, 0x37156) == 4 && !memcmp(c, bl, 4);
}

/* What p2pcam runs with now. Fails if any value looks wrong for the supported build. */
struct now { int32_t thresh, track, night, day, noprobe, recmode, volume, voicevol, nightcolor, osd; float person; uint8_t flip; };

static int read_now(int fd, uint32_t dev, struct now *n)
{
    if (rd32m(fd, VA_THRESH, &n->thresh) || rd32m(fd, dev + OFF_TRACK, &n->track) ||
        rd32m(fd, VA_AGC_NIGHT, &n->night) || rd32m(fd, VA_AGC_DAY, &n->day) ||
        rd32m(fd, VA_NOPROBE, &n->noprobe) || rd32m(fd, dev + OFF_RECMODE, &n->recmode) ||
        rd32m(fd, dev + OFF_VOLUME, &n->volume) || rd32m(fd, VA_VOICE_VOL, &n->voicevol) ||
        rd32m(fd, dev + OFF_NIGHTCOLOR, &n->nightcolor) || rd32m(fd, VA_OSDPOS, &n->osd) ||
        pread(fd, &n->person, 4, VA_PERSON) != 4 || pread(fd, &n->flip, 1, dev + OFF_FLIP) != 1)
        return -1;
    if (n->night >= 0 || n->night < -100000 || n->day <= 0 || n->day > 1000000 || (uint32_t)n->noprobe > 1 ||
        (uint32_t)n->recmode > 3 || (uint32_t)n->volume > 100 || (uint32_t)n->nightcolor > 2 ||
        !(n->person >= 0.0f && n->person <= 1.0f) || n->osd < -1 || n->osd > 3 || n->flip > 3)
        return -1;
    return 0;
}

static int sens_of(int32_t thresh) { return thresh == 15 ? 0 : thresh == 4 ? 1 : thresh == 1 ? 2 : -1; }

static int state(int pid)
{
    uint32_t dev = 0;
    struct now n;
    int fd = open_mem(pid, O_RDONLY, &dev);
    if (fd < 0) return 1;
    int ok = read_now(fd, dev, &n) == 0, patched = probe_patched(fd);
    close(fd);
    if (!ok) { fprintf(stderr, "mdcfg: unexpected values\n"); return 1; }
    printf("sensitivity=%d\ntracking=%d\nperson=%d\nnight=%d\nday=%d\nhourly=%d\nhourly_switch=%d\n"
           "recmode=%d\nvolume=%d\nnightcolor=%d\nrotate=%d\nosd=%d\n",
           sens_of(n.thresh), n.track ? 1 : 0, (int)(n.person * 100.0f + 0.5f), -n.night, n.day,
           patched && n.noprobe ? 0 : 1, patched, n.recmode == 3 ? 2 : n.recmode,
           n.volume ? n.volume / 10 : n.voicevol, n.nightcolor, n.flip == 3, n.osd);
    return 0;
}

/* Settings p2pcam reads from memory every time. Zones are left out on purpose: zones written to memory were not
 * picked up during motion, so they stay boot-only, like nightcolor and rotate. recmode changes live only between
 * 0 and 1 (continuous recording is set up at start-up). Every value is checked before the first write. */
static int live(int pid, const char *conf)
{
    static const int thresh[] = { 15, 4, 1 };
    static const float pmap[] = { 0.7f, 0.4f, 0.1f };
    char line[256];
    int sens = -1, track = -1, person = -1, night = -1, day = -1, hourly = -1, recmode = -1, volume = -1, any = 0;
    FILE *f = fopen(conf, "r");
    if (!f) { fprintf(stderr, "mdcfg: no conf\n"); return 1; }
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *v = strchr(line, '=');
        if (!v) continue;
        *v++ = 0;
        int x = atoi(v);
        if (!strcmp(line, "sensitivity")) sens = x;
        else if (!strcmp(line, "tracking")) track = x ? 1 : 0;
        else if (!strcmp(line, "person")) person = x;
        else if (!strcmp(line, "night")) night = x;
        else if (!strcmp(line, "day")) day = x;
        else if (!strcmp(line, "hourly")) hourly = x ? 1 : 0;
        else if (!strcmp(line, "recmode")) recmode = x;
        else if (!strcmp(line, "volume")) volume = x;
        else continue;
        any = 1;
    }
    fclose(f);
    if (!any || sens > 2 || (person != -1 && person != 0 && (person < 10 || person > 90)) ||
        (night != -1 && (night < 50 || night > 2000)) || (day != -1 && (day < 500 || day > 20000)) ||
        recmode > 2 || (volume != -1 && (volume < 1 || volume > 10))) {
        fprintf(stderr, "mdcfg: bad conf\n");
        return 1;
    }

    uint32_t dev = 0;
    struct now n;
    int fd = open_mem(pid, O_RDWR, &dev);
    if (fd < 0) return 1;
    if (read_now(fd, dev, &n)) { close(fd); fprintf(stderr, "mdcfg: unexpected values\n"); return 1; }
    int bad = 0;
    if (sens >= 0) bad |= mem_put(fd, VA_THRESH, thresh[sens]);
    if (track >= 0) bad |= mem_put(fd, dev + OFF_TRACK, track);
    if (night > 0) bad |= mem_put(fd, VA_AGC_NIGHT, -night);
    if (day > 0) bad |= mem_put(fd, VA_AGC_DAY, day);
    if (hourly >= 0 && probe_patched(fd)) bad |= mem_put(fd, VA_NOPROBE, !hourly);
    if (person > 0 || (person <= 0 && sens >= 0)) {   /* no person key: follow sensitivity, as stock does at boot */
        float p = person > 0 ? person / 100.0f : pmap[sens];
        int32_t raw;
        memcpy(&raw, &p, 4);
        bad |= mem_put(fd, VA_PERSON, raw);
    }
    if (recmode >= 0 && recmode < 2 && n.recmode < 2) bad |= mem_put(fd, dev + OFF_RECMODE, recmode);
    if (volume > 0) bad |= mem_put(fd, dev + OFF_VOLUME, volume * 10);
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
    if (argc == 3 && !strcmp(argv[1], "state")) return state(atoi(argv[2]));
    if (argc == 3 && !strcmp(argv[1], "reboot")) return reboot_cfg(atoi(argv[2]), -1);
    if (argc == 4 && !strcmp(argv[1], "reboot") && (!strcmp(argv[3], "0") || !strcmp(argv[3], "1")))
        return reboot_cfg(atoi(argv[2]), argv[3][0] - '0');
    fprintf(stderr, "usage: mdcfg apply <in> <out> <conf> | mdcfg live <pid> <conf> | mdcfg state <pid> |"
                    " mdcfg image <pid> | mdcfg reboot <pid> [0|1] | mdcfg show <in>\n");
    return 2;
}
