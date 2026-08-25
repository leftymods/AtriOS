// SPDX-License-Identifier: GPL-2.0
/*
 * atri-knob - AtriStation laser volume knob tester.
 *
 * mode "input" (default): listen for REL_DIAL events produced by the
 *   rotary-poll daemon through uinput and report direction/steps.
 * mode "raw <chip_label> <off_a> <off_b>": poll two GPIO lines via sysfs
 *   and print live A/B quadrature state + decoded direction, independent
 *   of the daemon.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <dirent.h>
#include <linux/input.h>

static int find_dial_device(char *out, size_t outsz)
{
	DIR *d = opendir("/dev/input");
	struct dirent *de;
	char path[128], name[64];
	unsigned char bits[KEY_MAX / 8 + 1];

	while ((de = readdir(d))) {
		if (strncmp(de->d_name, "event", 5)) continue;
		snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0) continue;
		memset(bits, 0, sizeof(bits));
		if (ioctl(fd, EVIOCGBIT(EV_REL, sizeof(bits)), bits) >= 0 &&
		    (bits[REL_DIAL / 8] & (1 << (REL_DIAL % 8)))) {
			if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0)
				name[0] = '\0';
			snprintf(out, outsz, "%s (%s)", de->d_name, name);
			close(fd);
			closedir(d);
			return 0;
		}
		close(fd);
	}
	closedir(d);
	return -1;
}

static int input_mode(void)
{
	char devname[96];
	int total = 0;

	if (find_dial_device(devname, sizeof(devname)) < 0) {
		fprintf(stderr,
			"no REL_DIAL input device found "
			"(rotary-poll daemon not running, or no rotary hardware).\n"
			"Falling back to ADC volume pot mode...\n\n");
		return -1;
	}

	char path[112];
	snprintf(path, sizeof(path), "/dev/input/%.*s",
		 (int)strcspn(devname, " "), devname);
	printf("== atri-knob: listening on %s ==\n", path);
	printf("Rotate the knob. Ctrl+C to stop.\n\n");

	int fd = open(path, O_RDONLY);
	if (fd < 0) { perror("open"); return 1; }

	struct input_event ev;
	for (;;) {
		if (read(fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev)) {
			usleep(10000);
			continue;
		}
		if (ev.type == EV_REL && ev.code == REL_DIAL) {
			total += ev.value;
			printf("\r[%c] step %+d   total %+d      ",
			       ev.value > 0 ? '+' : '-', ev.value, total);
			fflush(stdout);
		}
	}
	return 0;
}

/* ---- raw GPIO quadrature view ---- */
static int gpio_read(const char *chip_label, int offset)
{
	/* resolve base for chip label via /sys/class/gpio chips */
	static char cache_label[64]; static int cache_base = -1;
	if (cache_base >= 0 && !strcmp(cache_label, chip_label))
		return cache_base + offset;

	DIR *d = opendir("/sys/class/gpio");
	struct dirent *de; char path[256], lbl[64]; int base = -1;
	while ((de = readdir(d))) {
		if (strncmp(de->d_name, "gpiochip", 8)) continue;
		snprintf(path, sizeof(path), "/sys/class/gpio/%s/label", de->d_name);
		int fd = open(path, O_RDONLY); if (fd < 0) continue;
		int n = read(fd, lbl, sizeof(lbl)-1); close(fd);
		if (n <= 0) continue;
		lbl[n] = 0;
		if (lbl[n-1] == '\n') lbl[--n] = 0;
		if (!strcmp(lbl, chip_label)) { sscanf(de->d_name+8, "%d", &base); break; }
	}
	closedir(d);
	if (base < 0) return -1;
	strncpy(cache_label, chip_label, sizeof(cache_label)-1);
	cache_base = base;

	char exp[32]; snprintf(exp, sizeof(exp), "%d", base + offset);
	int fd = open("/sys/class/gpio/export", O_WRONLY);
	if (fd >= 0) { write(fd, exp, strlen(exp)); close(fd); usleep(10000); }
	return base + offset;
}

static int gval(int gpio)
{
	char p[64]; char v = '0';
	snprintf(p, sizeof(p), "/sys/class/gpio/gpio%d/value", gpio);
	int fd = open(p, O_RDONLY);
	if (fd < 0) return -1;
	read(fd, &v, 1); close(fd);
	return v - '0';
}

static int raw_mode(const char *label, int oa, int ob)
{
	int ga = gpio_read(label, oa), gb = gpio_read(label, ob);
	if (ga < 0 || gb < 0) { fprintf(stderr, "gpio export failed\n"); return 1; }

	const char *quad[4] = { "00", "01", "11", "10" };
	int last = -1;
	printf("== atri-knob raw: %s:%d (A) / :%d (B) ==\n", label, oa, ob);
	for (;;) {
		int a = gval(ga), b = gval(gb);
		int s = (a << 1) | b;
		if (s != last && a >= 0 && b >= 0) {
			printf("\rA=%d B=%d [%s]  ", a, b, quad[s]);
			fflush(stdout);
			last = s;
		}
		usleep(5000);
	}
}

/* ---- SAR ADC volume pot (actual knob wiring on AtriStation) ---- */
static int find_adc(char *path, size_t sz)
{
	DIR *d = opendir("/sys/bus/iio/devices");
	struct dirent *de;
	char p[256];

	while ((de = readdir(d))) {
		if (strncmp(de->d_name, "iio:device", 10)) continue;
		snprintf(p, sizeof(p), "/sys/bus/iio/devices/%s/in_voltage0_raw",
			 de->d_name);
		int fd = open(p, O_RDONLY);
		if (fd < 0) continue;
		close(fd);
		snprintf(path, sz, "%s", p);
		closedir(d);
		return 0;
	}
	closedir(d);
	return -1;
}

static int adc_mode(void)
{
	char paths[8][160];
	int nch = 0;

	DIR *d = opendir("/sys/bus/iio/devices/iio:device0");
	if (d) {
		struct dirent *de;
		while ((de = readdir(d)) && nch < 8) {
			size_t L = strlen(de->d_name);
			if (L > 24 || strncmp(de->d_name, "in_voltage", 10))
				continue;
			if (strcmp(de->d_name + L - 4, "_raw")) continue;
			snprintf(paths[nch], sizeof(paths[0]),
				 "/sys/bus/iio/devices/iio:device0/%s", de->d_name);
			nch++;
		}
		closedir(d);
	}
	if (!nch) {
		fprintf(stderr, "no IIO ADC channels found (saradc up?)\n");
		return 1;
	}

	printf("== atri-knob adc: %d channel(s), live view ==\n", nch);
	printf("Turn the volume knob - watch which chN moves. Ctrl+C stop.\n\n");

	int last[8];
	for (int i = 0; i < nch; i++) last[i] = -1;

	for (;;) {
		char line[512];
		int off = 0, moved = -1;
		for (int i = 0; i < nch; i++) {
			char buf[32];
			int v = last[i];
			int fd = open(paths[i], O_RDONLY);
			if (fd >= 0) {
				int n = read(fd, buf, sizeof(buf) - 1);
				close(fd);
				if (n > 0) { buf[n] = 0; v = atoi(buf); }
			}
			off += snprintf(line + off, sizeof(line) - off,
				       "%sch%d%c%d", off ? "  " : "", i,
				       last[i] >= 0 && abs(v - last[i]) >= 12
					       ? '*' : '=', v);
			if (last[i] >= 0 && abs(v - last[i]) >= 12) moved = i;
			last[i] = v;
		}
		printf("\r%-70s%s", line, moved >= 0 ? "  <-- MOVE" : "");
		fflush(stdout);
		usleep(150000);
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "adc"))
		return adc_mode();
	if (argc > 1 && !strcmp(argv[1], "raw") && argc >= 5)
		return raw_mode(argv[2], atoi(argv[3]), atoi(argv[4]));
	if (argc > 1 && !strcmp(argv[1], "-h")) {
		printf("usage: atri-knob [adc | raw <chip_label> <off_a> <off_b>]\n");
		printf("  default: REL_DIAL listener; auto-falls back to adc mode\n");
		return 0;
	}
	int rc = input_mode();
	if (rc == -1)
		rc = adc_mode();
	return rc;
}
