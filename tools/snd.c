/* HC1703L Plus - sound helper for the control panel. MIT License.
 *
 *   snd listen [seconds]        microphone: play the RTSP audio track of 127.0.0.1:554/0/av1 and copy its
 *                               G.711 A-law payload (8 kHz mono) to stdout until the reader goes away
 *                               (or after <seconds>, default 600). The panel decodes it in the browser.
 *   snd recv <file> <bytes>     read exactly <bytes> from stdin (a CGI POST body) into <file>; it must be an
 *                               8 kHz mono A-law WAV of at most 30 s, the format p2pcam's playaudio uses.
 *   snd tone <file>             write the panel's test chime as an 8 kHz A-law WAV.
 *
 * Nothing here touches flash. Build: zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -Os -static -s -o snd snd.c
 */
#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define RATE 8000
#define MAX_SECONDS 30
#define WAV_HEAD 44   /* RIFF + fmt (16) + data headers; an A-law "fmt " chunk is often 18 bytes, see wav_ok */

static const char *URL = "rtsp://127.0.0.1:554/0/av1";

/* ---- RTSP over TCP, audio track only, interleaved ---------------------------------------------------- */

static int fd = -1, cseq = 0;
static char session[128];
#define CAP 65536
static unsigned char buf[CAP + 1];   /* always NUL-terminated at buf[have] */
static size_t have;

static int send_req(const char *method, const char *url, const char *extra)
{
    char req[1024];
    size_t n = snprintf(req, sizeof req, "%s %s RTSP/1.0\r\nCSeq: %d\r\nUser-Agent: hc1703l-plus\r\n", method, url, ++cseq);
    if (session[0] && n < sizeof req) n += snprintf(req + n, sizeof req - n, "Session: %s\r\n", session);
    if (extra && n < sizeof req) n += snprintf(req + n, sizeof req - n, "%s\r\n", extra);
    if (n < sizeof req) n += snprintf(req + n, sizeof req - n, "\r\n");
    if (n >= sizeof req) return -1;
    return write(fd, req, n) == (ssize_t)n ? 0 : -1;
}

static int fill(void)
{
    if (have == CAP) return -1;
    ssize_t r = read(fd, buf + have, CAP - have);
    if (r <= 0) return -1;
    have += r;
    buf[have] = 0;
    return 0;
}

static void drop(size_t n) { memmove(buf, buf + n, have - n); have -= n; buf[have] = 0; }

/* Read one RTSP response (headers + body) into out. Interleaved data in front of it is skipped. */
static int response(char *out, size_t cap)
{
    for (;;) {
        while (have > 0 && buf[0] == '$') {            /* interleaved packet, skip it */
            while (have < 4) if (fill()) return -1;
            size_t len = 4 + (buf[2] << 8 | buf[3]);
            while (have < len) if (fill()) return -1;
            drop(len);
        }
        unsigned char *end = have ? memmem(buf, have, "\r\n\r\n", 4) : NULL;
        if (end && buf[0] != '$') {
            size_t hlen = end - buf + 4, blen = 0;
            char *cl = strcasestr((char *)buf, "Content-Length:");
            if (cl && (unsigned char *)cl < end) blen = strtoul(cl + 15, NULL, 10);
            while (have < hlen + blen) if (fill()) return -1;
            size_t n = hlen + blen < cap - 1 ? hlen + blen : cap - 1;
            memcpy(out, buf, n);
            out[n] = 0;
            drop(hlen + blen);
            return strncmp(out, "RTSP/1.0 200", 12) ? -1 : 0;
        }
        if (fill()) return -1;
    }
}

static int rtsp_audio(void)
{
    static char resp[8192];
    char ctl[256] = "", *p;
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(554) };
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    struct timeval tv = { 5, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof a)) return -1;

    if (send_req("DESCRIBE", URL, "Accept: application/sdp") || response(resp, sizeof resp)) return -1;
    p = strstr(resp, "m=audio");
    if (!p || !(p = strstr(p, "a=control:"))) return -1;
    sscanf(p + 10, "%255[^\r\n]", ctl);
    if (strncmp(ctl, "rtsp://", 7)) {                  /* relative control URL */
        char rel[256];
        snprintf(rel, sizeof rel, "%s", ctl);
        snprintf(ctl, sizeof ctl, "%s/%s", URL, rel);
    }
    if (send_req("SETUP", ctl, "Transport: RTP/AVP/TCP;unicast;interleaved=0-1") || response(resp, sizeof resp)) return -1;
    if (!(p = strcasestr(resp, "Session:"))) return -1;
    p += 8;
    while (*p == ' ') p++;
    sscanf(p, "%127[^;\r\n]", session);
    if (send_req("PLAY", URL, "Range: npt=0.000-") || response(resp, sizeof resp)) return -1;
    return 0;
}

static int listen_cmd(int seconds)
{
    signal(SIGPIPE, SIG_IGN);
    if (rtsp_audio()) { fprintf(stderr, "snd: RTSP audio not available\n"); return 1; }
    time_t start = time(NULL), alive = start;
    for (;;) {
        time_t now = time(NULL);
        if (now - start >= seconds) break;
        if (now - alive >= 25) {                       /* keep the session alive */
            alive = now;
            if (send_req("GET_PARAMETER", URL, NULL)) break;
        }
        while (have < 4) if (fill()) goto out;
        if (buf[0] != '$') {                           /* an RTSP reply to the keep-alive */
            static char resp[2048];
            if (response(resp, sizeof resp) && strncmp(resp, "RTSP/1.0", 8)) break;
            continue;
        }
        size_t len = buf[2] << 8 | buf[3];
        while (have < 4 + len) if (fill()) goto out;
        unsigned char *rtp = buf + 4;
        if (buf[1] == 0 && len > 12 && (rtp[0] >> 6) == 2) {
            size_t off = 12 + 4 * (rtp[0] & 0x0f);
            if ((rtp[0] & 0x10) && off + 4 <= len) off += 4 + 4 * (rtp[off + 2] << 8 | rtp[off + 3]);
            if (off < len) {
                if (fwrite(rtp + off, 1, len - off, stdout) != len - off || fflush(stdout)) goto out;
            }
        }
        drop(4 + len);
    }
out:
    send_req("TEARDOWN", URL, NULL);
    close(fd);
    return 0;
}

/* ---- WAV files ---------------------------------------------------------------------------------------- */

static void le32(unsigned char *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void le16(unsigned char *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static uint32_t rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const unsigned char *p) { return p[0] | p[1] << 8; }

/* A WAV that playaudio takes: RIFF/WAVE, fmt = A-law (6), mono, 8000 Hz, 8 bit, data <= 30 s. */
static int wav_ok(const unsigned char *b, size_t n)
{
    if (n < WAV_HEAD || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4) || memcmp(b + 12, "fmt ", 4)) return 0;
    uint32_t fl = rd32(b + 16);
    if (fl < 16 || 20 + fl + 8 > n) return 0;
    if (rd16(b + 20) != 6 || rd16(b + 22) != 1 || rd32(b + 24) != RATE || rd16(b + 34) != 8) return 0;
    const unsigned char *d = b + 20 + fl;
    return !memcmp(d, "data", 4) && rd32(d + 4) <= (uint32_t)RATE * MAX_SECONDS && d + 8 + rd32(d + 4) <= b + n;
}

static int recv_cmd(const char *file, long want)
{
    long cap = WAV_HEAD + 2 + (long)RATE * MAX_SECONDS;
    if (want < WAV_HEAD || want > cap) { fprintf(stderr, "snd: bad size\n"); return 1; }
    unsigned char *b = malloc(want);
    long got = 0;
    while (b && got < want) {
        ssize_t r = read(0, b + got, want - got);
        if (r <= 0) break;
        got += r;
    }
    if (!b || got != want || !wav_ok(b, want)) { free(b); fprintf(stderr, "snd: not an 8 kHz A-law WAV\n"); return 1; }
    FILE *f = fopen(file, "wb");
    int ok = f && fwrite(b, 1, want, f) == (size_t)want;
    if (f && fclose(f)) ok = 0;
    free(b);
    if (!ok) { remove(file); return 1; }
    return 0;
}

/* ITU-T G.711 A-law encoder */
static unsigned char alaw(int s)
{
    int sign = s < 0 ? 0x80 : 0, seg = 0;   /* after ^0xd5: bit 7 set = positive */
    if (s < 0) s = -s - 1;
    if (s > 32767) s = 32767;
    s >>= 3;
    while (seg < 7 && s >= (32 << seg)) seg++;
    int m = seg ? (s >> seg) & 0x0f : (s >> 1) & 0x0f;
    return (unsigned char)((sign | seg << 4 | m) ^ 0xd5);
}

/* Two soft notes with a quick decay; about 0.5 s. */
static int tone_cmd(const char *file)
{
    static const struct { double hz, start, len; } note[] = { { 880, 0.00, 0.22 }, { 1320, 0.16, 0.34 } };
    int n = (int)(RATE * 0.52);
    unsigned char h[WAV_HEAD];
    FILE *f = fopen(file, "wb");
    if (!f) return 1;
    memcpy(h, "RIFF", 4); le32(h + 4, 36 + n); memcpy(h + 8, "WAVEfmt ", 8); le32(h + 16, 16);
    le16(h + 20, 6); le16(h + 22, 1); le32(h + 24, RATE); le32(h + 28, RATE); le16(h + 32, 1); le16(h + 34, 8);
    memcpy(h + 36, "data", 4); le32(h + 40, n);
    fwrite(h, 1, sizeof h, f);
    for (int i = 0; i < n; i++) {
        double t = (double)i / RATE, v = 0;
        for (size_t k = 0; k < sizeof note / sizeof note[0]; k++) {
            double u = t - note[k].start;
            if (u < 0 || u > note[k].len) continue;
            double env = (u < 0.01 ? u / 0.01 : 1.0) * exp(-u * 9.0);
            v += 0.32 * env * sin(2 * M_PI * note[k].hz * u);
        }
        fputc(alaw((int)(v * 32767)), f);
    }
    return fclose(f) ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "listen")) {
        int s = argc > 2 ? atoi(argv[2]) : 600;
        return listen_cmd(s > 0 && s <= 3600 ? s : 600);
    }
    if (argc == 4 && !strcmp(argv[1], "recv")) return recv_cmd(argv[2], atol(argv[3]));
    if (argc == 3 && !strcmp(argv[1], "tone")) return tone_cmd(argv[2]);
    fprintf(stderr, "usage: snd listen [seconds] | snd recv <file> <bytes> | snd tone <file>\n");
    return 2;
}
