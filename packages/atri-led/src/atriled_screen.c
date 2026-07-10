#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <errno.h>
#include <signal.h>

static const char *spi_dev = "/dev/spidev1.0";
static const char *gpio_reset = "/sys/class/gpio/gpio74/value";
static int mode = 0;
static uint8_t bits = 8;
static uint32_t speed = 4000000;

static int spi_fd = -1;

static int spi_open(void)
{
	spi_fd = open(spi_dev, O_RDWR);
	if (spi_fd < 0) { perror(spi_dev); return -1; }
	ioctl(spi_fd, SPI_IOC_WR_MODE, &mode);
	ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
	ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
	return 0;
}

static int gpio_set(const char *path, int val)
{
	char buf[8];
	int fd = open(path, O_WRONLY);
	if (fd < 0) return -1;
	snprintf(buf, sizeof(buf), "%d", val);
	(void)write(fd, buf, strlen(buf));
	close(fd);
	return 0;
}

static int screen_reset(void)
{
	gpio_set(gpio_reset, 0);
	usleep(10000);
	gpio_set(gpio_reset, 1);
	usleep(100000);
	return 0;
}

static int screen_send(const uint8_t *data, int len)
{
	if (spi_fd < 0) return -1;
	struct spi_ioc_transfer tr = {
		.tx_buf = (unsigned long)data,
		.rx_buf = 0,
		.len = (uint32_t)len,
		.speed_hz = speed,
		.bits_per_word = bits,
	};
	return ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);
}

static void print_usage(const char *prog)
{
	printf("Usage: %s <command> [args]\n\n", prog);
	printf("Commands:\n");
	printf("  probe              Check if screen is detected\n");
	printf("  on                 Turn on backlight\n");
	printf("  off                Turn off backlight\n");
	printf("  brightness <0-200> Set brightness\n");
	printf("  send <file>        Send raw SPI data from file\n");
	printf("  rect <r> <g> <b>   Fill screen with color\n");
}

int main(int argc, char *argv[])
{
	if (argc < 2) { print_usage(argv[0]); return 1; }

	const char *cmd = argv[1];

	if (strcmp(cmd, "probe") == 0) {
		if (spi_open() < 0) {
			printf("SPI LED screen: not found (%s)\n", spi_dev);
			return 1;
		}
		printf("SPI LED screen: detected (%s, %d Hz)\n", spi_dev, speed);
		close(spi_fd);
		return 0;

	} else if (strcmp(cmd, "on") == 0) {
		return system("echo 200 > /sys/class/backlight/led_screen/brightness 2>/dev/null || echo 1");

	} else if (strcmp(cmd, "off") == 0) {
		return system("echo 0 > /sys/class/backlight/led_screen/brightness 2>/dev/null || echo 1");

	} else if (strcmp(cmd, "brightness") == 0) {
		if (argc < 3) { fprintf(stderr, "Usage: %s brightness <val>\n", argv[0]); return 1; }
		char cmd[256];
		snprintf(cmd, sizeof(cmd), "echo %s > /sys/class/backlight/led_screen/brightness 2>/dev/null", argv[2]);
		return system(cmd);

	} else if (strcmp(cmd, "send") == 0) {
		if (argc < 3) { fprintf(stderr, "Usage: %s send <file>\n", argv[0]); return 1; }
		FILE *f = fopen(argv[2], "rb");
		if (!f) { perror(argv[2]); return 1; }
		fseek(f, 0, SEEK_END);
		int len = ftell(f);
		fseek(f, 0, SEEK_SET);
		uint8_t *data = malloc(len);
		(void)fread(data, 1, len, f);
		fclose(f);
		if (spi_open() < 0) { free(data); return 1; }
		screen_reset();
		screen_send(data, len);
		close(spi_fd);
		free(data);
		printf("Sent %d bytes to SPI screen\n", len);
		return 0;

	} else if (strcmp(cmd, "test") == 0) {
		if (spi_open() < 0) return 1;
		screen_reset();
		printf("SPI screen test: sending test pattern\n");
		uint8_t pattern[256];
		memset(pattern, 0xFF, sizeof(pattern));
		screen_send(pattern, sizeof(pattern));
		close(spi_fd);
		return 0;

	} else {
		print_usage(argv[0]);
		return 1;
	}
}