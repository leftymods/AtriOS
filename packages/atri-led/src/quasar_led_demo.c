#include "quasar_screen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

static volatile int running = 1;
static void sigint_handler(int sig) { (void)sig; running = 0; }

static void demo_bounce(quasar_screen_t *scr)
{
	int bx = 1, by = 1, bdx = 1, bdy = 1;
	int trail[10][2] = {0};
	int trail_len = 8;
	for (int f = 0; running && f < 300; f++) {
		screen_clear(scr);
		for (int i = 0; i < trail_len; i++) {
			if (trail[i][0] || trail[i][1])
				screen_pixel(scr, trail[i][0], trail[i][1], 0);
		}
		for (int i = trail_len - 1; i > 0; i--) {
			trail[i][0] = trail[i-1][0];
			trail[i][1] = trail[i-1][1];
		}
		trail[0][0] = bx; trail[0][1] = by;
		screen_pixel(scr, bx, by, 1);
		screen_flush(scr);
		bx += bdx; by += bdy;
		if (bx <= 0 || bx >= SCREEN_WIDTH - 1) bdx = -bdx;
		if (by <= 0 || by >= SCREEN_HEIGHT - 1) bdy = -bdy;
		usleep(50000);
	}
}

static void demo_snake(quasar_screen_t *scr)
{
	int snake[50][2];
	int len = 8;
	int dx = 1, dy = 0;
	for (int i = 0; i < len; i++) {
		snake[i][0] = len - 1 - i;
		snake[i][1] = 0;
	}
	for (int f = 0; running && f < 500; f++) {
		screen_clear(scr);
		for (int i = 0; i < len; i++)
			screen_pixel(scr, snake[i][0], snake[i][1], i == 0 ? 1 : 0);
		screen_pixel(scr, snake[0][0], snake[0][1], 1);
		screen_flush(scr);
		int hx = snake[0][0] + dx;
		int hy = snake[0][1] + dy;
		if (hx < 0 || hx >= SCREEN_WIDTH) { dx = -dx; hx = snake[0][0] + dx; }
		if (hy < 0 || hy >= SCREEN_HEIGHT) { dy = -dy; hy = snake[0][1] + dy; }
		if (f % 20 == 0) {
			int r = rand() % 4;
			if (r == 0 && dx != 1) { dx = -1; dy = 0; }
			else if (r == 1 && dx != -1) { dx = 1; dy = 0; }
			else if (r == 2 && dy != 1) { dx = 0; dy = -1; }
			else if (r == 3 && dy != -1) { dx = 0; dy = 1; }
		}
		for (int i = len - 1; i > 0; i--) {
			snake[i][0] = snake[i-1][0];
			snake[i][1] = snake[i-1][1];
		}
		snake[0][0] = hx; snake[0][1] = hy;
		usleep(80000);
	}
}

static void demo_fire(quasar_screen_t *scr)
{
	uint8_t heat[SCREEN_WIDTH] = {0};
	for (int f = 0; running && f < 300; f++) {
		screen_clear(scr);
		for (int x = 0; x < SCREEN_WIDTH; x++) {
			if (f < 20) heat[x] = rand() % 2;
			else {
				int left = x > 0 ? heat[x-1] : 0;
				int right = x < SCREEN_WIDTH - 1 ? heat[x+1] : 0;
				int val = (heat[x] + left + right) / 3;
				val += (rand() % 3) - 1;
				if (val < 0) val = 0;
				if (val > 3) val = 3;
				heat[x] = val;
			}
			int h = heat[x] * (SCREEN_HEIGHT / 4);
			if (h > SCREEN_HEIGHT) h = SCREEN_HEIGHT;
			screen_rect(scr, x, SCREEN_HEIGHT - h, 1, h, 1);
		}
		screen_flush(scr);
		usleep(60000);
	}
}

static void demo_matrix(quasar_screen_t *scr)
{
	int drops[SCREEN_WIDTH] = {0};
	srand(time(NULL));
	for (int f = 0; running && f < 400; f++) {
		screen_clear(scr);
		for (int x = 0; x < SCREEN_WIDTH; x++) {
			if (drops[x] == 0 && rand() % 10 < 2)
				drops[x] = rand() % 3 + 1;
			if (drops[x] > 0) {
				screen_pixel(scr, x, drops[x] - 1, 1);
				drops[x]++;
				if (drops[x] > SCREEN_HEIGHT) drops[x] = 0;
			}
		}
		screen_flush(scr);
		usleep(100000);
	}
}

static void demo_stars(quasar_screen_t *scr)
{
	int stars[20][2];
	for (int i = 0; i < 20; i++) {
		stars[i][0] = rand() % SCREEN_WIDTH;
		stars[i][1] = rand() % SCREEN_HEIGHT;
	}
	for (int f = 0; running && f < 200; f++) {
		screen_clear(scr);
		for (int i = 0; i < 20; i++) {
			screen_pixel(scr, stars[i][0], stars[i][1], 1);
			stars[i][1]++;
			if (stars[i][1] >= SCREEN_HEIGHT) {
				stars[i][0] = rand() % SCREEN_WIDTH;
				stars[i][1] = 0;
			}
		}
		screen_flush(scr);
		usleep(100000);
	}
}

static void demo_tunnel(quasar_screen_t *scr)
{
	for (int f = 0; running && f < 200; f++) {
		screen_clear(scr);
		int cx = SCREEN_WIDTH / 2, cy = SCREEN_HEIGHT / 2;
		for (int r = 1; r <= 12; r++) {
			int phase = (f + r * 3) % 12;
			int radius = r * 2;
			int on = phase < 6;
			screen_rect(scr, cx - radius, cy - radius, radius * 2, radius * 2, on);
		}
		screen_flush(scr);
		usleep(80000);
	}
}

int main(int argc, char *argv[])
{
	const char *demo = NULL;

	if (argc < 2) {
		printf("Usage: %s [options] [demo]\n", argv[0]);
		printf("  -l         list demos\n");
		printf("\nDemos: bounce snake fire matrix stars tunnel all\n");
		return 1;
	}

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-l") == 0) {
			printf("bounce   - bouncing ball with trail\n");
			printf("snake    - snake game simulation\n");
			printf("fire     - fire effect\n");
			printf("matrix   - matrix rain\n");
			printf("stars    - scrolling starfield\n");
			printf("tunnel   - expanding rings\n");
			printf("all      - run all demos sequentially\n");
			return 0;
		}
		else demo = argv[i];
	}

	if (!demo) { fprintf(stderr, "Specify a demo or -l to list\n"); return 1; }

	quasar_screen_t scr;
	if (screen_open(&scr, NULL) < 0) return 1;
	screen_reset(&scr);

	signal(SIGINT, sigint_handler);
	srand(time(NULL));

	struct { const char *name; void (*fn)(quasar_screen_t*); } demos[] = {
		{"bounce", demo_bounce},
		{"snake",  demo_snake},
		{"fire",   demo_fire},
		{"matrix", demo_matrix},
		{"stars",  demo_stars},
		{"tunnel", demo_tunnel},
	};

	if (strcmp(demo, "all") == 0) {
		int nd = sizeof(demos) / sizeof(demos[0]);
		for (int i = 0; i < nd && running; i++) {
			printf("Demo: %s\n", demos[i].name);
			screen_clear(&scr);
			screen_text(&scr, 0, 4, demos[i].name, 1);
			screen_flush(&scr);
			sleep(1);
			demos[i].fn(&scr);
		}
	} else {
		for (size_t i = 0; i < sizeof(demos)/sizeof(demos[0]); i++) {
			if (strcmp(demo, demos[i].name) == 0) {
				printf("Demo: %s\n", demo);
				demos[i].fn(&scr);
				goto done;
			}
		}
		fprintf(stderr, "Unknown demo: %s\n", demo);
	}

done:
	screen_clear(&scr);
	screen_flush(&scr);
	screen_close(&scr);
	printf("Demo finished\n");
	return 0;
}
