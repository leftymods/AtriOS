/*
 * atri-zigbee — bring-up tool for the AtriStation Zigbee module
 * (Tuya TZ9213-2782 on UART_AO_B, /dev/ttyAML1).
 *
 * The stock radio runs vendor firmware. To use it as a coordinator
 * with zigbee2mqtt/ZHA the module is reflashed over its UART boot
 * loader (boot pin held during reset):
 *   - TI CC2652P family  : Koenkk Z-Stack coordinator, XMODEM-CRC
 *   - Silicon Labs MG21  : EmberZNet NCP (XMODEM via UART bootloader)
 *
 * Modes:
 *   atri-zigbee info [dev]              open + line status register dump
 *   atri-zigbee reset [dev]             normal reset (boot released)
 *   atri-zigbee bootloader [dev]        enter UART boot loader
 *   atri-zigbee listen [dev] [sec]      print any bytes the module sends
 *   atri-zigbee send <file> [dev]       XMODEM-CRC transmit (bootloader)
 *   atri-zigbee raw [dev] [baud]        stdin/stdout passthrough
 *
 * Exit code = number of failures.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

#define DEFAULT_DEV	"/dev/ttyAML1"
#define DEFAULT_BAUD	B115200
#define GPIO_EXPORT_FMT "/sys/class/gpio/gpio%s"

static int failures = 0;
#define FAIL(...) do { fprintf(stderr, "atri-zigbee ERROR: " __VA_ARGS__); \
		       fputc('\n', stderr); failures++; } while (0)
#define LOG(...)  do { printf("atri-zigbee: " __VA_ARGS__); putchar('\n'); } while (0)

/* ---- gpio helpers through sysfs (chip numbers differ per build) ---- */

static int gpio_write(const char *num, const char *val)
{
	char path[128];
	int fd;

	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%s/value", num);
	fd = open(path, O_WRONLY);
	if (fd < 0) return -1;
	int r = write(fd, val, strlen(val));
	close(fd);
	return r < 0 ? -1 : 0;
}

static int gpio_export(const char *num)
{
	char path[64];
	int fd, r;
	struct stat st;

	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%s", num);
	if (stat(path, &st) == 0) return 0;
	fd = open("/sys/class/gpio/export", O_WRONLY);
	if (fd < 0) return -1;
	r = dprintf(fd, "%s", num);
	close(fd);
	usleep(100000);
	return r < 0 ? -1 : 0;
}

/* resolve logical names to global gpio numbers from gpioinfo-style
 * base offset: zigbee pins live on the periphs chip; find it by name */

/*
 * The DTS exposes the module control lines as plain GPIOs; their global
 * numbers depend on pinctrl enumeration, so we read them from the
 * device tree instead: /proc/device-tree/zigbee-control gpios props
 */



#include <dirent.h>
#include <sys/stat.h>

static int gpiochip_base_by_label(const char *label_want)
{
	DIR *d = opendir("/sys/class/gpio");
	struct dirent *de;

	if (!d) return -1;
	while ((de = readdir(d))) {
		char plabel[300], lab[96] = "";
		int fd, base = -1, k;

		if (strncmp(de->d_name, "gpiochip", 8)) continue;
		snprintf(plabel, sizeof(plabel), "/sys/class/gpio/%s/label",
			 de->d_name);
		fd = open(plabel, O_RDONLY);
		if (fd >= 0) {
			k = read(fd, lab, sizeof(lab) - 1);
			close(fd);
			if (k > 0 && lab[k-1] == '\n') lab[k-1] = '\0';
		}
		snprintf(plabel, sizeof(plabel), "/sys/class/gpio/%s/base",
			 de->d_name);
		fd = open(plabel, O_RDONLY);
		if (fd >= 0) {
			char b[32] = "";
			k = read(fd, b, sizeof(b) - 1);
			close(fd);
			if (k > 0) base = atoi(b);
		}
		if (base >= 0 && strcmp(lab, label_want) == 0) {
			closedir(d);
			return base;
		}
	}
	closedir(d);
	return -1;
}

/* reset=GPIOX_17, boot=GPIOX_11 → both on "meson-g12a" periphs chip */
static void zb_pin(int *reset, int *boot)
{
	int base = gpiochip_base_by_label("meson-g12a");
	if (base < 0) base = 0;
	*reset = base + 82;	/* 0x52 */
	*boot  = base + 76;	/* 0x4c */
}

static int zb_export_pins(int reset, int boot)
{
	char n[16];
	snprintf(n, sizeof(n), "%d", reset);
	if (gpio_export(n)) LOG("warn: gpio export %s failed", n);
	snprintf(n, sizeof(n), "%d", boot);
	if (gpio_export(n)) LOG("warn: gpio export %s failed", n);
	return 0;
}

/* ---- serial ---- */

static speed_t baud_const(int b)
{
	switch (b) {
	case 9600: return B9600;
	case 19200: return B19200;
	case 38400: return B38400;
	case 57600: return B57600;
	case 230400: return B230400;
	case 460800: return B460800;
	case 921600: return B921600;
	case 1000000: return B1000000;
	default: return B115200;
	}
}

static int tty_open(const char *dev, int baud)
{
	int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
	struct termios t;

	if (fd < 0) {
		FAIL("open %s: %s", dev, strerror(errno));
		return -1;
	}
	tcgetattr(fd, &t);
	cfmakeraw(&t);
	cfsetspeed(&t, baud_const(baud));
	t.c_cflag |= CLOCAL | CREAD;
	t.c_cc[VMIN] = 0;
	t.c_cc[VTIME] = 0;
	tcsetattr(fd, TCSANOW, &t);
	tcflush(fd, TCIOFLUSH);
	return fd;
}

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int drain(int fd, char *buf, size_t len, int ms)
{
	long deadline = now_ms() + ms;
	size_t got = 0;

	while (now_ms() < deadline) {
		fd_set rf;
		struct timeval tv = { .tv_sec = 0, .tv_usec = 20000 };
		FD_ZERO(&rf);
		FD_SET(fd, &rf);
		if (select(fd + 1, &rf, NULL, NULL, &tv) <= 0) continue;
		ssize_t n = read(fd, buf + got, len - got);
		if (n > 0) {
			got += n;
			if (got >= len) break;
		}
	}
	return (int)got;
}

/* ---- XMODEM-CRC sender ---- */

#define X_SOH 0x01
#define X_STX 0x02
#define X_EOT 0x04
#define X_ACK 0x06
#define X_NAK 0x15
#define X_CAN 0x18

static uint16_t crc16_xmodem(const uint8_t *d, size_t n)
{
	uint16_t crc = 0;
	for (size_t i = 0; i < n; i++) {
		crc ^= (uint16_t)d[i] << 8;
		for (int b = 0; b < 8; b++)
			crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021
					     : (crc << 1);
	}
	return crc;
}

static int xmodem_send(int fd, const char *path)
{
	FILE *f = fopen(path, "rb");
	static uint8_t blk[1024];
	int blkno = 1, retries;
	long t0 = now_ms();

	if (!f) { FAIL("open %s: %s", path, strerror(errno)); return 1; }

	/* wait for bootloader 'C' (CRC mode) up to 10 s */
	char c = 0;
	long deadline = now_ms() + 10000;
	LOG("waiting for 'C' (CRC mode)...");
	while (now_ms() < deadline) {
		char tmp[64];
		int n = drain(fd, tmp, sizeof(tmp), 200);
		for (int i = 0; i < n; i++)
			if (tmp[i] == 'C') { c = 'C'; break; }
		if (c) break;
	}
	if (!c) {
		FAIL("bootloader did not offer CRC mode (no 'C')");
		fclose(f);
		return 1;
	}

	size_t n;
	while ((n = fread(blk, 1, sizeof(blk), f)) > 0 || !feof(f)) {
		if (n == 0) break;
		memset(blk + n, 0xFF, sizeof(blk) - n); /* pad */

		retries = 0;
 resend:
		if (++retries > 10) {
			FAIL("too many NAK on block %d", blkno);
			fclose(f);
			return 1;
		}
		uint8_t hdr[3] = { X_STX, (uint8_t)blkno,
				   (uint8_t)(255 - blkno) };
		uint16_t crc = crc16_xmodem(blk, sizeof(blk));
		uint8_t tail[2] = { crc >> 8, crc & 0xff };
		if (write(fd, hdr, 3) < 0) { FAIL("tty write"); return 1; }
		if (write(fd, blk, sizeof(blk)) < 0) { FAIL("tty write"); return 1; }
		if (write(fd, tail, 2) < 0) { FAIL("tty write"); return 1; }

		char resp = 0;
		char tmp[64];
		int got = drain(fd, tmp, sizeof(tmp), 1500);
		for (int i = 0; i < got; i++) {
			if (tmp[i] == X_ACK) { resp = X_ACK; break; }
			if (tmp[i] == X_NAK || tmp[i] == X_CAN) {
				resp = tmp[i]; break;
			}
		}
		if (resp != X_ACK) {
			LOG("block %d: %s, retry", blkno,
			    resp == X_CAN ? "CAN" : "NAK");
			goto resend;
		}
		if (blkno % 32 == 0)
			LOG("sent %d KB...", blkno * 1024 / 1024);
		blkno++;
		if (n < sizeof(blk)) break; /* short final block sent padded */
	}

	uint8_t eot = X_EOT;
	if (write(fd, &eot, 1) < 0) { FAIL("tty write"); return 1; }
	char tmp[64];
	drain(fd, tmp, sizeof(tmp), 2000);
	if (write(fd, &eot, 1) < 0) { FAIL("tty write"); return 1; }
	drain(fd, tmp, sizeof(tmp), 2000);

	fclose(f);
	LOG("transfer done: %d blocks in %ld ms", blkno - 1, now_ms() - t0);
	return 0;
}

/* ---- modes ---- */

static void usage(void)
{
	printf("usage:\n"
	       "  atri-zigbee info [dev]\n"
	       "  atri-zigbee reset [dev] [baud]\n"
	       "  atri-zigbee bootloader [dev]\n"
	       "  atri-zigbee listen [dev] [seconds]\n"
	       "  atri-zigbee send <file.xmodem> [dev]\n"
	       "  atri-zigbee raw [dev] [baud]\n");
}

int main(int argc, char **argv)
{
	const char *cmd = argc > 1 ? argv[1] : NULL;
	const char *dev = DEFAULT_DEV;
	int baud = 115200;

	if (!cmd) { usage(); return 0; }

	int reset, boot;
	zb_pin(&reset, &boot);
	zb_export_pins(reset, boot);
	LOG("zigbee gpio: reset=%d boot=%d (global nums)", reset, boot);

	if (strcmp(cmd, "info") == 0) {
		int fd = tty_open(dev, DEFAULT_BAUD);
		if (fd < 0) return 1;
		char buf[256];
		int n = drain(fd, buf, sizeof(buf), 500);
		LOG("%s opened; %d bytes of ambient traffic", dev, n);
		if (n > 0) {
			fputs("  hex:", stdout);
			for (int i = 0; i < n; i++) printf(" %02x", buf[i]);
			putchar('\n');
		}
		close(fd);
		return failures ? 1 : 0;
	}

	if (strcmp(cmd, "reset") == 0) {
		char rn[16], bn[16];
		snprintf(rn, sizeof(rn), "%d", reset);
		snprintf(bn, sizeof(bn), "%d", boot);
		gpio_write(bn, "0");		/* boot released */
		gpio_write(rn, "0"); usleep(50000);
		gpio_write(rn, "1"); usleep(120000);
		LOG("normal reset done (%s)", dev);
		return failures ? 1 : 0;
	}

	if (strcmp(cmd, "bootloader") == 0) {
		char rn[16], bn[16];
		snprintf(rn, sizeof(rn), "%d", reset);
		snprintf(bn, sizeof(bn), "%d", boot);
		gpio_write(bn, "1");		/* boot asserted */
		usleep(10000);
		gpio_write(rn, "0"); usleep(50000);
		gpio_write(rn, "1"); usleep(120000);
		gpio_write(bn, "0");
		LOG("entered bootloader sequence on %s", dev);
		return failures ? 1 : 0;
	}

	if (strcmp(cmd, "listen") == 0) {
		int sec = argc > 3 ? atoi(argv[3]) : 10;
		int fd = tty_open(dev, DEFAULT_BAUD);
		if (fd < 0) return 1;
		LOG("listening %s for %ds @115200 ...", dev, sec);
		long end = now_ms() + sec * 1000L;
		char buf[512];
		while (now_ms() < end) {
			int n = drain(fd, buf, sizeof(buf), 250);
			for (int i = 0; i < n; i++)
				putchar((buf[i] >= 32 && buf[i] < 127)
					? buf[i] : '.');
			fflush(stdout);
		}
		putchar('\n');
		close(fd);
		return failures ? 1 : 0;
	}

	if (strcmp(cmd, "send") == 0) {
		if (argc < 3) { usage(); return 1; }
		if (argc > 3) dev = argv[3];
		int fd = tty_open(dev, DEFAULT_BAUD);
		if (fd < 0) return 1;
		int rc = xmodem_send(fd, argv[2]);
		close(fd);
		return rc;
	}

	if (strcmp(cmd, "raw") == 0) {
		if (argc > 2) dev = argv[2];
		if (argc > 3) baud = atoi(argv[3]);
		int fd = tty_open(dev, baud);
		if (fd < 0) return 1;
		LOG("raw passthrough %s @%d (ctrl-C to exit)", dev, baud);
		while (1) {
			fd_set rf;
			FD_ZERO(&rf);
			FD_SET(0, &rf);
			FD_SET(fd, &rf);
			select(fd + 1, &rf, NULL, NULL, NULL);
			char b[512];
			if (FD_ISSET(0, &rf)) {
				ssize_t n = read(0, b, sizeof(b));
				if (n <= 0) break;
				if (write(fd, b, n) < 0) break;
			}
			if (FD_ISSET(fd, &rf)) {
				ssize_t n = read(fd, b, sizeof(b));
				if (n <= 0) break;
				fwrite(b, 1, n, stdout);
				fflush(stdout);
			}
		}
		close(fd);
		return 0;
	}

	usage();
	return 1;
}
