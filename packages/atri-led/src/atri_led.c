#include "atri_led.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <glob.h>
#include <errno.h>
#include <math.h>

#define LED_CLASS_PATH "/sys/class/leds"

static char *ring_multi_intensity[ATRI_LED_MAX_RINGS];
static char *ring_brightness[ATRI_LED_MAX_RINGS];

static int ring_found[ATRI_LED_MAX_RINGS];

/* gamma 2.2 LUT, built once — perceptually linear LED output */
static uint8_t gamma_lut[256];
static int gamma_ready = 0;

static void build_gamma_lut(void)
{
	if (gamma_ready) return;
	for (int i = 0; i < 256; i++) {
		double x = i / 255.0;
		int v = (int)(pow(x, 2.2) * 255.0 + 0.5);
		gamma_lut[i] = v > 255 ? 255 : v;
	}
	gamma_lut[0] = 0;
	gamma_lut[255] = 255;
	gamma_ready = 1;
}

static void load_ring_leds(void)
{
	glob_t gl;
	for (int r = 0; r < ATRI_LED_MAX_RINGS; r++) {
		char pattern[192];
		ring_found[r] = 0;

		snprintf(pattern, sizeof(pattern), LED_CLASS_PATH "/ring%d/multi_intensity", r);
		if (ring_multi_intensity[r]) { free(ring_multi_intensity[r]); ring_multi_intensity[r] = NULL; }
		int gr = glob(pattern, 0, NULL, &gl);
		if (gr == 0 && gl.gl_pathc > 0) {
			ring_multi_intensity[r] = strdup(gl.gl_pathv[0]);
			ring_found[r] = 1;
		}
		if (gr == 0)
			globfree(&gl);	/* only valid after successful glob */

		snprintf(pattern, sizeof(pattern), LED_CLASS_PATH "/ring%d/brightness", r);
		if (ring_brightness[r]) { free(ring_brightness[r]); ring_brightness[r] = NULL; }
		gr = glob(pattern, 0, NULL, &gl);
		if (gr == 0 && gl.gl_pathc > 0)
			ring_brightness[r] = strdup(gl.gl_pathv[0]);
		if (gr == 0)
			globfree(&gl);
	}
}

int atri_led_init(struct atri_led *led)
{
	memset(led, 0, sizeof(*led));
	build_gamma_lut();
	led->master = 255;
	led->gamma_en = 1;
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

void atri_led_set_master(struct atri_led *led, uint8_t master)
{
	led->master = master;
}

uint8_t atri_led_get_master(struct atri_led *led)
{
	return led->master;
}

void atri_led_set_gamma(struct atri_led *led, int enable)
{
	led->gamma_en = enable ? 1 : 0;
	build_gamma_lut();
}

static inline uint8_t scale_chan(struct atri_led *led, uint8_t v)
{
	if (led->master != 255)
		v = (uint8_t)((v * led->master) / 255);
	if (led->gamma_en)
		v = gamma_lut[v];
	return v;
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
				scale_chan(led, led->brightness[r][RGB_R]),
				scale_chan(led, led->brightness[r][RGB_G]),
				scale_chan(led, led->brightness[r][RGB_B]));
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
		int hue = r * 360 / led->ring_count;
		uint8_t rv, gv, bv;
		atri_led_hsv_to_rgb(hue, 255, 255, &rv, &gv, &bv);
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

void atri_led_hsv_to_rgb(int h, int s, int v, uint8_t *r, uint8_t *g, uint8_t *b)
{
	int region, remainder, p, q, t;
	h %= 360;
	if (h < 0) h += 360;
	if (s <= 0) {
		*r = *g = *b = (uint8_t)v;
		return;
	}
	if (s > 255) s = 255;
	if (v > 255) v = 255;
	region = h / 60;
	remainder = (h % 60) * 255 / 60;
	p = (v * (255 - s)) / 255;
	q = (v * (255 - ((s * remainder) / 255))) / 255;
	t = (v * (255 - ((s * (255 - remainder)) / 255))) / 255;
	switch (region) {
	case 0: *r = v; *g = t; *b = p; break;
	case 1: *r = q; *g = v; *b = p; break;
	case 2: *r = p; *g = v; *b = t; break;
	case 3: *r = p; *g = q; *b = v; break;
	case 4: *r = t; *g = p; *b = v; break;
	default: *r = v; *g = p; *b = q; break;
	}
}

void atri_led_render_arc(struct atri_led *led, int pct,
	uint8_t r, uint8_t g, uint8_t b, int start_ring, int direction)
{
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	int n = led->ring_count;
	if (n <= 0) return;
	if (direction == 0) direction = 1;

	/* lit rings with a fractional tail LED for smooth edge */
	int lit_tenths = pct * n * 10 / 100;
	int full = lit_tenths / 10;
	int tail = (lit_tenths % 10) * 255 / 10;

	for (int i = 0; i < n; i++) {
		int idx = ((start_ring + i * direction) % n + n) % n;
		if (i < full) {
			led->brightness[idx][RGB_R] = r;
			led->brightness[idx][RGB_G] = g;
			led->brightness[idx][RGB_B] = b;
		} else if (i == full && tail > 0) {
			led->brightness[idx][RGB_R] = (uint8_t)(r * tail / 255);
			led->brightness[idx][RGB_G] = (uint8_t)(g * tail / 255);
			led->brightness[idx][RGB_B] = (uint8_t)(b * tail / 255);
		} else {
			led->brightness[idx][RGB_R] = 0;
			led->brightness[idx][RGB_G] = 0;
			led->brightness[idx][RGB_B] = 0;
		}
	}
}
