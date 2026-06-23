#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <poll.h>
#include <time.h>

#define WIDTH 25
#define HEIGHT 16
#define PIXELS (WIDTH * HEIGHT)

static int spi_fd = -1;
static uint8_t frame[WIDTH * HEIGHT / 8];
static int gpio_reset = -1;

int led_spi_init(const char *dev)
{
	spi_fd = open(dev, O_RDWR);
	if (spi_fd < 0) {
		perror(dev);
		return -1;
	}

	uint32_t mode = SPI_MODE_0;
	uint32_t bits = 8;
	uint32_t speed = 4000000;

	ioctl(spi_fd, SPI_IOC_WR_MODE32, &mode);
	ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
	ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

	memset(frame, 0, sizeof(frame));
	return 0;
}

void led_reset(void)
{
	if (gpio_reset >= 0) {
		char path[64];
		snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio_reset);
		int fd = open(path, O_WRONLY);
		if (fd >= 0) {
			write(fd, "0", 1);
			usleep(10000);
			write(fd, "1", 1);
			close(fd);
			usleep(100000);
		}
	}
}

void led_pixel(int x, int y, int on)
{
	if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
	int addr = y * WIDTH + x;
	int byte = addr / 8;
	int bit = addr % 8;
	if (on)
		frame[byte] |= (1 << bit);
	else
		frame[byte] &= ~(1 << bit);
}

void led_update(void)
{
	uint8_t tx[WIDTH + 1];
	for (int y = 0; y < HEIGHT; y++) {
		tx[0] = y + 1;
		memcpy(tx + 1, frame + y * WIDTH / 8, WIDTH / 8);
		struct spi_ioc_transfer t = {
			.tx_buf = (uintptr_t)tx,
			.len = WIDTH / 8 + 1,
			.speed_hz = 4000000,
		};
		ioctl(spi_fd, SPI_IOC_MESSAGE(1), &t);
	}
}

void led_test_patterns(void)
{
	printf("Testing Quasar 25x16 monochrome LED screen via SPI\n");

	printf("Pattern: all on\n");
	memset(frame, 0xff, sizeof(frame));
	led_update();
	sleep(1);

	printf("Pattern: all off\n");
	memset(frame, 0, sizeof(frame));
	led_update();
	usleep(500000);

	printf("Pattern: horizontal scan\n");
	for (int y = 0; y < HEIGHT; y++) {
		memset(frame, 0, sizeof(frame));
		for (int x = 0; x < WIDTH; x++) led_pixel(x, y, 1);
		led_update();
		usleep(100000);
	}

	printf("Pattern: vertical scan\n");
	for (int x = 0; x < WIDTH; x++) {
		memset(frame, 0, sizeof(frame));
		for (int y = 0; y < HEIGHT; y++) led_pixel(x, y, 1);
		led_update();
		usleep(100000);
	}

	printf("Pattern: checkerboard\n");
	memset(frame, 0, sizeof(frame));
	for (int y = 0; y < HEIGHT; y++) {
		for (int x = 0; x < WIDTH; x++) {
			if ((x + y) % 2) led_pixel(x, y, 1);
		}
	}
	led_update();
	sleep(1);
}

int main(int argc, char *argv[])
{
	const char *dev = "/dev/spidev0.0";
	if (argc > 1) dev = argv[1];

	if (led_spi_init(dev) < 0) {
		fprintf(stderr, "Failed to open SPI device: %s\n", dev);
		return 1;
	}

	led_reset();

	printf("Running test patterns...\n");
	led_test_patterns();

	close(spi_fd);
	return 0;
}