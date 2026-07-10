#include "quasar_screen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
static volatile int running = 1;
static void sigint_handler(int sig) { (void)sig; running = 0; }

static void backlight_write(int val)
{
	char cmd[200];
	snprintf(cmd, sizeof(cmd),
		"echo %d > /sys/class/backlight/atri_led_panel/brightness 2>/dev/null", val);
	if (system(cmd) < 0) cmd[0] = '\0';
}

static void demo_bounce(quasar_screen_t *scr)
{
	int bx = 1, by = 1, bdx = 1, bdy = 1;
	int trail[10][2] = {0};
	int trail_len = 8;
	for (int f = 0; running && f < 300; f++) {
		screen_clear(scr);
		for (int i = 0; i < trail_len; i++)
			if (trail[i][0] || trail[i][1])
				screen_pixel(scr, trail[i][0], trail[i][1], 0);
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
	for (int i = 0; i < len; i++) { snake[i][0] = len - 1 - i; snake[i][1] = 0; }
	for (int f = 0; running && f < 500; f++) {
		screen_clear(scr);
		for (int i = 0; i < len; i++)
			screen_pixel(scr, snake[i][0], snake[i][1], i == 0 ? 1 : 0);
		screen_pixel(scr, snake[0][0], snake[0][1], 1);
		screen_flush(scr);
		int hx = snake[0][0] + dx, hy = snake[0][1] + dy;
		if (hx < 0 || hx >= SCREEN_WIDTH) { dx = -dx; hx = snake[0][0] + dx; }
		if (hy < 0 || hy >= SCREEN_HEIGHT) { dy = -dy; hy = snake[0][1] + dy; }
		if (f % 20 == 0) {
			int r = rand() % 4;
			if (r == 0 && dx != 1) { dx = -1; dy = 0; }
			else if (r == 1 && dx != -1) { dx = 1; dy = 0; }
			else if (r == 2 && dy != 1) { dx = 0; dy = -1; }
			else if (r == 3 && dy != -1) { dx = 0; dy = 1; }
		}
		for (int i = len - 1; i > 0; i--) { snake[i][0] = snake[i-1][0]; snake[i][1] = snake[i-1][1]; }
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
	for (int i = 0; i < 20; i++) { stars[i][0] = rand() % SCREEN_WIDTH; stars[i][1] = rand() % SCREEN_HEIGHT; }
	for (int f = 0; running && f < 200; f++) {
		screen_clear(scr);
		for (int i = 0; i < 20; i++) {
			screen_pixel(scr, stars[i][0], stars[i][1], 1);
			stars[i][1]++;
			if (stars[i][1] >= SCREEN_HEIGHT) { stars[i][0] = rand() % SCREEN_WIDTH; stars[i][1] = 0; }
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
			screen_rect(scr, cx - radius, cy - radius, radius * 2, radius * 2, phase < 6);
		}
		screen_flush(scr);
		usleep(80000);
	}
}

static void pattern_checkerboard(quasar_screen_t *scr)
{
	for (int y = 0; y < SCREEN_HEIGHT; y++)
		for (int x = 0; x < SCREEN_WIDTH; x++)
			screen_pixel(scr, x, y, (x + y) % 2);
	screen_flush(scr);
}

static void pattern_borders(quasar_screen_t *scr)
{
	screen_clear(scr);
	screen_rect(scr, 0, 0, SCREEN_WIDTH, 1, 1);
	screen_rect(scr, 0, SCREEN_HEIGHT - 1, SCREEN_WIDTH, 1, 1);
	screen_rect(scr, 0, 0, 1, SCREEN_HEIGHT, 1);
	screen_rect(scr, SCREEN_WIDTH - 1, 0, 1, SCREEN_HEIGHT, 1);
	screen_flush(scr);
}

static void pattern_diagonal(quasar_screen_t *scr)
{
	screen_clear(scr);
	screen_line(scr, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, 1);
	screen_line(scr, SCREEN_WIDTH - 1, 0, 0, SCREEN_HEIGHT - 1, 1);
	screen_flush(scr);
}

static void pattern_all_on(quasar_screen_t *scr)
{
	screen_rect(scr, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 1);
	screen_flush(scr);
}

static void print_usage(const char *prog)
{
	printf("Usage: %s <command> [args]\n\n", prog);
	printf("Commands:\n");
	printf("  play <demo>            Run demo: bounce snake fire matrix stars tunnel\n");
	printf("  anim <pattern>         Show static pattern: checkers borders diagonal fill\n");
	printf("  text [opts] <text>     Show scrolling text\n");
	printf("  info                   Show system info on screen\n");
	printf("  brightness <0-200>     Set backlight brightness\n");
	printf("  on                     Turn on backlight\n");
	printf("  off                    Turn off backlight\n");
	printf("  clear                  Clear screen\n");
	printf("  probe                  Check if screen driver is loaded\n");
	printf("\nOptions for text:\n");
	printf("  -s          single frame, no scroll\n");
	printf("  -r <ms>     scroll speed (default: 100)\n");
}

int main(int argc, char *argv[])
{
	if (argc < 2) { print_usage(argv[0]); return 1; }

	const char *cmd = argv[1];

	if (strcmp(cmd, "probe") == 0) {
		char fb_path[32];
		struct fb_fix_screeninfo fix;
		for (int i = 0; i < 8; i++) {
			snprintf(fb_path, sizeof(fb_path), "/dev/fb%d", i);
			int fd = open(fb_path, O_RDONLY);
			if (fd < 0) continue;
		if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) == 0 &&
		    strcmp(fix.id, "atri_led_panel_fb") == 0) {
			close(fd);
			printf("Quasar LED screen: detected (%s)\n", fb_path);
			return 0;
		}
	}
	printf("Quasar LED screen: not found\n");
		return 1;

	} else if (strcmp(cmd, "on") == 0) {
		backlight_write(200);
		return 0;

	} else if (strcmp(cmd, "off") == 0) {
		backlight_write(0);
		return 0;

	} else if (strcmp(cmd, "brightness") == 0) {
		if (argc < 3) { fprintf(stderr, "Usage: %s brightness <0-200>\n", argv[0]); return 1; }
		backlight_write(atoi(argv[2]));
		return 0;

	} else if (strcmp(cmd, "clear") == 0) {
		quasar_screen_t scr;
		if (screen_open(&scr, NULL) < 0) return 1;
		screen_clear(&scr);
		screen_flush(&scr);
		screen_close(&scr);
		return 0;

	} else if (strcmp(cmd, "anim") == 0) {
		if (argc < 3) { fprintf(stderr, "Usage: %s anim <checkers|borders|diagonal|fill>\n", argv[0]); return 1; }
		quasar_screen_t scr;
		if (screen_open(&scr, NULL) < 0) return 1;

		if (strcmp(argv[2], "checkers") == 0 || strcmp(argv[2], "checkerboard") == 0)
			pattern_checkerboard(&scr);
		else if (strcmp(argv[2], "borders") == 0)
			pattern_borders(&scr);
		else if (strcmp(argv[2], "diagonal") == 0)
			pattern_diagonal(&scr);
		else if (strcmp(argv[2], "fill") == 0 || strcmp(argv[2], "all") == 0)
			pattern_all_on(&scr);
		else
			printf("Unknown pattern: %s (try: checkers, borders, diagonal, fill)\n", argv[2]);

		screen_close(&scr);
		return 0;

	} else if (strcmp(cmd, "play") == 0) {
		if (argc < 3) { fprintf(stderr, "Usage: %s play <bounce|snake|fire|matrix|stars|tunnel>\n", argv[0]); return 1; }
		quasar_screen_t scr;
		if (screen_open(&scr, NULL) < 0) return 1;
		signal(SIGINT, sigint_handler);
		srand(time(NULL));

		struct { const char *name; void (*fn)(quasar_screen_t*); } demos[] = {
			{"bounce", demo_bounce}, {"snake", demo_snake}, {"fire", demo_fire},
			{"matrix", demo_matrix}, {"stars", demo_stars}, {"tunnel", demo_tunnel},
		};

		int found = 0;
		for (size_t i = 0; i < sizeof(demos)/sizeof(demos[0]); i++) {
			if (strcmp(argv[2], demos[i].name) == 0) {
				printf("Playing: %s\n", demos[i].name);
				screen_clear(&scr);
				screen_text(&scr, 0, 4, demos[i].name, 1);
				screen_flush(&scr);
				sleep(1);
				demos[i].fn(&scr);
				found = 1;
				break;
			}
		}
		if (!found)
			printf("Unknown demo: %s (try: bounce, snake, fire, matrix, stars, tunnel)\n", argv[2]);

		screen_clear(&scr);
		screen_flush(&scr);
		screen_close(&scr);
		return 0;

	} else if (strcmp(cmd, "text") == 0) {
		const char *text = NULL;
		int single = 0;
		int speed_ms = 100;

		for (int i = 2; i < argc; i++) {
			if (strcmp(argv[i], "-s") == 0) single = 1;
			else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) speed_ms = atoi(argv[++i]);
			else text = argv[i];
		}
		if (!text) { fprintf(stderr, "No text provided\n"); return 1; }

		quasar_screen_t scr;
		if (screen_open(&scr, NULL) < 0) return 1;
		signal(SIGINT, sigint_handler);
		running = 1;

		if (single) {
			screen_clear(&scr);
			screen_text(&scr, 0, 4, text, 1);
			screen_flush(&scr);
		} else {
			int scroll = 0;
			int text_len = strlen(text);
			int total_frames = (text_len * 6) + SCREEN_WIDTH;
			for (int i = 0; i < total_frames && running; i++) {
				screen_clear(&scr);
				int x = SCREEN_WIDTH - scroll;
				for (const char *p = text; *p; p++) {
					if (x + 5 >= 0)
						screen_text(&scr, x, 4, (char[]){*p, 0}, 1);
					x += 6;
				}
				screen_flush(&scr);
				scroll++;
				usleep(speed_ms * 1000);
			}
		}

		screen_clear(&scr);
		screen_flush(&scr);
		screen_close(&scr);
		return 0;

	} else if (strcmp(cmd, "info") == 0) {
		quasar_screen_t scr;
		if (screen_open(&scr, NULL) < 0) return 1;

		char hostname[32] = "", ip[32] = "", uptime[32] = "";

		FILE *f = fopen("/proc/sys/kernel/hostname", "r");
		if (f) {
			if (fgets(hostname, sizeof(hostname), f)) hostname[strcspn(hostname, "\n")] = 0;
			fclose(f);
		}

		f = popen("ip -4 addr show | grep inet | head -1 | awk '{print $2}' | cut -d/ -f1", "r");
		if (f) {
			if (fgets(ip, sizeof(ip), f)) ip[strcspn(ip, "\n")] = 0;
			pclose(f);
		}
		if (!ip[0]) strcpy(ip, "no network");

		f = fopen("/proc/uptime", "r");
		if (f) {
			if (fgets(uptime, sizeof(uptime), f)) {
				int secs = atoi(uptime);
				int h = secs / 3600, m = (secs % 3600) / 60;
				snprintf(uptime, sizeof(uptime), "%02d:%02dh", h, m);
			}
			fclose(f);
		}
		int secs = atoi(uptime);
		int h = secs / 3600, m = (secs % 3600) / 60;
		snprintf(uptime, sizeof(uptime), "%02d:%02dh", h, m);

		signal(SIGINT, sigint_handler);
		for (int cycle = 0; running && cycle < 20; cycle++) {
			screen_clear(&scr);
			screen_text(&scr, 0, 0, hostname, 1);
			screen_text(&scr, 0, 8, cycle % 2 ? uptime : ip, 1);
			screen_flush(&scr);
			for (int s = 0; s < 3 && running; s++) sleep(1);
		}

		screen_clear(&scr);
		screen_flush(&scr);
		screen_close(&scr);
		return 0;

	} else {
		fprintf(stderr, "Unknown command: %s\n", cmd);
		print_usage(argv[0]);
		return 1;
	}
}
