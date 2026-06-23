#include "atri_led.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <glob.h>
#include <errno.h>

#define LED_CLASS_PATH "/sys/class/leds"

static char *ring_multi_intensity[ATRI_LED_MAX_RINGS];
static char *ring_brightness[ATRI_LED_MAX_RINGS];

static int ring_found[ATRI_LED_MAX_RINGS];

static void load_ring_leds(void)
{
	glob_t gl;
	for (int r = 0; r < ATRI_LED_MAX_RINGS; r++) {
		char pattern[192];
		ring_found[r] = 0;

		snprintf(pattern, sizeof(pattern), LED_CLASS_PATH "/ring%d/multi_intensity", r);
		if (ring_multi_intensity[r]) { free(ring_multi_intensity[r]); ring_multi_intensity[r] = NULL; }
		if (glob(pattern, 0, NULL, &gl) == 0 && gl.gl_pathc > 0) {
			ring_multi_intensity[r] = strdup(gl.gl_pathv[0]);
			ring_found[r] = 1;
		}
		globfree(&gl);

		snprintf(pattern, sizeof(pattern), LED_CLASS_PATH "/ring%d/brightness", r);
		if (ring_brightness[r]) { free(ring_brightness[r]); ring_brightness[r] = NULL; }
		if (glob(pattern, 0, NULL, &gl) == 0 && gl.gl_pathc > 0) {
			ring_brightness[r] = strdup(gl.gl_pathv[0]);
		}
		globfree(&gl);
	}
}

int atri_led_init(struct atri_led *led)
{
	memset(led, 0, sizeof(*led));
	load_ring_leds();
	led->ring_count = 0;
	for (int r = 0; r < ATRI_LED_MAX_RINGS; r++) {
		if (ring_found[r]) led->ring_count = r + 1;
	}
	return 0;
}

int atri_led_ring_count(void)
{
	int count = 0;
	for (int r = 0; r < ATRI_LED_MAX_RINGS; r++) {
		if (ring_found[r]) count++;
	}
	return count;
}

static int write_sysfs(const char *path, const char *buf)
{
	if (!path) return -1;
	int fd = open(path, O_WRONLY);
	if (fd < 0) return -errno;
	int len = strlen(buf);
	int ret = write(fd, buf, len);
	close(fd);
	if (ret != len) return -1;
	return 0;
}

static int write_sysfs_val(const char *path, int val)
{
	char buf[16];
	snprintf(buf, sizeof(buf), "%d\n", val);
	return write_sysfs(path, buf);
}

void atri_led_set_ring_rgb(struct atri_led *led, int ring, uint8_t r, uint8_t g, uint8_t b)
{
	if (ring >= led->ring_count || ring < 0) return;
	led->brightness[ring][RGB_R] = r;
	led->brightness[ring][RGB_G] = g;
	led->brightness[ring][RGB_B] = b;
}

int atri_led_apply(struct atri_led *led)
{
	int err = 0;
	for (int r = 0; r < led->ring_count; r++) {
		if (!ring_found[r]) continue;
		if (ring_brightness[r])
			write_sysfs_val(ring_brightness[r], 255);
		if (ring_multi_intensity[r]) {
			char buf[32];
			snprintf(buf, sizeof(buf), "%d %d %d",
				led->brightness[r][RGB_R],
				led->brightness[r][RGB_G],
				led->brightness[r][RGB_B]);
			if (write_sysfs(ring_multi_intensity[r], buf) < 0)
				err = -1;
		}
	}
	return err;
}

int atri_led_set_all_rgb(struct atri_led *led, uint8_t r, uint8_t g, uint8_t b)
{
	for (int i = 0; i < led->ring_count; i++) {
		led->brightness[i][RGB_R] = r;
		led->brightness[i][RGB_G] = g;
		led->brightness[i][RGB_B] = b;
	}
	return atri_led_apply(led);
}

int atri_led_off(struct atri_led *led)
{
	for (int i = 0; i < led->ring_count; i++) {
		led->brightness[i][RGB_R] = 0;
		led->brightness[i][RGB_G] = 0;
		led->brightness[i][RGB_B] = 0;
		if (ring_brightness[i])
			write_sysfs_val(ring_brightness[i], 0);
	}
	return atri_led_apply(led);
}

void atri_led_fade_to(struct atri_led *led, int ring, uint8_t r, uint8_t g, uint8_t b, int steps, int delay_ms)
{
	if (ring >= led->ring_count || ring < 0) return;
	uint8_t cr = led->brightness[ring][RGB_R];
	uint8_t cg = led->brightness[ring][RGB_G];
	uint8_t cb = led->brightness[ring][RGB_B];

	for (int i = 1; i <= steps; i++) {
		led->brightness[ring][RGB_R] = cr + (r - cr) * i / steps;
		led->brightness[ring][RGB_G] = cg + (g - cg) * i / steps;
		led->brightness[ring][RGB_B] = cb + (b - cb) * i / steps;
		atri_led_apply(led);
		usleep(delay_ms * 1000);
	}
}

void atri_led_rainbow(struct atri_led *led)
{
	for (int r = 0; r < led->ring_count; r++) {
		int hue = (r * 256 / led->ring_count) % 256;
		uint8_t rv, gv, bv;
		if (hue < 85) { rv = 255 - hue * 3; gv = hue * 3; bv = 0; }
		else if (hue < 170) { rv = 0; gv = 255 - (hue - 85) * 3; bv = (hue - 85) * 3; }
		else { rv = (hue - 170) * 3; gv = 0; bv = 255 - (hue - 170) * 3; }
		led->brightness[r][RGB_R] = rv;
		led->brightness[r][RGB_G] = gv;
		led->brightness[r][RGB_B] = bv;
	}
	atri_led_apply(led);
}

void atri_led_anim_color(struct atri_led *led, const char *name)
{
	uint8_t r = 0, g = 0, b = 0;

	if (strcmp(name, "happy") == 0) { r = 100; g = 255; b = 50; }
	else if (strcmp(name, "sad") == 0) { r = 50; g = 100; b = 255; }
	else if (strcmp(name, "excited") == 0) { r = 255; g = 80; b = 50; }
	else if (strcmp(name, "calm") == 0) { r = 50; g = 150; b = 255; }
	else if (strcmp(name, "focused") == 0) { r = 200; g = 100; b = 255; }
	else if (strcmp(name, "love") == 0) { r = 255; g = 50; b = 100; }
	else if (strcmp(name, "party") == 0) { atri_led_rainbow(led); return; }
	else { r = 255; g = 255; b = 255; }

	atri_led_set_all_rgb(led, r, g, b);
}

int atri_led_anim_to_rgb(const char *name, uint8_t *r, uint8_t *g, uint8_t *b)
{
	if (strcmp(name, "happy") == 0) { *r = 100; *g = 255; *b = 50; }
	else if (strcmp(name, "sad") == 0) { *r = 50; *g = 100; *b = 255; }
	else if (strcmp(name, "excited") == 0) { *r = 255; *g = 80; *b = 50; }
	else if (strcmp(name, "calm") == 0) { *r = 50; *g = 150; *b = 255; }
	else if (strcmp(name, "focused") == 0) { *r = 200; *g = 100; *b = 255; }
	else if (strcmp(name, "love") == 0) { *r = 255; *g = 50; *b = 100; }
	else return -1;
	return 0;
}
