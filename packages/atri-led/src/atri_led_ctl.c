#include "atri_led.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>

static void print_usage(const char *prog)
{
	printf("Usage: %s [options]\n\n", prog);
	printf("Options:\n");
	printf("  -r, --ring NUM     Ring number (0-N)\n");
	printf("  -R, --red VAL      Red value (0-255)\n");
	printf("  -G, --green VAL    Green value (0-255)\n");
	printf("  -B, --blue VAL     Blue value (0-255)\n");
	printf("  -a, --all          Apply to all rings\n");
	printf("  -b, --brightness   Set global brightness (0-255)\n");
	printf("  -a, --anim  NAME    Set animation color\n");
	printf("  -l, --list         List available rings\n");
	printf("  -s, --status       Show current ring status\n");
	printf("  -o, --off          Turn off all LEDs\n");
	printf("  -h, --help         Show this help\n\n");
	printf("Animations: happy, sad, excited, calm, focused, love, party\n");
	printf("Examples:\n");
	printf("  %s -a -R 255 -G 100 -B 50       All rings warm orange\n", prog);
	printf("  %s -r 0 -R 0 -G 0 -B 255         Ring 0 blue\n", prog);
	printf("  %s -a happy                      Set happy animation\n", prog);
	printf("  %s -b 128                        Set brightness to 50%%\n", prog);
	printf("  %s -o                            All off\n", prog);
}

static void show_status(struct atri_led *led)
{
	printf("LED rings found: %d\n", led->ring_count);
	for (int i = 0; i < led->ring_count; i++) {
		printf("  ring%-2d: R=%-3d G=%-3d B=%-3d\n",
			i,
			led->brightness[i][RGB_R],
			led->brightness[i][RGB_G],
			led->brightness[i][RGB_B]);
	}
}

static void list_rings(struct atri_led *led)
{
	printf("Available LED rings: %d (0-%d)\n", led->ring_count, led->ring_count - 1);
	for (int i = 0; i < led->ring_count; i++) {
		printf("  /sys/class/leds/ring%d/\n", i);
	}
}

int main(int argc, char *argv[])
{
	struct atri_led led;
	int ring = -1, all = 0, brightness = -1, off = 0, list = 0, status = 0;
	uint8_t r = 0, g = 0, b = 0;
	const char *anim_name = NULL;
	int has_rgb = 0;

	static struct option opts[] = {
		{"ring", required_argument, 0, 'r'},
		{"red", required_argument, 0, 'R'},
		{"green", required_argument, 0, 'G'},
		{"blue", required_argument, 0, 'B'},
		{"all", no_argument, 0, 'A'},
		{"anim", required_argument, 0, 'a'},
		{"brightness", required_argument, 0, 'b'},
		{"off", no_argument, 0, 'o'},
		{"list", no_argument, 0, 'l'},
		{"status", no_argument, 0, 's'},
		{"help", no_argument, 0, 'h'},
		{NULL, 0, 0, 0}
	};

	int c;
	while ((c = getopt_long(argc, argv, "r:R:G:B:Aa:b:olsh", opts, NULL)) != -1) {
		switch (c) {
			case 'r': ring = atoi(optarg); break;
			case 'R': r = atoi(optarg); has_rgb = 1; break;
			case 'G': g = atoi(optarg); has_rgb = 1; break;
			case 'B': b = atoi(optarg); has_rgb = 1; break;
			case 'A': all = 1; break;
			case 'a': anim_name = optarg; break;
			case 'b': brightness = atoi(optarg); break;
			case 'o': off = 1; break;
			case 'l': list = 1; break;
			case 's': status = 1; break;
			case 'h': print_usage(argv[0]); return 0;
			default: return 1;
		}
	}

	if (atri_led_init(&led) < 0) {
		fprintf(stderr, "Error: cannot access LED sysfs\n");
		return 1;
	}

	if (led.ring_count == 0) {
		fprintf(stderr, "Error: no LED rings found in /sys/class/leds/\n");
		fprintf(stderr, "Check that the LED driver is loaded and DT has led nodes.\n");
		return 1;
	}

	if (list) {
		list_rings(&led);
		return 0;
	}

	if (status) {
		show_status(&led);
		return 0;
	}

	if (off) {
		atri_led_off(&led);
		printf("LEDs turned off\n");
		return 0;
	}

	if (anim_name) {
		atri_led_anim_color(&led, anim_name);
		printf("Animation set to: %s\n", anim_name);
		return 0;
	}

	if (brightness >= 0) {
		if (brightness > 255) brightness = 255;
		r = brightness; g = brightness; b = brightness;
		has_rgb = 1;
		all = 1;
	}

	if (has_rgb) {
		if (ring >= 0 && !all) {
			if (ring >= led.ring_count) {
				fprintf(stderr, "Error: ring %d out of range (0-%d)\n", ring, led.ring_count - 1);
				return 1;
			}
			atri_led_set_ring_rgb(&led, ring, r, g, b);
		} else {
			atri_led_set_all_rgb(&led, r, g, b);
		}
		if (anim_name == NULL) {
			atri_led_apply(&led);
			if (ring >= 0 && !all)
				printf("Ring %d set to RGB(%d,%d,%d)\n", ring, r, g, b);
			else
				printf("All rings set to RGB(%d,%d,%d)\n", r, g, b);
		}
		return 0;
	}

	if (argc == 1) {
		print_usage(argv[0]);
		return 1;
	}

	return 0;
}
