#include "quasar_screen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_LINE_LEN 64

static void render_line(quasar_screen_t *scr, const char *line, int scroll)
{
	screen_clear(scr);
	int len = strlen(line);
	int text_pixels = len * 6;
	int offset = scroll % (text_pixels + SCREEN_WIDTH);
	int x = SCREEN_WIDTH - offset;
	for (const char *p = line; *p && x < SCREEN_WIDTH; p++) {
		if (x + 5 >= 0)
			screen_text(scr, x, 4, (char[]){*p, 0}, 1);
		x += 6;
	}
	screen_flush(scr);
}

int main(int argc, char *argv[])
{
	const char *dev = "/dev/spidev1.0";
	int gpio = 74;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s [options] <text...>\n", argv[0]);
		fprintf(stderr, "  -d <dev>   SPI device (default: /dev/spidev1.0)\n");
		fprintf(stderr, "  -g <gpio>  GPIO reset pin (default: 74)\n");
		fprintf(stderr, "  -s         single frame, no scroll\n");
		fprintf(stderr, "  -r <ms>    scroll speed (default: 100)\n");
		return 1;
	}

	const char *text = NULL;
	int single = 0;
	int speed_ms = 100;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) dev = argv[++i];
		else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) gpio = atoi(argv[++i]);
		else if (strcmp(argv[i], "-s") == 0) single = 1;
		else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) speed_ms = atoi(argv[++i]);
		else text = argv[i];
	}

	if (!text) {
		fprintf(stderr, "No text provided\n");
		return 1;
	}

	quasar_screen_t scr;
	if (screen_open(&scr, dev) < 0) return 1;
	scr.gpio_reset = gpio;
	screen_reset(&scr);

	if (single) {
		screen_clear(&scr);
		screen_text(&scr, 0, 4, text, 1);
		screen_flush(&scr);
	} else {
		int scroll = 0;
		int text_len = strlen(text);
		int total_frames = (text_len * 6) + SCREEN_WIDTH;
		for (int i = 0; i < total_frames; i++) {
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
}
