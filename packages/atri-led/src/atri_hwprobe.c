/*
 * atri-hwprobe — enumerate what the system actually sees:
 *   GPIO chips + per-line consumers, I2C buses with address scan +
 *   device identification, SPI devices + bound drivers, input devices
 *   with capabilities, LED/backlight classes.
 *
 * Read-only by default. Optional deep tests:
 *   --i2c-read       read ID/status registers from known chips (safe)
 *
 * Build: cc -O2 -Wall -o atri-hwprobe atri_hwprobe.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <time.h>

static int opt_i2c_read = 0;
static int opt_watch_sec = 0;

#define P(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define SEP()       P("-----------------------------------------------------------")

/* ---------- known-device identification ---------- */

struct addr_id { uint8_t addr; const char *name; };

static const struct addr_id known_i2c[] = {
	{ 0x08, "ES8156 DAC (AtriStation: external I2S codec)" },
	{ 0x10, "IS31FL3236 LED driver (alt addr)" },
	{ 0x18, "various (codec/sensor)" },
	{ 0x2a, "SY6045S amplifier — TWEETERS" },
	{ 0x2b, "SY6045S amplifier — WOOFER (PBTL)" },
	{ 0x3c, "IS31FL3236 LED ring driver" },
	{ 0x3f, "IS31FL3236 LED ring driver (2nd)" },
	{ 0x40, "ES7210 four-ch ADC (mic/feedback)" },
	{ 0x50, "EEPROM range" },
	{ 0x51, "EEPROM range" },
	{ 0x64, "RK8xx-class PMIC (not expected here)" },
	{ 0, NULL }
};

static const char *identify_i2c(uint8_t addr)
{
	for (int i = 0; known_i2c[i].name; i++)
		if (known_i2c[i].addr == addr)
			return known_i2c[i].name;
	return "unknown";
}

/* ---------- GPIO chips ---------- */

static void probe_gpio(void)
{
	DIR *d = opendir("/dev");
	struct dirent *de;
	int found = 0;

	P("== GPIO chips (/dev/gpiochip*) ==");
	if (!d) { P("  opendir failed: %s", strerror(errno)); return; }

	while ((de = readdir(d))) {
		char path[300];
		struct gpiochip_info info;

		if (strncmp(de->d_name, "gpiochip", 8) != 0)
			continue;
		found++;
		snprintf(path, sizeof(path), "/dev/%s", de->d_name);
		int fd = open(path, O_RDONLY);
		if (fd < 0) {
			P("  %s: open: %s", path, strerror(errno));
			continue;
		}
		if (ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info)) {
			P("  %s: chipinfo: %s", path, strerror(errno));
			close(fd);
			continue;
		}
		P("  %s: '%s' (%u lines)", path, info.name, info.lines);

		/* print lines that are in use (consumers tell the story) */
		unsigned used = 0;
		for (unsigned off = 0; off < info.lines; off++) {
			struct gpioline_info li;
			memset(&li, 0, sizeof(li));
			li.line_offset = off;
			if (ioctl(fd, GPIO_GET_LINEINFO_IOCTL, &li))
				continue;
			if (li.flags & GPIOLINE_FLAG_KERNEL) {
				P("    line %3u: [kernel] %s",
				  off, li.consumer);
				used++;
			}
		}
		if (!used)
			P("    (no kernel-held lines)");
		close(fd);
	}
	closedir(d);
	if (!found) P("  none found");
}

/* ---------- I2C ---------- */

static void i2c_identify_chip(int fd, uint8_t addr)
{
	/* safe register reads on chips we expect on this board */
	uint8_t reg; int got;
	unsigned char val;

	switch (addr) {
	case 0x2a: case 0x2b: /* SY6045S: chip version 0x3E */
		reg = 0x3E;
		if (write(fd, &reg, 1) == 1 &&
		    (got = read(fd, &val, 1)) == 1)
			P("      SY6045S ver-reg 0x3E = 0x%02x", val);
		break;
	case 0x40: /* ES7210: chip id regs 0x3D/0x3E */
		reg = 0x3D;
		if (write(fd, &reg, 1) == 1 &&
		    (got = read(fd, &val, 1)) == 1) {
			uint8_t v1 = val;
			reg = 0x3E;
			if (write(fd, &reg, 1) == 1 &&
			    read(fd, &val, 1) == 1)
				P("      ES7210 id 0x3D=0x%02x 0x3E=0x%02x",
				  v1, val);
		}
		break;
	default:
		break;
	}
}

static void probe_i2c(int deep)
{
	DIR *d = opendir("/dev");
	struct dirent *de;

	P("== I2C buses (/dev/i2c-*) ==");
	if (!d) { P("  opendir failed: %s", strerror(errno)); return; }

	while ((de = readdir(d))) {
		char path[300];
		int fd;
		unsigned long funcs = 0;

		if (strncmp(de->d_name, "i2c-", 4) != 0)
			continue;
		snprintf(path, sizeof(path), "/dev/%s", de->d_name);
		fd = open(path, O_RDWR);
		if (fd < 0) {
			P("  %s: %s", path, strerror(errno));
			continue;
		}
		ioctl(fd, I2C_FUNCS, &funcs);
		P("  %s%s", path,
		  (funcs & I2C_FUNC_I2C) ? "" : " (SMBus only!)");

		/* which driver owns this bus? */
		{
			char link[300], tgt[300];
			ssize_t n;
			snprintf(link, sizeof(link),
				 "/sys/bus/i2c/devices/%s/device/driver",
				 de->d_name);
			n = readlink(link, tgt, sizeof(tgt) - 1);
			if (n > 0) {
				tgt[n] = '\0';
				char *sl = strrchr(tgt, '/');
				P("    adapter driver: %s",
				  sl ? sl + 1 : tgt);
			}
		}

		for (uint8_t a = 0x03; a <= 0x77; a++) {
			if (ioctl(fd, I2C_SLAVE_FORCE, a) < 0)
				continue;
			/* probe: plain read of one byte */
			unsigned char b;
			if (read(fd, &b, 1) == 1) {
				P("    0x%02x RESPONDS — %s",
				  a, identify_i2c(a));
				if (deep)
					i2c_identify_chip(fd, a);
			}
		}
		close(fd);
	}
	closedir(d);
}

/* ---------- SPI ---------- */

static void probe_spi(void)
{
	DIR *d = opendir("/sys/bus/spi/devices");

	P("== SPI devices ==");
	if (!d) { P("  /sys/bus/spi/devices: %s", strerror(errno)); return; }
	struct dirent *de;
	while ((de = readdir(d))) {
		char pmod[400], pdrv[400], tgt[400];
		ssize_t n;
		if (de->d_name[0] == '.') continue;
		P("  %s", de->d_name);
		snprintf(pmod, sizeof(pmod), "/sys/bus/spi/devices/%s/modalias",
			 de->d_name);
		int fd = open(pmod, O_RDONLY);
		if (fd >= 0) {
			char m[128] = "";
			int k = read(fd, m, sizeof(m) - 1);
			close(fd);
			if (k > 0) { m[k ? k : 0] = '\0';
				     if (k && m[k-1]=='\n') m[k-1]='\0';
				     P("    modalias: %s", m); }
		}
		snprintf(pdrv, sizeof(pdrv), "/sys/bus/spi/devices/%s/driver",
			 de->d_name);
		n = readlink(pdrv, tgt, sizeof(tgt) - 1);
		if (n > 0) {
			tgt[n] = '\0';
			char *sl = strrchr(tgt, '/');
			P("    driver: %s", sl ? sl + 1 : tgt);
		} else {
			P("    driver: UNBOUND");
		}
	}
	closedir(d);
}

/* ---------- input ---------- */

static void probe_input(void)
{
	DIR *d = opendir("/dev/input");

	P("== input devices (/dev/input/event*, REL/KEY caps) ==");
	if (!d) { P("  opendir: %s", strerror(errno)); return; }
	struct dirent *de;
	while ((de = readdir(d))) {
		char path[300], name[256] = "?";
		uint8_t rel[REL_MAX/8+1] = {0}, key[KEY_MAX/8+1] = {0};
		int fd;

		if (strncmp(de->d_name, "event", 5) != 0) continue;
		snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
		fd = open(path, O_RDONLY);
		if (fd < 0) continue;
		ioctl(fd, EVIOCGNAME(sizeof(name)), name);
		int has_rel = ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel)), rel) > 0;
		int has_key = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key)), key) > 0;

		P("  %s: %s", path, name);
		if (has_rel && (rel[REL_DIAL/8]&1<<(REL_DIAL%8)))
			P("    REL_DIAL   <- volume knob (rotary-poll)");
		if (has_rel && (rel[REL_WHEEL/8]&1<<(REL_WHEEL%8)))
			P("    REL_WHEEL");
		if (has_key && (key[KEY_VOLUMEUP/8]&1<<(KEY_VOLUMEUP%8)))
			P("    KEY_VOLUMEUP/DOWN");
		if (has_rel && !rel[REL_DIAL/8] && !rel[REL_WHEEL/8]) {
			/* list any other rel bits present */
			P("    other REL axes present");
		}
		close(fd);
	}
	closedir(d);
}

/* ---------- leds / backlight ---------- */

static void sys_dir_list(const char *cls, const char *note)
{
	char base[128];
	DIR *d;

	snprintf(base, sizeof(base), "/sys/class/%s", cls);
	d = opendir(base);
	P("== %s (%s) ==", cls, note);
	if (!d) { P("  %s: %s", base, strerror(errno)); return; }
	struct dirent *de;
	while ((de = readdir(d))) {
		char pp[512];
		if (de->d_name[0] == '.') continue;
		snprintf(pp, sizeof(pp), "%s/%s/max_brightness", base,
			 de->d_name);
		int fd = open(pp, O_RDONLY);
		if (fd >= 0) {
			char b[32] = ""; int k = read(fd, b, 31); close(fd);
			if (k > 0) b[(k>0&&b[k-1]=='\n')?k-1:k] = '\0';
			P("  %-24s max=%s", de->d_name, b);
		} else {
			P("  %s", de->d_name);
		}
	}
	closedir(d);
}

static void probe_tty(void)
{
	DIR *d = opendir("/sys/class/tty");

	P("== serial ports (/sys/class/tty, bound drivers) ==");
	if (!d) { P("  opendir: %s", strerror(errno)); return; }
	struct dirent *de;
	while ((de = readdir(d))) {
		char pdev[600], plink[600], tgt[600];
		ssize_t n;
		char name[300];
		snprintf(name, sizeof(name), "%s", de->d_name);
		/* only real hardware ports */
		if (strncmp(name, "ttyAML", 6) && strncmp(name, "ttyUSB", 6) &&
		    strncmp(name, "ttyACM", 6) && strncmp(name, "ttyS", 4))
			continue;
		P("  /dev/%-10s", name);
		snprintf(pdev, sizeof(pdev), "/sys/class/tty/%s/device",
			 name);
		n = readlink(pdev, plink, sizeof(plink) - 1);
		if (n > 0) {
			plink[n] = '\0';
			char *sl = strrchr(plink, '/');
			P("      device: %s", sl ? sl + 1 : plink);
		}
		snprintf(pdev, sizeof(pdev),
			 "/sys/class/tty/%s/device/driver", name);
		n = readlink(pdev, tgt, sizeof(tgt) - 1);
		if (n > 0) {
			tgt[n] = '\0';
			char *sl = strrchr(tgt, '/');
			P("      driver: %s", sl ? sl + 1 : tgt);
		}
	}
	closedir(d);
	P("  hints: ttyAML0=console ttyAML1=BT(uart_A) ttyAML2=Zigbee(uart_AO_B)");
}

static struct gpiochip_info info_tmp;


static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
#define WATCH_LOG(...) do { printf("[watch] "); printf(__VA_ARGS__); putchar(0x0a); } while (0)

/* ---- live gpio watcher: reveal encoder/unknown pins by motion ---- */

#include <linux/gpio.h>

#define WATCH_MAXLINES 128

struct snap { unsigned int lines; uint8_t val[WATCH_MAXLINES]; };

static int chip_watch(const char *path, struct snap *s)
{
	int fd = open(path, O_RDONLY);
	struct gpiohandle_request req;
	int i, ret;

	if (fd < 0) return -1;
	memset(&req, 0, sizeof(req));
	if (ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info_tmp) < 0) {
		close(fd); return -1;
	}
	s->lines = info_tmp.lines > WATCH_MAXLINES ? WATCH_MAXLINES
						   : info_tmp.lines;
	/* request all lines as inputs (nonexclusive fails on claimed:
	 * those are skipped silently — we only watch free lines) */
	for (i = 0; i < (int)s->lines; i++)
		req.lineoffsets[i] = i;
	req.lines = s->lines;
	req.flags = GPIOHANDLE_REQUEST_INPUT;
	ret = ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req);
	if (ret < 0) { close(fd); return -2; }	/* some lines claimed */

	struct gpiohandle_data data;
	memset(&data, 0, sizeof(data));
	if (ioctl(req.fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0) {
		close(req.fd); close(fd); return -3;
	}
	for (i = 0; i < (int)s->lines; i++)
		s->val[i] = data.values[i] & 1;
	close(req.fd);
	close(fd);
	return 0;
}

static void watch_gpio(int seconds)
{
	DIR *d = opendir("/dev");
	struct dirent *de;
	char chips[16][64];
	int nchips = 0;

	WATCH_LOG("watching free GPIO lines for %ds — rotate the knob now!", seconds);
	if (!d) return;
	while ((de = readdir(d)) && nchips < 16) {
		if (strncmp(de->d_name, "gpiochip", 8)) continue;
		if (strlen(de->d_name) >= 56) continue;
		snprintf(chips[nchips++], sizeof(chips[0]), "/dev/%s", de->d_name);
	}
	closedir(d);

	struct snap prev[16], cur[16];
	int valid[16] = {0};
	long end = now_ms() + seconds * 1000L;

	/* initial snapshots best-effort */
	for (int i = 0; i < nchips; i++) {
		if (chip_watch(chips[i], &prev[i]) == 0)
			valid[i] = 1;
	}

	while (now_ms() < end) {
		usleep(120000);
		for (int i = 0; i < nchips; i++) {
			if (!valid[i]) continue;
			if (chip_watch(chips[i], &cur[i]) != 0)
				continue;
			for (unsigned l = 0; l < cur[i].lines; l++) {
				if (valid[i] && cur[i].val[l] != prev[i].val[l]) {
					WATCH_LOG("  %s line %u: %u -> %u",
					    chips[i], l,
					    prev[i].val[l], cur[i].val[l]);
				}
			}
			memcpy(&prev[i], &cur[i], sizeof(cur[i]));
		}
	}
	WATCH_LOG("watch done");
}

int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--i2c-read") == 0)
			opt_i2c_read = 1;
		else if (strcmp(argv[i], "--watch-gpio") == 0)
			opt_watch_sec = argc > i+1 ? atoi(argv[++i]) : 15;
		else if (strcmp(argv[i], "--help") == 0 ||
			 strcmp(argv[i], "-h") == 0) {
			P("usage: atri-hwprobe [--i2c-read] [--watch-gpio <seconds>]");
			return 0;
		}
	}

	P("=== atri-hwprobe: what is connected? ===");
	if (opt_watch_sec > 0) {
		watch_gpio(opt_watch_sec);
		return 0;
	}
	probe_gpio();  SEP();
	probe_i2c(opt_i2c_read);  SEP();
	probe_spi();   SEP();
	probe_input(); SEP();
	sys_dir_list("leds", "LED class");
	SEP();
	sys_dir_list("backlight", "backlight class");
	SEP();
	probe_tty();
	SEP();
	P("(identification hints are AtriStation-specific; 'unknown' just "
	  "means not on our board map)");
	return 0;
}
