#include "atri_led.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <alsa/asoundlib.h>
#include <signal.h>
#include <time.h>

#define ADC_PATH "/sys/bus/iio/devices/iio:device0/in_voltage0_raw"
#define POLL_MS 30
#define DEBOUNCE 3
#define ADC_MAX 1023
#define IDLE_TIMEOUT_MS 1500
#define FADE_STEPS 60
#define FADE_MS (POLL_MS * FADE_STEPS)

static snd_mixer_t *mixer;
static snd_mixer_elem_t *elem;
static long vol_min, vol_max;
static int running = 1;
static int prev_pct = -10;
static int stable_count = 0;
static struct atri_led led;
static int led_inited = 0;

static int last_r, last_g, last_b;

static void hsv_to_rgb(int h, int s, int v, uint8_t *r, uint8_t *g, uint8_t *b)
{
	int region, remainder, p, q, t;
	if (s == 0) {
		*r = *g = *b = v;
		return;
	}
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

static void show_color(int r, int g, int b)
{
	if (!led_inited) return;
	for (int i = 0; i < led.ring_count; i++)
		atri_led_set_ring_rgb(&led, i, r, g, b);
	atri_led_apply(&led);
}

static void set_color_from_pct(int pct)
{
	int h = (100 - pct) * 240 / 100;
	uint8_t r, g, b;
	hsv_to_rgb(h, 255, 200, &r, &g, &b);
	last_r = r; last_g = g; last_b = b;
	show_color(r, g, b);
}

static void fade_out(int step, int total)
{
	int t = step * 255 / total;
	int inv = 255 - t;
	/* smoothstep: t = t * t * (3 - 2*t), then scale to 0-200 */
	int s = inv * inv * (3 * 255 - 2 * inv) / (255 * 255);
	int r = last_r * s / 255;
	int g = last_g * s / 255;
	int b = last_b * s / 255;
	show_color(r, g, b);
}

static void volume_off(void)
{
	if (!led_inited) return;
	atri_led_off(&led);
}

static void cleanup(void)
{
	if (mixer) {
		snd_mixer_detach(mixer, "default");
		snd_mixer_close(mixer);
	}
}

static void handle_sig(int sig)
{
	(void)sig;
	running = 0;
}

static int init_mixer(const char *selem_name)
{
	snd_mixer_selem_id_t *sid;
	int ret;

	ret = snd_mixer_open(&mixer, 0);
	if (ret < 0) return ret;

	ret = snd_mixer_attach(mixer, "default");
	if (ret < 0) goto err;

	ret = snd_mixer_selem_register(mixer, NULL, NULL);
	if (ret < 0) goto err;

	ret = snd_mixer_load(mixer);
	if (ret < 0) goto err;

	snd_mixer_selem_id_alloca(&sid);
	snd_mixer_selem_id_set_index(sid, 0);
	snd_mixer_selem_id_set_name(sid, selem_name);

	elem = snd_mixer_find_selem(mixer, sid);
	if (!elem) {
		snd_mixer_elem_t *e = snd_mixer_first_elem(mixer);
		while (e) {
			if (snd_mixer_selem_is_active(e) &&
			    snd_mixer_selem_has_playback_volume(e)) {
				elem = e;
				break;
			}
			e = snd_mixer_elem_next(e);
		}
		if (!elem) { ret = -ENOENT; goto err; }
		fprintf(stderr, "using mixer: %s\n",
			snd_mixer_selem_get_name(elem));
	}

	snd_mixer_selem_get_playback_volume_range(elem, &vol_min, &vol_max);
	return 0;

err:
	snd_mixer_close(mixer);
	mixer = NULL;
	return ret;
}

static int set_volume(int pct)
{
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	long val = vol_min + (vol_max - vol_min) * pct / 100;
	return snd_mixer_selem_set_playback_volume_all(elem, val);
}

static int read_adc(void)
{
	char buf[16];
	int fd, ret;

	fd = open(ADC_PATH, O_RDONLY);
	if (fd < 0) return -1;
	ret = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (ret <= 0) return -1;
	buf[ret] = 0;
	return atoi(buf);
}

int main(int argc, char **argv)
{
	const char *selem_name = argc > 1 ? argv[1] : "PCM";
	struct timespec ts = {0, POLL_MS * 1000000};
	int idle_ms = 0;
	int fade_step = -1;
	int fade_total = FADE_STEPS;

	signal(SIGINT, handle_sig);
	signal(SIGTERM, handle_sig);
	atexit(cleanup);

	if (init_mixer(selem_name) < 0) {
		fprintf(stderr, "failed to init ALSA mixer\n");
		return 1;
	}

	if (atri_led_init(&led) == 0) {
		led_inited = 1;
		fprintf(stderr, "LED ring ready (%d rings)\n", led.ring_count);
	} else {
		fprintf(stderr, "no LED ring, volume-only mode\n");
	}

	fprintf(stderr, "atrivolume: volume range %ld-%ld\n", vol_min, vol_max);

	while (running) {
		int adc = read_adc();
		if (adc >= 0) {
			int pct = (ADC_MAX - adc) * 100 / ADC_MAX;
			if (pct != prev_pct) {
				if (stable_count < DEBOUNCE) {
					stable_count++;
				} else {
					set_volume(pct);
					set_color_from_pct(pct);
					prev_pct = pct;
					idle_ms = 0;
					fade_step = -1;
				}
			} else {
				stable_count = 0;
			}
		}
		if (fade_step >= 0) {
			fade_step++;
			if (fade_step >= fade_total) {
				volume_off();
				fade_step = -1;
				idle_ms = -1;
			} else {
				fade_out(fade_step, fade_total);
			}
		} else if (idle_ms >= 0) {
			idle_ms += POLL_MS;
			if (idle_ms >= IDLE_TIMEOUT_MS) {
				fade_step = 0;
			}
		}
		nanosleep(&ts, NULL);
	}

	volume_off();
	return 0;
}
