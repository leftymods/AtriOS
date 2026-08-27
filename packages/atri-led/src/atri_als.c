/*
 * atri-als - AtriStation ambient light sensor (LiteOn LTR-F216A @ i2c0:0x53)
 *
 * Reads the IIO illuminance channel exposed by ltrf216a and, optionally,
 * publishes lux to stdout / scales LED ring brightness to match ambient
 * light like Android did. Root not required once /dev/iio:device* perms set.
 *
 * Modes:
 *   atri-als                     print "lux=N" every second
 *   atri-als --once              single reading, exit
 *   atri-als --watch             continuous updates on change >10%
 *   atri-als --auto-led          + scale /sys brightness of led ring/screen
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

static int find_als_device(char *out, size_t outlen)
{
	DIR *d = opendir("/sys/bus/iio/devices");
	struct dirent *e;
	if (!d) return -1;
	while ((e = readdir(d))) {
		char path[512], buf[256];
		int fd;
		if (strncmp(e->d_name, "iio:device", 10)) continue;
		snprintf(path, sizeof(path), "/sys/bus/iio/devices/%s/name", e->d_name);
		fd = open(path, O_RDONLY);
		if (fd < 0) continue;
		int n = read(fd, buf, sizeof(buf) - 1);
		close(fd);
		if (n <= 0) continue;
		buf[n] = 0;
		if (strstr(buf, "ltrf216a") || strstr(buf, "ltr308")) {
			snprintf(out, outlen, "/dev/%s", e->d_name);
			closedir(d);
			return 0;
		}
	}
	closedir(d);
	return -1;
}

static long read_lux(const char *dev)
{
	char sysf[512];
	/* ltrf216a exposes in_illuminance_raw in sysfs; read via device name mapping */
	DIR *d = opendir("/sys/bus/iio/devices");
	struct dirent *e;
	long lux = -1;
	(void)dev;
	if (!d) return -1;
	while ((e = readdir(d))) {
		char p[600], buf[64];
		int fd, n;
		char nm[128];
		if (strncmp(e->d_name, "iio:device", 10)) continue;
		snprintf(p, sizeof(p), "/sys/bus/iio/devices/%s/name", e->d_name);
		fd = open(p, O_RDONLY);
		if (fd < 0) continue;
		n = read(fd, nm, sizeof(nm) - 1); close(fd);
		if (n <= 0) continue;
		nm[n] = 0;
		if (!strstr(nm, "ltrf216a") && !strstr(nm, "ltr308")) continue;
		snprintf(p, sizeof(p), "/sys/bus/iio/devices/%s/in_illuminance_raw", e->d_name);
		fd = open(p, O_RDONLY);
		if (fd < 0) { perror(p); continue; }
		n = read(fd, buf, sizeof(buf) - 1); close(fd);
		if (n <= 0) continue;
		buf[n] = 0;
		lux = atol(buf);
		break;
	}
	closedir(d);
	return lux;
}

/* map lux -> 0..100 % brightness band, similar to stock behaviour */
static int lux_to_brightness(long lux)
{
	if (lux < 0) return 50;
	if (lux < 5)   return 15;
	if (lux < 30)  return 30;
	if (lux < 80)  return 50;
	if (lux < 200) return 70;
	return 100;
}

static void apply_led_brightness(int pct)
{
	/* both ring controllers expose global brightness through our led tooling */
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "atri-led brightness %d >/dev/null 2>&1 &", pct);
	system(cmd);
}

int main(int argc, char **argv)
{
	int mode_once = 0, mode_watch = 0, mode_autoled = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--once")) mode_once = 1;
		else if (!strcmp(argv[i], "--watch")) mode_watch = 1;
		else if (!strcmp(argv[i], "--auto-led")) mode_autoled = mode_watch = 1;
	}

	char dev[256];
	if (find_als_device(dev, sizeof(dev)) != 0) {
		fprintf(stderr, "atri-als: no ltrf216a/ltr308 iio device found\n");
		return 2;
	}

	long last = -999999;
	while (1) {
		long lux = read_lux(dev);
		if (mode_once) { printf("lux=%ld\n", lux); return 0; }
		if (lux >= 0 && (last == -999999 ||
		    labs(lux - last) * 100 / (last ? last : 1) > 10)) {
			printf("%s lux=%ld\n",
			       ctime(&(time_t){time(NULL)}), lux);
			fflush(stdout);
			last = lux;
			if (mode_autoled)
				apply_led_brightness(lux_to_brightness(lux));
		}
		if (!mode_watch) break;
		usleep(500000);
	}
	return 0;
}
