#ifndef _ATRI_LED_ANIM_H
#define _ATRI_LED_ANIM_H

#include "atri_led.h"
#include <time.h>

struct animation_frame {
	int duration_ms;
	uint8_t rgb[ATRI_LED_MAX_RINGS][ATRI_LED_CHANNELS_PER_RING];
};

struct animation {
	char name[64];
	int frame_count;
	struct animation_frame *frames;
	void (*num_init)(void);
};

int atri_led_load_animation(const char *path, struct animation *anim);
int atri_led_play_animation(struct atri_led *led, struct animation *anim, int loop);
void atri_led_anim_stop(void);
int atri_led_anim_running(void);
void atri_led_init_animations(void);
struct animation *atri_led_find_builtin(const char *name);
void atri_led_crossfade(struct atri_led *led,
	struct animation_frame *from, struct animation_frame *to,
	int steps, int delay_ms);

void atri_led_anim_blur(struct animation_frame *frames, int n, int radius);
void atri_led_fill_wave(struct animation_frame *frames, int n, int ms,
	int nwaves, uint8_t r1, uint8_t g1, uint8_t b1,
	uint8_t r2, uint8_t g2, uint8_t b2);
void atri_led_fill_wave_slow(struct animation_frame *frames, int n, int ms,
	int nwaves, uint8_t r1, uint8_t g1, uint8_t b1,
	uint8_t r2, uint8_t g2, uint8_t b2);
void atri_led_fill_breathe(struct animation_frame *frames, int n, int ms,
	uint8_t r, uint8_t g, uint8_t b);
void atri_led_fill_heart(struct animation_frame *frames, int n, int ms,
	uint8_t r, uint8_t g, uint8_t b,
	uint8_t rp, uint8_t gp, uint8_t bp);
void atri_led_fill_sparkle(struct animation_frame *frames, int n, int ms,
	uint8_t r, uint8_t g, uint8_t b,
	uint8_t rp, uint8_t gp, uint8_t bp);
void atri_led_fill_fire(struct animation_frame *frames, int n, int ms,
	uint8_t r1, uint8_t g1, uint8_t b1,
	uint8_t r2, uint8_t g2, uint8_t b2);
void atri_led_fill_morph(struct animation_frame *frames, int n, int ms,
	uint8_t r1, uint8_t g1, uint8_t b1,
	uint8_t r2, uint8_t g2, uint8_t b2,
	uint8_t r3, uint8_t g3, uint8_t b3);
void atri_led_fill_meteor(struct animation_frame *frames, int n, int ms,
	uint8_t r, uint8_t g, uint8_t b,
	uint8_t rt, uint8_t gt, uint8_t bt);
void atri_led_fill_cycle(struct animation_frame *frames, int n, int ms,
	int speed, uint8_t bright);
void atri_led_fill_storm(struct animation_frame *frames, int n, int ms,
	uint8_t r, uint8_t g, uint8_t b,
	uint8_t rp, uint8_t gp, uint8_t bp);

extern struct animation anim_red;
extern struct animation anim_green;
extern struct animation anim_blue;
extern struct animation anim_white;
extern struct animation anim_off;
extern struct animation anim_chase;
extern struct animation anim_colors;
extern struct animation anim_rainbow;

extern struct animation anim_happy;
extern struct animation anim_focused;
extern struct animation anim_calming;
extern struct animation anim_love;
extern struct animation anim_night;
extern struct animation anim_excited;

#endif
