/*
 * atri_matrix.c - Unified 25x16 LED matrix control, test and demo tool
 *
 * Replaces and consolidates:
 *   - quasar_led_ctl
 *   - quasar_led_demo
 *   - quasar_led_info
 *   - quasar_led_test
 *   - quasar_led_text
 *   - atriled_screen
 *   - atri-screen-test
 *
 * Copyright (c) 2026 leftymods / AtriOS Project
 */

#include "quasar_screen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

static volatile int running = 1;
static void sigint_handler(int sig) { (void)sig; running = 0; }

static int set_backlight(int val)
{
	const char *paths[] = {
		"/sys/class/backlight/gowin-backlight/brightness",
		"/sys/class/backlight/atri_led_panel/brightness",
		"/sys/class/backlight/gowin_led/brightness",
		"/sys/class/backlight/led_screen/brightness",
		NULL
	};
	if (val < 0) val = 0;
	if (val > 200) val = 200;

	for (int i = 0; paths[i]; i++) {
		int fd = open(paths[i], O_WRONLY);
		if (fd >= 0) {
			char buf[16];
			int n = snprintf(buf, sizeof(buf), "%d\n", val);
			write(fd, buf, n);
			close(fd);
			return 0;
		}
	}
	return -1;
}

static void print_info(void)
{
	const char *fb_paths[] = { "/dev/fb0", "/dev/fb1", NULL };
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	int found = 0;

	printf("=== AtriOS 25x16 LED Matrix Info ===\n");
	for (int i = 0; fb_paths[i]; i++) {
		int fd = open(fb_paths[i], O_RDWR);
		if (fd >= 0) {
			if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) == 0 &&
			    ioctl(fd, FBIOGET_VSCREENINFO, &var) == 0) {
				printf("Framebuffer : %s\n", fb_paths[i]);
				printf("Driver ID   : %s\n", fix.id);
				printf("Resolution  : %dx%d (%d bpp)\n", var.xres, var.yres, var.bits_per_pixel);
				printf("Buffer Size : %u bytes (line length: %u)\n", fix.smem_len, fix.line_length);
				found = 1;
			}
			close(fd);
		}
	}
	if (!found) {
		printf("Warning: no matrix framebuffer detected (/dev/fb0, /dev/fb1)\n");
	}
}

/* --- Tests & Patterns --- */
static void run_tests(quasar_screen_t *scr)
{
	printf("Running matrix diagnostic tests...\n");

	/* All ON */
	printf("  [1/8] All pixels ON (1 sec)\n");
	screen_rect(scr, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 1);
	screen_flush(scr);
	sleep(1);

	/* All OFF */
	printf("  [2/8] All pixels OFF (0.5 sec)\n");
	screen_clear(scr);
	screen_flush(scr);
	usleep(500000);

	/* Borders */
	printf("  [3/8] Outer borders\n");
	screen_clear(scr);
	screen_rect(scr, 0, 0, SCREEN_WIDTH, 1, 1);
	screen_rect(scr, 0, SCREEN_HEIGHT - 1, SCREEN_WIDTH, 1, 1);
	screen_rect(scr, 0, 0, 1, SCREEN_HEIGHT, 1);
	screen_rect(scr, SCREEN_WIDTH - 1, 0, 1, SCREEN_HEIGHT, 1);
	screen_flush(scr);
	sleep(1);

	/* Horizontal Scan */
	printf("  [4/8] Horizontal line scan\n");
	for (int y = 0; running && y < SCREEN_HEIGHT; y++) {
		screen_clear(scr);
		screen_rect(scr, 0, y, SCREEN_WIDTH, 1, 1);
		screen_flush(scr);
		usleep(50000);
	}

	/* Vertical Scan */
	printf("  [5/8] Vertical line scan\n");
	for (int x = 0; running && x < SCREEN_WIDTH; x++) {
		screen_clear(scr);
		screen_rect(scr, x, 0, 1, SCREEN_HEIGHT, 1);
		screen_flush(scr);
		usleep(50000);
	}

	/* Diagonals */
	printf("  [6/8] Diagonal cross\n");
	screen_clear(scr);
	screen_line(scr, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, 1);
	screen_line(scr, SCREEN_WIDTH - 1, 0, 0, SCREEN_HEIGHT - 1, 1);
	screen_flush(scr);
	sleep(1);

	/* Text "ATRIOS" */
	printf("  [7/8] Text pattern 'ATRIOS'\n");
	screen_clear(scr);
	screen_text(scr, 0, 0, "HELLO", 1);
	screen_text(scr, 0, 8, "ATRIOS", 1);
	screen_flush(scr);
	sleep(2);

	/* Pong */
	printf("  [8/8] Mini Pong simulation\n");
	int bx = 1, by = 1, bdx = 1, bdy = 1;
	int px = SCREEN_WIDTH / 2 - 3, py = SCREEN_HEIGHT - 2;
	for (int frame = 0; running && frame < 80; frame++) {
		screen_clear(scr);
		screen_rect(scr, px, py, 7, 1, 1);
		screen_pixel(scr, bx, by, 1);
		screen_flush(scr);
		bx += bdx; by += bdy;
		if (bx <= 0 || bx >= SCREEN_WIDTH - 1) bdx = -bdx;
		if (by <= 0) bdy = -bdy;
		if (by >= SCREEN_HEIGHT - 1) break;
		if (by == py - 1 && bx >= px && bx < px + 7) bdy = -bdy;
		usleep(40000);
	}

	screen_clear(scr);
	screen_flush(scr);
	printf("Tests completed successfully.\n");
}

/* --- Demos --- */
static void demo_bounce(quasar_screen_t *scr)
{
	int bx = 1, by = 1, bdx = 1, bdy = 1;
	int trail[10][2] = {0};
	int trail_len = 8;
	for (int f = 0; running && f < 250; f++) {
		screen_clear(scr);
		for (int i = 0; i < trail_len; i++)
			screen_pixel(scr, trail[i][0], trail[i][1], 1);
		for (int i = trail_len - 1; i > 0; i--) {
			trail[i][0] = trail[i-1][0];
			trail[i][1] = trail[i-1][1];
		}
		trail[0][0] = bx; trail[0][1] = by;
		screen_flush(scr);
		bx += bdx; by += bdy;
		if (bx <= 0 || bx >= SCREEN_WIDTH - 1) bdx = -bdx;
		if (by <= 0 || by >= SCREEN_HEIGHT - 1) bdy = -bdy;
		usleep(40000);
	}
}

static void demo_fire(quasar_screen_t *scr)
{
	uint8_t buf[SCREEN_HEIGHT + 2][SCREEN_WIDTH];
	memset(buf, 0, sizeof(buf));
	for (int f = 0; running && f < 300; f++) {
		for (int x = 0; x < SCREEN_WIDTH; x++)
			buf[SCREEN_HEIGHT][x] = (rand() % 100 > 30) ? 1 : 0;
		for (int y = 0; y < SCREEN_HEIGHT; y++) {
			for (int x = 0; x < SCREEN_WIDTH; x++) {
				int xm1 = (x > 0) ? x - 1 : SCREEN_WIDTH - 1;
				int xp1 = (x < SCREEN_WIDTH - 1) ? x + 1 : 0;
				int sum = buf[y + 1][xm1] + buf[y + 1][x] + buf[y + 1][xp1] + buf[y + 2][x];
				buf[y][x] = sum / 4;
			}
		}
		screen_clear(scr);
		for (int y = 0; y < SCREEN_HEIGHT; y++)
			for (int x = 0; x < SCREEN_WIDTH; x++)
				screen_pixel(scr, x, y, buf[y][x]);
		screen_flush(scr);
		usleep(60000);
	}
}

static void demo_stars(quasar_screen_t *scr)
{
	int stars[20][3];
	for (int i = 0; i < 20; i++) {
		stars[i][0] = rand() % SCREEN_WIDTH;
		stars[i][1] = rand() % SCREEN_HEIGHT;
		stars[i][2] = (rand() % 3) + 1;
	}
	for (int f = 0; running && f < 300; f++) {
		screen_clear(scr);
		for (int i = 0; i < 20; i++) {
			screen_pixel(scr, stars[i][0], stars[i][1], 1);
			stars[i][0] -= stars[i][2];
			if (stars[i][0] < 0) {
				stars[i][0] = SCREEN_WIDTH - 1;
				stars[i][1] = rand() % SCREEN_HEIGHT;
				stars[i][2] = (rand() % 3) + 1;
			}
		}
		screen_flush(scr);
		usleep(60000);
	}
}

static void print_usage(const char *prog)
{
	printf("Usage: %s <command> [arguments]\n\n", prog);
	printf("Commands:\n");
	printf("  test                     Run full matrix diagnostic tests\n");
	printf("  demo [bounce|fire|stars] Play animated demo pattern\n");
	printf("  text <string>            Display text string on matrix\n");
	printf("  on / off                 Turn display backlight on / off\n");
	printf("  brightness <0..200>      Set backlight brightness level\n");
	printf("  clear                    Clear all pixels\n");
	printf("  fill                     Turn all pixels ON\n");
	printf("  info                     Display framebuffer and hardware info\n");
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		print_usage(argv[0]);
		return 1;
	}

	const char *cmd = argv[1];

	if (strcmp(cmd, "info") == 0) {
		print_info();
		return 0;
	}

	if (strcmp(cmd, "on") == 0) {
		return set_backlight(150) == 0 ? 0 : 1;
	}
	if (strcmp(cmd, "off") == 0) {
		return set_backlight(0) == 0 ? 0 : 1;
	}
	if (strcmp(cmd, "brightness") == 0) {
		if (argc < 3) { fprintf(stderr, "Usage: %s brightness <0..200>\n", argv[0]); return 1; }
		return set_backlight(atoi(argv[2])) == 0 ? 0 : 1;
	}

	quasar_screen_t scr;
	if (screen_open(&scr, NULL) < 0) {
		fprintf(stderr, "atri-matrix: failed to open matrix framebuffer\n");
		return 1;
	}
	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);

	if (strcmp(cmd, "test") == 0) {
		run_tests(&scr);
	} else if (strcmp(cmd, "clear") == 0) {
		screen_clear(&scr);
		screen_flush(&scr);
	} else if (strcmp(cmd, "fill") == 0) {
		screen_rect(&scr, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 1);
		screen_flush(&scr);
	} else if (strcmp(cmd, "text") == 0) {
		if (argc < 3) { fprintf(stderr, "Usage: %s text <string>\n", argv[0]); screen_close(&scr); return 1; }
		screen_clear(&scr);
		screen_text(&scr, 0, 4, argv[2], 1);
		screen_flush(&scr);
	} else if (strcmp(cmd, "demo") == 0) {
		const char *d = (argc >= 3) ? argv[2] : "bounce";
		if (strcmp(d, "fire") == 0) demo_fire(&scr);
		else if (strcmp(d, "stars") == 0) demo_stars(&scr);
		else demo_bounce(&scr);
		screen_clear(&scr);
		screen_flush(&scr);
	} else {
		print_usage(argv[0]);
	}

	screen_close(&scr);
	return 0;
}
