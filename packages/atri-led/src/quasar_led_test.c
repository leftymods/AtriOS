#include "quasar_screen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static void pattern_all_on(quasar_screen_t *scr)
{
	printf("all on\n");
	screen_rect(scr, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 1);
	screen_flush(scr);
	sleep(1);
}

static void pattern_all_off(quasar_screen_t *scr)
{
	printf("all off\n");
	screen_clear(scr);
	screen_flush(scr);
	usleep(500000);
}

static void pattern_hscan(quasar_screen_t *scr)
{
	printf("horizontal scan\n");
	for (int y = 0; y < SCREEN_HEIGHT; y++) {
		screen_clear(scr);
		screen_rect(scr, 0, y, SCREEN_WIDTH, 1, 1);
		screen_flush(scr);
		usleep(80000);
	}
}

static void pattern_vscan(quasar_screen_t *scr)
{
	printf("vertical scan\n");
	for (int x = 0; x < SCREEN_WIDTH; x++) {
		screen_clear(scr);
		screen_rect(scr, x, 0, 1, SCREEN_HEIGHT, 1);
		screen_flush(scr);
		usleep(80000);
	}
}

static void pattern_checkerboard(quasar_screen_t *scr)
{
	printf("checkerboard\n");
	for (int y = 0; y < SCREEN_HEIGHT; y++)
		for (int x = 0; x < SCREEN_WIDTH; x++)
			screen_pixel(scr, x, y, (x + y) % 2);
	screen_flush(scr);
	sleep(1);
}

static void pattern_borders(quasar_screen_t *scr)
{
	printf("borders\n");
	screen_clear(scr);
	screen_rect(scr, 0, 0, SCREEN_WIDTH, 1, 1);
	screen_rect(scr, 0, SCREEN_HEIGHT - 1, SCREEN_WIDTH, 1, 1);
	screen_rect(scr, 0, 0, 1, SCREEN_HEIGHT, 1);
	screen_rect(scr, SCREEN_WIDTH - 1, 0, 1, SCREEN_HEIGHT, 1);
	screen_flush(scr);
	sleep(1);
}

static void pattern_diagonal(quasar_screen_t *scr)
{
	printf("diagonal lines\n");
	screen_clear(scr);
	screen_line(scr, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, 1);
	screen_line(scr, SCREEN_WIDTH - 1, 0, 0, SCREEN_HEIGHT - 1, 1);
	screen_line(scr, 0, SCREEN_HEIGHT / 2, SCREEN_WIDTH - 1, SCREEN_HEIGHT / 2, 1);
	screen_line(scr, SCREEN_WIDTH / 2, 0, SCREEN_WIDTH / 2, SCREEN_HEIGHT - 1, 1);
	screen_flush(scr);
	sleep(1);
}

static void pattern_countdown(quasar_screen_t *scr)
{
	printf("countdown\n");
	for (int i = 9; i >= 0; i--) {
		screen_clear(scr);
		char buf[2] = {(char)('0' + i), 0};
		screen_text(scr, SCREEN_WIDTH / 2 - 3, SCREEN_HEIGHT / 2 - 4, buf, 1);
		screen_flush(scr);
		usleep(300000);
	}
}

static void pattern_text(quasar_screen_t *scr)
{
	printf("text display\n");
	screen_clear(scr);
	screen_text(scr, 0, 0, "HELLO", 1);
	screen_text(scr, 0, 8, "QUASAR", 1);
	screen_flush(scr);
	sleep(2);
}

static void pattern_pong(quasar_screen_t *scr)
{
	printf("pong demo\n");
	int bx = 1, by = 1, bdx = 1, bdy = 1;
	int px = SCREEN_WIDTH / 2 - 3, py = SCREEN_HEIGHT - 2;
	for (int frame = 0; frame < 100; frame++) {
		screen_clear(scr);
		screen_rect(scr, px, py, 7, 1, 1);
		screen_pixel(scr, bx, by, 1);
		screen_flush(scr);
		bx += bdx; by += bdy;
		if (bx <= 0 || bx >= SCREEN_WIDTH - 1) bdx = -bdx;
		if (by <= 0) bdy = -bdy;
		if (by >= SCREEN_HEIGHT - 1) break;
		if (by == py - 1 && bx >= px && bx < px + 7) bdy = -bdy;
		usleep(80000);
	}
}

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	quasar_screen_t scr;
	if (screen_open(&scr, NULL) < 0) {
		fprintf(stderr, "Failed to open atri_led_panel framebuffer\n");
		return 1;
	}
	screen_reset(&scr);

	printf("Quasar 25x16 LED screen test (via atri_led_panel fb)\n\n");

	pattern_all_on(&scr);
	pattern_all_off(&scr);
	pattern_borders(&scr);
	pattern_hscan(&scr);
	pattern_vscan(&scr);
	pattern_diagonal(&scr);
	pattern_checkerboard(&scr);
	pattern_text(&scr);
	pattern_countdown(&scr);
	pattern_pong(&scr);

	screen_clear(&scr);
	screen_flush(&scr);
	screen_close(&scr);
	printf("\nTest complete\n");
	return 0;
}
