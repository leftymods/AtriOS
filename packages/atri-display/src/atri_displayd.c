/*
 * atri_displayd.c - Screen display daemon for AtriStation / Yandex Station Max
 *
 * Drives the 25x16 LED dot matrix display (Gowin FPGA fbdev / atri_led_panel)
 * Features:
 *   - Digital clock with 4x10 high-contrast font and blinking colon
 *   - Status indicators (Wi-Fi, Bluetooth, Zigbee, Mute)
 *   - Volume popup overlay with progress bar
 *   - Scrolling text ticker for messages and IP address
 *   - Temperature and weather display
 *   - Automatic backlight dimming based on ambient light sensor (LTR-308ALS)
 *   - Unix domain socket IPC (/run/atri-display.sock)
 *
 * Copyright (c) 2026 leftymods / AtriOS Project
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/fb.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>

#define SCREEN_W 25
#define SCREEN_H 16
#define SCREEN_PIXELS (SCREEN_W * SCREEN_H)
#define SOCK_PATH "/run/atri-display.sock"

/* Display modes */
typedef enum {
	MODE_CLOCK = 0,
	MODE_MESSAGE,
	MODE_VOLUME,
	MODE_TEMP,
	MODE_EYES
} display_mode_t;

/* State */
static volatile bool running = true;
static int fb_fd = -1;
static uint8_t *fb_mem = NULL;
static size_t fb_size = 0;
static int sock_fd = -1;

static display_mode_t cur_mode = MODE_CLOCK;
static display_mode_t prev_mode = MODE_CLOCK;
static time_t mode_timeout = 0;

static char msg_buffer[256] = {0};
static int msg_scroll_pos = 0;
static char temp_buffer[16] = "+22";

static int volume_level = 50;
static bool auto_brightness = true;
static int manual_brightness = 150;
static char backlight_path[256] = {0};

/* 4x10 Digital Font for Digits 0-9 and Colon */
static const uint16_t digits_4x10[10][10] = {
	/* 0 */ { 0x0E, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x0E },
	/* 1 */ { 0x02, 0x06, 0x0A, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x07 },
	/* 2 */ { 0x0E, 0x09, 0x01, 0x01, 0x02, 0x04, 0x08, 0x08, 0x09, 0x0F },
	/* 3 */ { 0x0E, 0x09, 0x01, 0x01, 0x06, 0x01, 0x01, 0x01, 0x09, 0x0E },
	/* 4 */ { 0x09, 0x09, 0x09, 0x09, 0x0F, 0x01, 0x01, 0x01, 0x01, 0x01 },
	/* 5 */ { 0x0F, 0x08, 0x08, 0x08, 0x0E, 0x01, 0x01, 0x01, 0x09, 0x0E },
	/* 6 */ { 0x06, 0x08, 0x08, 0x08, 0x0E, 0x09, 0x09, 0x09, 0x09, 0x0E },
	/* 7 */ { 0x0F, 0x01, 0x01, 0x02, 0x02, 0x04, 0x04, 0x08, 0x08, 0x08 },
	/* 8 */ { 0x0E, 0x09, 0x09, 0x09, 0x06, 0x09, 0x09, 0x09, 0x09, 0x0E },
	/* 9 */ { 0x0E, 0x09, 0x09, 0x09, 0x07, 0x01, 0x01, 0x01, 0x01, 0x06 }
};

/* 3x5 ASCII Font for Compact Text and Scrolling */
static const uint8_t font3x5[96][3] = {
	{0x00,0x00,0x00}, /* ' ' */
	{0x00,0x17,0x00}, /* '!' */
	{0x03,0x00,0x03}, /* '"' */
	{0x1F,0x0A,0x1F}, /* '#' */
	{0x12,0x1F,0x09}, /* '$' */
	{0x19,0x04,0x13}, /* '%' */
	{0x0A,0x15,0x02}, /* '&' */
	{0x00,0x03,0x00}, /* ''' */
	{0x00,0x0E,0x11}, /* '(' */
	{0x11,0x0E,0x00}, /* ')' */
	{0x0A,0x04,0x0A}, /* '*' */
	{0x04,0x0E,0x04}, /* '+' */
	{0x10,0x08,0x00}, /* ',' */
	{0x04,0x04,0x04}, /* '-' */
	{0x00,0x10,0x00}, /* '.' */
	{0x18,0x04,0x03}, /* '/' */
	{0x1F,0x11,0x1F}, /* '0' */
	{0x00,0x1F,0x00}, /* '1' */
	{0x1D,0x15,0x17}, /* '2' */
	{0x15,0x15,0x1F}, /* '3' */
	{0x07,0x04,0x1F}, /* '4' */
	{0x17,0x15,0x1D}, /* '5' */
	{0x1F,0x15,0x1D}, /* '6' */
	{0x01,0x01,0x1F}, /* '7' */
	{0x1F,0x15,0x1F}, /* '8' */
	{0x17,0x15,0x1F}, /* '9' */
	{0x00,0x0A,0x00}, /* ':' */
	{0x10,0x0A,0x00}, /* ';' */
	{0x04,0x0A,0x11}, /* '<' */
	{0x0A,0x0A,0x0A}, /* '=' */
	{0x11,0x0A,0x04}, /* '>' */
	{0x01,0x15,0x02}, /* '?' */
	{0x0E,0x11,0x1E}, /* '@' */
	{0x1E,0x05,0x1E}, /* 'A' */
	{0x1F,0x15,0x0A}, /* 'B' */
	{0x0E,0x11,0x11}, /* 'C' */
	{0x1F,0x11,0x0E}, /* 'D' */
	{0x1F,0x15,0x11}, /* 'E' */
	{0x1F,0x05,0x01}, /* 'F' */
	{0x0E,0x11,0x1D}, /* 'G' */
	{0x1F,0x04,0x1F}, /* 'H' */
	{0x11,0x1F,0x11}, /* 'I' */
	{0x08,0x10,0x0F}, /* 'J' */
	{0x1F,0x04,0x1B}, /* 'K' */
	{0x1F,0x10,0x10}, /* 'L' */
	{0x1F,0x02,0x1F}, /* 'M' */
	{0x1F,0x02,0x1F}, /* 'N' */
	{0x0E,0x11,0x0E}, /* 'O' */
	{0x1F,0x05,0x02}, /* 'P' */
	{0x0E,0x11,0x1E}, /* 'Q' */
	{0x1F,0x05,0x1A}, /* 'R' */
	{0x12,0x15,0x09}, /* 'S' */
	{0x01,0x1F,0x01}, /* 'T' */
	{0x0F,0x10,0x0F}, /* 'U' */
	{0x07,0x18,0x07}, /* 'V' */
	{0x1F,0x08,0x1F}, /* 'W' */
	{0x1B,0x04,0x1B}, /* 'X' */
	{0x03,0x1C,0x03}, /* 'Y' */
	{0x19,0x15,0x13}, /* 'Z' */
};

static void sig_handler(int sig)
{
	(void)sig;
	running = false;
}

static void clear_screen(void)
{
	if (fb_mem)
		memset(fb_mem, 0, SCREEN_PIXELS);
}

static void set_pixel(int x, int y, uint8_t val)
{
	if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H && fb_mem) {
		fb_mem[y * SCREEN_W + x] = val;
	}
}

static void flush_screen(void)
{
	if (fb_fd >= 0) {
		fsync(fb_fd);
	}
}

static void init_backlight(void)
{
	const char *candidates[] = {
		"/sys/class/backlight/gowin-backlight/brightness",
		"/sys/class/backlight/atri_led_panel/brightness",
		"/sys/class/backlight/gowin_led/brightness",
		"/sys/class/backlight/led_screen/brightness",
		NULL
	};
	for (int i = 0; candidates[i]; i++) {
		if (access(candidates[i], W_OK) == 0) {
			strncpy(backlight_path, candidates[i], sizeof(backlight_path) - 1);
			return;
		}
	}
}

static void set_backlight_level(int val)
{
	if (val < 0) val = 0;
	if (val > 200) val = 200;
	if (backlight_path[0]) {
		FILE *f = fopen(backlight_path, "w");
		if (f) {
			fprintf(f, "%d\n", val);
			fclose(f);
		}
	}
}

static int read_ambient_light(void)
{
	const char *paths[] = {
		"/sys/bus/iio/devices/iio:device0/in_illuminance_raw",
		"/sys/bus/iio/devices/iio:device0/in_illuminance_input",
		NULL
	};
	char buf[32];
	for (int i = 0; paths[i]; i++) {
		int fd = open(paths[i], O_RDONLY);
		if (fd >= 0) {
			ssize_t n = read(fd, buf, sizeof(buf) - 1);
			close(fd);
			if (n > 0) {
				buf[n] = '\0';
				return atoi(buf);
			}
		}
	}
	return -1;
}

static void update_auto_brightness(void)
{
	if (!auto_brightness) return;
	int lux = read_ambient_light();
	if (lux < 0) return;

	/* Map lux 0..1000 to brightness 15..200 */
	int br;
	if (lux < 10) br = 15;
	else if (lux < 50) br = 35;
	else if (lux < 150) br = 70;
	else if (lux < 400) br = 120;
	else br = 180;

	set_backlight_level(br);
}

/* Check network and hardware states */
static bool is_wifi_connected(void)
{
	int fd = open("/sys/class/net/wlan0/operstate", O_RDONLY);
	if (fd < 0) return false;
	char buf[16] = {0};
	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	return (n > 0 && strstr(buf, "up") != NULL);
}

static bool is_bt_active(void)
{
	return (access("/sys/class/bluetooth/hci0", F_OK) == 0);
}

static bool is_zigbee_active(void)
{
	return (access("/dev/ttyZigbee", F_OK) == 0);
}

/* Render 4x10 digit */
static void draw_digit_4x10(int x, int y, int d)
{
	if (d < 0 || d > 9) return;
	for (int row = 0; row < 10; row++) {
		uint16_t row_bits = digits_4x10[d][row];
		for (int col = 0; col < 4; col++) {
			if (row_bits & (1 << (3 - col))) {
				set_pixel(x + col, y + row, 0xFF);
			}
		}
	}
}

/* Render 3x5 Character */
static int draw_char_3x5(int x, int y, char c)
{
	if (c < 32 || c > 127) c = '?';
	int idx = c - 32;
	for (int col = 0; col < 3; col++) {
		uint8_t col_bits = font3x5[idx][col];
		for (int row = 0; row < 5; row++) {
			if (col_bits & (1 << (4 - row))) {
				set_pixel(x + col, y + row, 0xFF);
			}
		}
	}
	return 4; /* width + spacing */
}

static void draw_string_3x5(int x, int y, const char *str)
{
	while (*str) {
		x += draw_char_3x5(x, y, *str);
		str++;
	}
}

/* Clock renderer */
static void render_clock(bool blink_colon)
{
	time_t now = time(NULL);
	struct tm tm_buf;
	localtime_r(&now, &tm_buf);

	int h = tm_buf.tm_hour;
	int m = tm_buf.tm_min;

	clear_screen();

	/* 4x10 font layout:
	 * Digit 0: x = 1..4
	 * Digit 1: x = 6..9
	 * Colon:   x = 11..12
	 * Digit 2: x = 14..17
	 * Digit 3: x = 19..22
	 * Total width = 22, screen width = 25 (offset = 1)
	 * Vertical: y = 3..12 (centered within 16)
	 */
	int base_y = 3;
	draw_digit_4x10(1, base_y, h / 10);
	draw_digit_4x10(6, base_y, h % 10);

	if (blink_colon) {
		set_pixel(11, base_y + 3, 0xFF);
		set_pixel(11, base_y + 4, 0xFF);
		set_pixel(11, base_y + 6, 0xFF);
		set_pixel(11, base_y + 7, 0xFF);
	}

	draw_digit_4x10(14, base_y, m / 10);
	draw_digit_4x10(19, base_y, m % 10);

	/* Status dots on bottom row (y = 15):
	 * x = 1: Wi-Fi status
	 * x = 5: Bluetooth status
	 * x = 9: Zigbee status
	 */
	if (is_wifi_connected()) set_pixel(1, 15, 0x80);
	if (is_bt_active()) set_pixel(5, 15, 0x80);
	if (is_zigbee_active()) set_pixel(9, 15, 0x80);

	flush_screen();
}

/* Volume popup renderer */
static void render_volume(int vol)
{
	clear_screen();

	char buf[8];
	snprintf(buf, sizeof(buf), "%d%%", vol);
	int text_x = (vol >= 100) ? 5 : (vol >= 10) ? 7 : 9;
	draw_string_3x5(text_x, 3, buf);

	/* Bottom progress bar (y = 11..13):
	 * 23 pixels wide (x = 1..23)
	 */
	int bar_width = (vol * 23) / 100;
	if (bar_width > 23) bar_width = 23;
	for (int x = 1; x <= bar_width; x++) {
		set_pixel(x, 11, 0xFF);
		set_pixel(x, 12, 0xFF);
	}
	/* Frame dots at ends */
	set_pixel(0, 11, 0x40);
	set_pixel(0, 12, 0x40);
	set_pixel(24, 11, 0x40);
	set_pixel(24, 12, 0x40);

	flush_screen();
}

/* Temperature renderer */
static void render_temp(const char *temp)
{
	clear_screen();
	draw_string_3x5(3, 5, temp);
	/* Degree symbol */
	int len = strlen(temp);
	int deg_x = 3 + len * 4;
	set_pixel(deg_x, 5, 0xFF);
	set_pixel(deg_x + 1, 5, 0xFF);
	set_pixel(deg_x, 6, 0xFF);
	set_pixel(deg_x + 1, 6, 0xFF);
	/* 'C' */
	draw_char_3x5(deg_x + 3, 5, 'C');
	flush_screen();
}

/* Scrolling ticker renderer */
static void render_scrolling_msg(void)
{
	clear_screen();
	int text_len = strlen(msg_buffer);
	int total_pixel_width = text_len * 4;

	for (int i = 0; i < text_len; i++) {
		int char_x = msg_scroll_pos + (i * 4);
		if (char_x >= -4 && char_x < SCREEN_W) {
			draw_char_3x5(char_x, 5, msg_buffer[i]);
		}
	}

	msg_scroll_pos--;
	if (msg_scroll_pos < -total_pixel_width) {
		msg_scroll_pos = SCREEN_W;
		/* One pass finished, switch back to clock */
		cur_mode = MODE_CLOCK;
	}
	flush_screen();
}

/* Eyes animation */
static void render_eyes(int frame)
{
	clear_screen();
	/* Two 5x6 rectangular pixel eyes */
	int left_x = 4, right_x = 15, eye_y = 5;

	if (frame == 1 || frame == 3) {
		/* Half closed / blink */
		for (int x = 0; x < 6; x++) {
			set_pixel(left_x + x, eye_y + 2, 0xFF);
			set_pixel(right_x + x, eye_y + 2, 0xFF);
		}
	} else if (frame == 2) {
		/* Fully closed */
		for (int x = 0; x < 6; x++) {
			set_pixel(left_x + x, eye_y + 3, 0xFF);
			set_pixel(right_x + x, eye_y + 3, 0xFF);
		}
	} else {
		/* Wide open */
		for (int y = 0; y < 6; y++) {
			for (int x = 0; x < 6; x++) {
				set_pixel(left_x + x, eye_y + y, 0xFF);
				set_pixel(right_x + x, eye_y + y, 0xFF);
			}
		}
	}
	flush_screen();
}

static void get_primary_ip(char *out, size_t maxlen)
{
	struct ifaddrs *ifaddr, *ifa;
	out[0] = '\0';

	if (getifaddrs(&ifaddr) == -1) return;

	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET)
			continue;
		if (strcmp(ifa->ifa_name, "lo") == 0)
			continue;
		struct sockaddr_in *p = (struct sockaddr_in *)ifa->ifa_addr;
		inet_ntop(AF_INET, &p->sin_addr, out, maxlen);
		if (strncmp(ifa->ifa_name, "wlan", 4) == 0)
			break; /* Prefer wlan */
	}
	freeifaddrs(ifaddr);
}

/* Handle command received over UNIX socket */
static void handle_command(int client_fd)
{
	char buf[256];
	ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
	if (n <= 0) return;
	buf[n] = '\0';

	/* Strip trailing \r\n */
	while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n')) {
		buf[--n] = '\0';
	}

	char resp[128] = "OK\n";

	if (strcasecmp(buf, "CLOCK") == 0) {
		cur_mode = MODE_CLOCK;
	} else if (strncasecmp(buf, "MSG ", 4) == 0) {
		snprintf(msg_buffer, sizeof(msg_buffer), "%s", buf + 4);
		msg_scroll_pos = SCREEN_W;
		cur_mode = MODE_MESSAGE;
	} else if (strncasecmp(buf, "TEMP ", 5) == 0) {
		snprintf(temp_buffer, sizeof(temp_buffer), "%s", buf + 5);
		cur_mode = MODE_TEMP;
		mode_timeout = time(NULL) + 5;
	} else if (strncasecmp(buf, "VOL ", 4) == 0) {
		volume_level = atoi(buf + 4);
		prev_mode = cur_mode;
		cur_mode = MODE_VOLUME;
		mode_timeout = time(NULL) + 3;
	} else if (strcasecmp(buf, "IP") == 0) {
		char ip[64] = {0};
		get_primary_ip(ip, sizeof(ip));
		if (ip[0]) {
			snprintf(msg_buffer, sizeof(msg_buffer), "IP %s", ip);
		} else {
			snprintf(msg_buffer, sizeof(msg_buffer), "NO IP");
		}
		msg_scroll_pos = SCREEN_W;
		cur_mode = MODE_MESSAGE;
	} else if (strcasecmp(buf, "EYES") == 0) {
		cur_mode = MODE_EYES;
		mode_timeout = time(NULL) + 4;
	} else if (strcasecmp(buf, "CLEAR") == 0) {
		clear_screen();
		flush_screen();
	} else if (strncasecmp(buf, "BRIGHTNESS ", 11) == 0) {
		const char *arg = buf + 11;
		if (strcasecmp(arg, "auto") == 0) {
			auto_brightness = true;
			update_auto_brightness();
		} else {
			auto_brightness = false;
			manual_brightness = atoi(arg);
			set_backlight_level(manual_brightness);
		}
	} else if (strcasecmp(buf, "STATUS") == 0) {
		snprintf(resp, sizeof(resp), "MODE=%d VOL=%d AUTO_BRIGHT=%d\n",
			 cur_mode, volume_level, auto_brightness);
	} else {
		snprintf(resp, sizeof(resp), "ERR unknown command\n");
	}

	ssize_t rw = write(client_fd, resp, strlen(resp));
	(void)rw;
}

static int open_fb(void)
{
	const char *fb_devs[] = { "/dev/fb0", "/dev/fb1", NULL };
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;

	for (int i = 0; fb_devs[i]; i++) {
		int fd = open(fb_devs[i], O_RDWR);
		if (fd < 0) continue;

		if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) == 0 &&
		    ioctl(fd, FBIOGET_VSCREENINFO, &var) == 0) {
			/* Check if it's Gowin LED or matches 25x16 */
			if (strstr(fix.id, "gowin") != NULL ||
			    strstr(fix.id, "atri") != NULL ||
			    strstr(fix.id, "led") != NULL ||
			    (var.xres == SCREEN_W && var.yres == SCREEN_H)) {
				fb_size = SCREEN_PIXELS;
				fb_mem = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
				if (fb_mem != MAP_FAILED) {
					fb_fd = fd;
					printf("atri-displayd: bound to %s (%s, %dx%d)\n",
					       fb_devs[i], fix.id, var.xres, var.yres);
					return 0;
				}
			}
		}
		close(fd);
	}

	/* Fallback: allocate virtual memory for testing if no hardware screen */
	fprintf(stderr, "atri-displayd: warning, no hardware fb matched, running in virtual mode\n");
	fb_size = SCREEN_PIXELS;
	fb_mem = calloc(1, fb_size);
	return 0;
}

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	signal(SIGTERM, sig_handler);
	signal(SIGINT, sig_handler);
	signal(SIGPIPE, SIG_IGN);

	printf("atri-displayd: starting AtriOS 25x16 screen display daemon\n");

	if (open_fb() < 0) {
		fprintf(stderr, "atri-displayd: failed to initialize framebuffer\n");
		return 1;
	}

	init_backlight();
	update_auto_brightness();

	/* Setup Unix socket */
	unlink(SOCK_PATH);
	sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock_fd >= 0) {
		struct sockaddr_un addr;
		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);
		bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr));
		listen(sock_fd, 5);
		chmod(SOCK_PATH, 0666);
	}

	int eye_frame = 0;
	bool colon_state = true;
	time_t last_sec = 0;
	time_t last_bright_check = 0;

	while (running) {
		time_t now = time(NULL);

		/* Periodic light sensor check every 5 seconds */
		if (now - last_bright_check >= 5) {
			update_auto_brightness();
			last_bright_check = now;
		}

		/* Mode timeout fallback */
		if (mode_timeout > 0 && now >= mode_timeout) {
			mode_timeout = 0;
			cur_mode = prev_mode;
		}

		/* Poll socket with 100ms timeout for smooth scrolling/animations */
		struct pollfd pfd;
		pfd.fd = sock_fd;
		pfd.events = POLLIN;
		int pr = poll(&pfd, 1, 100);

		if (pr > 0 && (pfd.revents & POLLIN)) {
			int client = accept(sock_fd, NULL, NULL);
			if (client >= 0) {
				handle_command(client);
				close(client);
			}
		}

		/* Rendering cycle */
		switch (cur_mode) {
		case MODE_CLOCK:
			if (now != last_sec) {
				colon_state = !colon_state;
				last_sec = now;
			}
			render_clock(colon_state);
			break;

		case MODE_VOLUME:
			render_volume(volume_level);
			break;

		case MODE_TEMP:
			render_temp(temp_buffer);
			break;

		case MODE_MESSAGE:
			render_scrolling_msg();
			break;

		case MODE_EYES:
			render_eyes(eye_frame % 4);
			eye_frame++;
			usleep(150000);
			break;
		}
	}

	printf("atri-displayd: shutting down\n");
	clear_screen();
	flush_screen();

	if (sock_fd >= 0) {
		close(sock_fd);
		unlink(SOCK_PATH);
	}
	if (fb_fd >= 0) {
		munmap(fb_mem, fb_size);
		close(fb_fd);
	}

	return 0;
}
