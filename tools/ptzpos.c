/* HC1703L Plus - read the pan/tilt position, or go to one. MIT License.
 *
 * Uses the same /dev/ptz ioctls as p2pcam: 0x81 range, 0x82 position, 0x83 go to position.
 *   ptzpos get          print "x y maxx maxy"
 *   ptzpos go <x> <y>   move there and wait until the motors are done
 *
 * The gears have about 5 steps of play, so the same position reached from the other side looks different.
 * "go" therefore always takes the last few steps from the same side.
 *
 * Build: zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -Os -static -s -o ptzpos ptzpos.c
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define OVER 12      /* steps past the target before the final approach */
#define PAN_SPS 30   /* slower than measured, so the wait is always long enough */
#define TILT_SPS 15

static int cx, cy;

static int move(int fd, int x, int y)
{
	int p[4] = { x, y, 0, 0 };
	int ms = abs(x - cx) * 1000 / PAN_SPS, tilt = abs(y - cy) * 1000 / TILT_SPS;

	if (ioctl(fd, 0x83, p) < 0) { perror("ioctl"); return -1; }
	usleep(((tilt > ms ? tilt : ms) + 300) * 1000);
	cx = x; cy = y;
	return 0;
}

int main(int argc, char **argv)
{
	int fd, r[4] = {0}, p[4] = {0};

	if (argc < 2) return 2;
	fd = open("/dev/ptz", O_RDWR);
	if (fd < 0) { perror("/dev/ptz"); return 1; }
	if (ioctl(fd, 0x81, r) < 0 || ioctl(fd, 0x82, p) < 0) { perror("ioctl"); return 1; }
	cx = p[0]; cy = p[1];
	if (!strcmp(argv[1], "get")) {
		printf("%d %d %d %d\n", p[0], p[1], r[0], r[1]);
	} else if (!strcmp(argv[1], "go") && argc == 4) {
		int x = atoi(argv[2]), y = atoi(argv[3]);
		int ox = x + OVER > r[0] ? r[0] : x + OVER, oy = y + OVER > r[1] ? r[1] : y + OVER;
		if (x < 0 || y < 0 || x > r[0] || y > r[1]) { fprintf(stderr, "outside range\n"); return 1; }
		if (x == cx && y == cy) return 0;
		if (move(fd, ox, oy) || move(fd, x, y)) return 1;
	} else return 2;
	close(fd);
	return 0;
}
