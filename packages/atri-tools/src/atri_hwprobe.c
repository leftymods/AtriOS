/*
 * atri_hwprobe.c - Unified hardware probe and diagnostics for AtriOS
 *
 * Enumerate hardware state:
 *   - GPIO chips + pin consumers
 *   - I2C buses with address scan + chip identification
 *   - SPI devices + bound drivers
 *   - Input devices with event capabilities
 *   - LED and Backlight classes
 *   - UART / TTY devices
 *
 * Integrated subcommands (can be invoked directly or via symlinks):
 *   atri-hwprobe [full probe]
 *   atri-hwprobe pcba    (or as atri-pcba)
 *   atri-hwprobe als     (or as atri-als)
 *   atri-hwprobe buttons (or as atri-buttons)
 *   atri-hwprobe knob    (or as atri-knob)
 *
 * Copyright (c) 2026 leftymods / AtriOS Project
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <time.h>

#define P(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define SEP()       P("-----------------------------------------------------------")

/* ---------- known-device identification ---------- */

struct addr_id { uint8_t addr; const char *name; };

static const struct addr_id known_i2c[] = {
	{ 0x08, "ES8156 DAC (external I2S codec)" },
	{ 0x10, "IS31FL3236 LED driver (alt addr)" },
	{ 0x18, "various (codec/sensor)" },
	{ 0x2a, "SY6045S amplifier — TWEETERS" },
	{ 0x2b, "SY6045S amplifier — WOOFER (PBTL)" },
	{ 0x3c, "IS31FL3236 LED ring driver" },
	{ 0x3f, "IS31FL3236 LED ring driver (2nd)" },
	{ 0x40, "ES7210 four-ch ADC (mic/feedback)" },
	{ 0x50, "EEPROM range (mainboard ID)" },
	{ 0x51, "EEPROM range" },
	{ 0x52, "EEPROM range (LED ring ID)" },
	{ 0x53, "LTR-308ALS / LTRF216A Ambient Light Sensor" },
	{ 0x56, "EEPROM range (mics ID)" },
	{ 0, NULL }
};

static const char *identify_i2c(uint8_t addr)
{
	for (int i = 0; known_i2c[i].name; i++)
		if (known_i2c[i].addr == addr)
			return known_i2c[i].name;
	return "unknown";
}

/* ---------- GPIO probe ---------- */

static void probe_gpio(void)
{
	DIR *d = opendir("/dev");
	struct dirent *de;
	P("[GPIO] enumerating chips in /dev/gpiochip*:");
	if (!d) { P("  cannot open /dev"); return; }
	while ((de = readdir(d))) {
		if (strncmp(de->d_name, "gpiochip", 8) != 0) continue;
		char p[64]; snprintf(p, sizeof(p), "/dev/%s", de->d_name);
		int fd = open(p, O_RDONLY);
		if (fd < 0) { P("  %s: open failed (%s)", p, strerror(errno)); continue; }
		struct gpiochip_info cinfo;
		if (ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &cinfo) == 0) {
			P("  %s: label='%s' lines=%u", p, cinfo.label, cinfo.lines);
			int used = 0;
			for (uint32_t i = 0; i < cinfo.lines; i++) {
				struct gpioline_info linfo;
				linfo.line_offset = i;
				if (ioctl(fd, GPIO_GET_LINEINFO_IOCTL, &linfo) == 0 &&
				    (linfo.flags & GPIOLINE_FLAG_IS_OUT || linfo.consumer[0])) {
					P("    line %3u: '%s' consumer='%s' flags=0x%x",
					  i, linfo.name, linfo.consumer, linfo.flags);
					used++;
				}
			}
			if (!used) P("    (no named/active lines reported by kernel)");
		}
		close(fd);
	}
	closedir(d);
}

/* ---------- I2C probe ---------- */

static void probe_i2c(int deep_read)
{
	DIR *d = opendir("/dev");
	struct dirent *de;
	P("[I2C] scanning /dev/i2c-* for active devices:");
	if (!d) return;
	while ((de = readdir(d))) {
		if (strncmp(de->d_name, "i2c-", 4) != 0) continue;
		char p[64]; snprintf(p, sizeof(p), "/dev/%s", de->d_name);
		int fd = open(p, O_RDWR);
		if (fd < 0) { P("  %s: open failed", p); continue; }
		P("  %s:", p);
		int found = 0;
		for (uint8_t a = 0x03; a < 0x78; a++) {
			if (ioctl(fd, I2C_SLAVE_FORCE, a) < 0) continue;
			uint8_t dummy = 0;
			int r = write(fd, &dummy, 0);
			if (r == 0) {
				found++;
				P("    0x%02x: %s", a, identify_i2c(a));
				if (deep_read) {
					uint8_t b = 0;
					if (read(fd, &b, 1) == 1)
						P("         -> read byte: 0x%02x", b);
				}
			}
		}
		if (!found) P("    (bus idle / no ACK on scan)");
		close(fd);
	}
	closedir(d);
}

/* ---------- SPI probe ---------- */

static void probe_spi(void)
{
	DIR *d = opendir("/dev");
	struct dirent *de;
	P("[SPI] enumerating /dev/spidev*:");
	if (!d) return;
	int count = 0;
	while ((de = readdir(d))) {
		if (strncmp(de->d_name, "spidev", 6) != 0) continue;
		P("  /dev/%s", de->d_name);
		count++;
	}
	if (!count) P("  (no /dev/spidev* nodes; SPI devices may be driver-bound like Gowin FPGA)");
	closedir(d);
}

/* ---------- Input devices probe ---------- */

static void probe_input(void)
{
	DIR *d = opendir("/dev/input");
	struct dirent *de;
	P("[INPUT] devices in /dev/input/:");
	if (!d) return;
	while ((de = readdir(d))) {
		if (strncmp(de->d_name, "event", 5) != 0) continue;
		char p[64], name[128] = {0};
		snprintf(p, sizeof(p), "/dev/input/%s", de->d_name);
		int fd = open(p, O_RDONLY | O_NONBLOCK);
		if (fd >= 0) {
			ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
			P("  %s: '%s'", p, name[0] ? name : "unnamed");
			close(fd);
		}
	}
	closedir(d);
}

/* ---------- Sysfs listing ---------- */

static void sys_dir_list(const char *sub, const char *title)
{
	char path[128]; snprintf(path, sizeof(path), "/sys/class/%s", sub);
	DIR *d = opendir(path);
	struct dirent *de;
	P("[%s] %s:", title, path);
	if (!d) { P("  (not present)"); return; }
	int count = 0;
	while ((de = readdir(d))) {
		if (de->d_name[0] == '.') continue;
		P("  %s", de->d_name);
		count++;
	}
	if (!count) P("  (empty)");
	closedir(d);
}

/* ---------- TTY probe ---------- */

static void probe_tty(void)
{
	const char *ttys[] = { "/dev/ttyAML0", "/dev/ttyAML1", "/dev/ttyAML2", "/dev/ttyZigbee", NULL };
	P("[SERIAL / TTY] ports:");
	for (int i = 0; ttys[i]; i++) {
		if (access(ttys[i], F_OK) == 0) {
			const char *hint = "";
			if (!strcmp(ttys[i], "/dev/ttyAML1")) hint = " (Bluetooth)";
			else if (!strcmp(ttys[i], "/dev/ttyAML2")) hint = " (Zigbee port)";
			else if (!strcmp(ttys[i], "/dev/ttyZigbee")) hint = " (Zigbee symlink)";
			P("  %s%s", ttys[i], hint);
		}
	}
}

/* ---------- SUBCOMMAND: PCBA ---------- */

static int read_eeprom(uint8_t addr, unsigned char *buf, int len)
{
	int fd = -1;
	const char *candidates[] = { "/dev/i2c-0", "/dev/i2c-1", "/dev/i2c-2", NULL };
	for (int i = 0; candidates[i]; i++) {
		fd = open(candidates[i], O_RDWR);
		if (fd >= 0) {
			if (ioctl(fd, I2C_SLAVE_FORCE, addr) == 0) break;
			close(fd);
			fd = -1;
		}
	}
	if (fd < 0) return -1;
	uint8_t off = 0;
	if (write(fd, &off, 1) != 1) { /* seek */ }
	int got = 0;
	while (got < len) {
		int r = read(fd, buf + got, len - got);
		if (r <= 0) break;
		got += r;
	}
	close(fd);
	return got;
}

static void json_str(const unsigned char *d, int n, char *out, int outn)
{
	int s = 0;
	while (s < n && (d[s] == 0 || d[s] == 0xFF)) s++;
	int e = n;
	while (e > s && (d[e-1] == 0 || d[e-1] == 0xFF)) e--;
	int j = 0;
	for (int i = s; i < e && j < outn - 1; i++) {
		unsigned char c = d[i];
		if (c == '"' || c == '\\') out[j++] = '\\';
		if (c >= 0x20 && c < 0x7F) out[j++] = c;
	}
	out[j] = 0;
}

static int run_pcba(int argc, char **argv)
{
	int raw_idx = -1;
	if (argc >= 3 && !strcmp(argv[2], "--raw"))
		raw_idx = atoi(argv[3]);
	else if (argc >= 2 && !strcmp(argv[1], "--raw"))
		raw_idx = atoi(argv[2]);

	#define PCBA_LEN 32
	unsigned char data[5][PCBA_LEN] = {0};
	const char *names[5] = { "mainboard", "led_display", "led_ring", "mics", "dc_ethernet" };
	const uint8_t addrs[5] = { 0x50, 0x50, 0x52, 0x56, 0x51 };

	if (raw_idx >= 0 && raw_idx <= 4) {
		unsigned char b[PCBA_LEN]; memset(b, 0, sizeof(b));
		int g = read_eeprom(addrs[raw_idx], b, PCBA_LEN);
		printf("# %s @0x%02X (%d bytes)\n", names[raw_idx], addrs[raw_idx], g);
		for (int i = 0; i < g; i++) printf("%02x ", b[i]);
		printf("\n");
		return 0;
	}

	printf("{");
	for (int i = 0; i < 5; i++) {
		char val[128];
		read_eeprom(addrs[i], data[i], PCBA_LEN);
		json_str(data[i], PCBA_LEN, val, sizeof(val));
		printf("%s\"%s\":\"%s\"", i ? "," : "", names[i], val);
	}
	printf("}\n");
	return 0;
}

/* ---------- SUBCOMMAND: ALS (Ambient Light) ---------- */

static int run_als(int argc, char **argv)
{
	int mode_once = 0, mode_watch = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--once")) mode_once = 1;
		else if (!strcmp(argv[i], "--watch")) mode_watch = 1;
	}

	const char *paths[] = {
		"/sys/bus/iio/devices/iio:device0/in_illuminance_raw",
		"/sys/bus/iio/devices/iio:device0/in_illuminance_input",
		NULL
	};

	int fd = -1;
	char buf[32];
	for (int i = 0; paths[i]; i++) {
		fd = open(paths[i], O_RDONLY);
		if (fd >= 0) break;
	}
	if (fd < 0) {
		fprintf(stderr, "atri-als: ambient light sensor not found in IIO devices\n");
		return 1;
	}

	long last = -1;
	for (;;) {
		lseek(fd, 0, SEEK_SET);
		ssize_t n = read(fd, buf, sizeof(buf) - 1);
		if (n > 0) {
			buf[n] = '\0';
			long lux = atol(buf);
			if (mode_once) {
				printf("lux=%ld\n", lux);
				close(fd);
				return 0;
			}
			if (lux != last) {
				printf("lux=%ld\n", lux);
				fflush(stdout);
				last = lux;
			}
		}
		if (!mode_watch) break;
		usleep(500000);
	}
	close(fd);
	return 0;
}

/* ---------- SUBCOMMAND: BUTTONS ---------- */

static int run_buttons(int argc, char **argv)
{
	(void)argc; (void)argv;
	DIR *d = opendir("/dev/input");
	struct dirent *de;
	int fds[16];
	char names[16][64];
	int nfds = 0;

	if (!d) return 1;
	while ((de = readdir(d))) {
		if (strncmp(de->d_name, "event", 5)) continue;
		if (nfds >= 16) break;
		char p[64]; snprintf(p, sizeof(p), "/dev/input/%s", de->d_name);
		int fd = open(p, O_RDONLY | O_NONBLOCK);
		if (fd < 0) continue;
		ioctl(fd, EVIOCGNAME(sizeof(names[0]) - 1), names[nfds]);
		fds[nfds++] = fd;
	}
	closedir(d);

	printf("Listening for button presses on %d input devices. Press buttons or Ctrl+C...\n", nfds);
	struct pollfd pfds[16];
	for (;;) {
		for (int i = 0; i < nfds; i++) { pfds[i].fd = fds[i]; pfds[i].events = POLLIN; }
		if (poll(pfds, nfds, -1) <= 0) break;
		for (int i = 0; i < nfds; i++) {
			if (!(pfds[i].revents & POLLIN)) continue;
			struct input_event ev;
			while (read(fds[i], &ev, sizeof(ev)) == sizeof(ev)) {
				if (ev.type == EV_KEY) {
					printf("[%s] key code=%d value=%d\n",
					       names[i], ev.code, ev.value);
					fflush(stdout);
				}
			}
		}
	}
	return 0;
}

/* ---------- SUBCOMMAND: KNOB ---------- */

static int run_knob(int argc, char **argv)
{
	(void)argc; (void)argv;
	DIR *d = opendir("/dev/input");
	struct dirent *de;
	char path[128] = {0};
	if (!d) return 1;
	while ((de = readdir(d))) {
		if (strncmp(de->d_name, "event", 5)) continue;
		char p[64]; snprintf(p, sizeof(p), "/dev/input/%s", de->d_name);
		int fd = open(p, O_RDONLY | O_NONBLOCK);
		if (fd < 0) continue;
		unsigned char rel_bits[REL_MAX / 8 + 1] = {0};
		unsigned char key_bits[KEY_MAX / 8 + 1] = {0};
		int has_dial = (ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel_bits)), rel_bits) >= 0) &&
			       ((rel_bits[REL_DIAL / 8] & (1 << (REL_DIAL % 8))) ||
			        (rel_bits[REL_WHEEL / 8] & (1 << (REL_WHEEL % 8))));
		int has_keys = (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) >= 0) &&
			       ((key_bits[KEY_VOLUMEUP / 8] & (1 << (KEY_VOLUMEUP % 8))) &&
			        (key_bits[KEY_VOLUMEDOWN / 8] & (1 << (KEY_VOLUMEDOWN % 8))));
		close(fd);
		if (has_dial || has_keys) { strncpy(path, p, sizeof(path) - 1); break; }
	}
	closedir(d);

	if (!path[0]) {
		fprintf(stderr, "atri-knob: no rotary encoder input device detected\n");
		return 1;
	}

	printf("Listening for knob rotation on %s. Turn knob or Ctrl+C...\n", path);
	int fd = open(path, O_RDONLY);
	if (fd < 0) return 1;
	struct input_event ev;
	int total = 0;
	while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
		if (ev.type == EV_REL) {
			total += ev.value;
			printf("\rknob step: %+d (total: %+d)    ", ev.value, total);
			fflush(stdout);
		} else if (ev.type == EV_KEY && ev.value == 1) {
			int s = (ev.code == KEY_VOLUMEUP) ? 1 : (ev.code == KEY_VOLUMEDOWN) ? -1 : 0;
			total += s;
			printf("\rknob key step: %+d (total: %+d)    ", s, total);
			fflush(stdout);
		}
	}
	close(fd);
	return 0;
}

/* ---------- MAIN ---------- */

int main(int argc, char **argv)
{
	/* Check if invoked via symlink or with explicit subcommand */
	const char *prog = argv[0];
	if (strstr(prog, "pcba") || (argc > 1 && !strcmp(argv[1], "pcba")))
		return run_pcba(argc, argv);
	if (strstr(prog, "als") || (argc > 1 && !strcmp(argv[1], "als")))
		return run_als(argc, argv);
	if (strstr(prog, "buttons") || (argc > 1 && !strcmp(argv[1], "buttons")))
		return run_buttons(argc, argv);
	if (strstr(prog, "knob") || (argc > 1 && !strcmp(argv[1], "knob")))
		return run_knob(argc, argv);

	int opt_i2c_read = 0;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--i2c-read") == 0)
			opt_i2c_read = 1;
		else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			P("Usage: atri-hwprobe [subcommand] [options]");
			P("Subcommands:");
			P("  (default)  Full hardware bus enumeration");
			P("  pcba       Read board identity EEPROMs as JSON");
			P("  als        Read ambient light sensor (ltr308/ltrf216a)");
			P("  buttons    Listen for hardware button events");
			P("  knob       Listen for rotary encoder volume steps");
			return 0;
		}
	}

	P("=== AtriOS Unified Hardware Probe (atri-hwprobe) ===");
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
	return 0;
}
