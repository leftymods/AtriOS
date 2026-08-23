#ifndef _ATRI_LED_H
#define _ATRI_LED_H

#include <stdint.h>
#include <stddef.h>

#define ATRI_LED_MAX_RINGS 24
#define ATRI_LED_CHANNELS_PER_RING 3

#define RGB_R 0
#define RGB_G 1
#define RGB_B 2

struct atri_led {
	int ring_count;
	uint8_t brightness[ATRI_LED_MAX_RINGS][ATRI_LED_CHANNELS_PER_RING];
	/* master dimmer 0-255 (255 = full), applied with gamma in apply() */
	uint8_t master;
	/* gamma correction enable (1 = on, default) */
	uint8_t gamma_en;
};

int atri_led_init(struct atri_led *led);
int atri_led_ring_count(void);
int atri_led_apply(struct atri_led *led);
int atri_led_set_all_rgb(struct atri_led *led, uint8_t r, uint8_t g, uint8_t b);
int atri_led_off(struct atri_led *led);

void atri_led_set_ring_rgb(struct atri_led *led, int ring, uint8_t r, uint8_t g, uint8_t b);
void atri_led_set_master(struct atri_led *led, uint8_t master);
void atri_led_set_gamma(struct atri_led *led, int enable);
uint8_t atri_led_get_master(struct atri_led *led);

/* hue 0-359, sat/val 0-255 */
void atri_led_hsv_to_rgb(int h, int s, int v, uint8_t *r, uint8_t *g, uint8_t *b);

/* render an arc: pct 0-100 of the ring lit with color (r,g,b), rest dark.
 * start_ring is index 0 of the arc; direction 1 = cw, -1 = ccw. */
void atri_led_render_arc(struct atri_led *led, int pct,
	uint8_t r, uint8_t g, uint8_t b, int start_ring, int direction);

void atri_led_fade_to(struct atri_led *led, int ring, uint8_t r, uint8_t g, uint8_t b, int steps, int delay_ms);
void atri_led_rainbow(struct atri_led *led);
void atri_led_anim_color(struct atri_led *led, const char *name);
int atri_led_anim_to_rgb(const char *name, uint8_t *r, uint8_t *g, uint8_t *b);

#endif
