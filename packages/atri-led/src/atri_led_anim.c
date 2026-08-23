#include "atri_led_anim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>

#define N_RINGS ATRI_LED_MAX_RINGS

static volatile int anim_running = 1;

static void animation_sighandler(int sig)
{
	(void)sig;
	anim_running = 0;
}

void atri_led_anim_stop(void)
{
	anim_running = 0;
}

int atri_led_anim_running(void)
{
	return anim_running;
}

static int parse_single_color_line(const char *line, struct animation_frame *frame, int ring_count)
{
	int r, g, b;
	if (sscanf(line, " %d,%d,%d ", &r, &g, &b) == 3) {
		for (int i = 0; i < ring_count && i < ATRI_LED_MAX_RINGS; i++) {
			frame->rgb[i][RGB_R] = (uint8_t)r;
			frame->rgb[i][RGB_G] = (uint8_t)g;
			frame->rgb[i][RGB_B] = (uint8_t)b;
		}
		return 0;
	}
	return -1;
}

static int parse_per_ring_line(const char *line, struct animation_frame *frame, int ring_count)
{
	char buf[1024];
	memcpy(buf, line, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	for (int i = 0; i < ring_count && i < ATRI_LED_MAX_RINGS; i++) {
		char *token = i == 0 ? strtok(buf, " ") : strtok(NULL, " ");
		if (!token) break;
		int r, g, b;
		if (sscanf(token, "%d,%d,%d", &r, &g, &b) == 3) {
			frame->rgb[i][RGB_R] = (uint8_t)r;
			frame->rgb[i][RGB_G] = (uint8_t)g;
			frame->rgb[i][RGB_B] = (uint8_t)b;
		}
	}
	return 0;
}

static int parse_hex_color(const char *s, uint8_t *r, uint8_t *g, uint8_t *b)
{
	unsigned int ri, gi, bi;
	if (s[0] == '\0') return -1;
	if (sscanf(s, "%02x%02x%02x", &ri, &gi, &bi) == 3) {
		*r = (uint8_t)ri; *g = (uint8_t)gi; *b = (uint8_t)bi;
		return 0;
	}
	return -1;
}

static int parse_led_line(const char *line, struct animation_frame *frame, int *loop_flag)
{
	char buf[4096];
	memcpy(buf, line, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	char *trimmed = buf;
	while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

	if (*trimmed == '\0' || *trimmed == '#') return -2;

	if (strcmp(trimmed, "loop") == 0) {
		*loop_flag = 1;
		return -2;
	}
	if (strcmp(trimmed, "gammacorrection") == 0) return -2;
	if (strncmp(trimmed, "smooth", 6) == 0) return -2;

	int n = 0;
	char *token = strtok(trimmed, " \t");
	while (token && n < ATRI_LED_MAX_RINGS) {
		if (parse_hex_color(token,
			&frame->rgb[n][RGB_R],
			&frame->rgb[n][RGB_G],
			&frame->rgb[n][RGB_B]) == 0) {
			n++;
		} else if (n > 0) {
			unsigned int d;
			if (sscanf(token, "%u", &d) == 1) {
				frame->duration_ms = (int)d;
			}
			break;
		}
		token = strtok(NULL, " \t");
	}

	return (n > 0) ? 0 : -1;
}

int atri_led_load_animation(const char *path, struct animation *anim)
{
	FILE *f = fopen(path, "r");
	if (!f) return -1;

	memset(anim, 0, sizeof(*anim));

	int is_led = (strstr(path, ".led") != NULL);

	if (is_led) {
		const char *basename = strrchr(path, '/');
		if (basename) basename++; else basename = path;
		strncpy(anim->name, basename, sizeof(anim->name) - 1);
		char *dot = strchr(anim->name, '.');
		if (dot) *dot = '\0';

		int loop_flag = 0;
		int capacity = 256;
		anim->frames = calloc(capacity, sizeof(struct animation_frame));
		if (!anim->frames) { fclose(f); return -1; }

		char line[4096];
		int fi = 0;
		while (fgets(line, sizeof(line), f)) {
			struct animation_frame tmp = { .duration_ms = 100 };
			int ret = parse_led_line(line, &tmp, &loop_flag);
			if (ret == 0) {
				if (fi >= capacity) {
					capacity *= 2;
					struct animation_frame *nf = realloc(anim->frames,
						capacity * sizeof(struct animation_frame));
					if (!nf) break;
					anim->frames = nf;
				}
				anim->frames[fi] = tmp;
				fi++;
			}
		}
		anim->frame_count = fi;
		fclose(f);

		if (loop_flag && anim->frame_count > 0) {
			struct animation_frame *nf = realloc(anim->frames,
				(anim->frame_count + 1) * sizeof(struct animation_frame));
			if (nf) {
				anim->frames = nf;
				anim->frames[anim->frame_count] = anim->frames[0];
				anim->frames[anim->frame_count].duration_ms = -1;
				anim->frame_count++;
			}
		}
		return 0;
	}

	if (fscanf(f, " %63s ", anim->name) != 1) {
		fclose(f);
		return -1;
	}

	int frame_count = 0;
	if (fscanf(f, " %d ", &frame_count) != 1) {
		frame_count = 1;
	}

	int duration_ms = 200;
	if (fscanf(f, " %d ", &duration_ms) != 1)
		duration_ms = 200;	/* missing duration: keep default */

	anim->frame_count = frame_count > 0 ? frame_count : 1;
	anim->frames = calloc(anim->frame_count, sizeof(struct animation_frame));
	if (!anim->frames) { fclose(f); return -1; }

	char line[4096];
	int fi = 0;
	while (fgets(line, sizeof(line), f) && fi < anim->frame_count) {
		char *nl = strchr(line, '\n');
		if (nl) *nl = '\0';
		char *trimmed = line;
		while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
		if (*trimmed == '\0' || *trimmed == '#') continue;

		anim->frames[fi].duration_ms = duration_ms;

		if (strchr(trimmed, ' ') && strchr(trimmed, ',')) {
			parse_per_ring_line(trimmed, &anim->frames[fi], ATRI_LED_MAX_RINGS);
		} else {
			parse_single_color_line(trimmed, &anim->frames[fi], ATRI_LED_MAX_RINGS);
		}
		fi++;
	}

	anim->frame_count = fi;
	fclose(f);
	return 0;
}

int atri_led_play_animation(struct atri_led *led, struct animation *anim, int loop)
{
	anim_running = 1;
	signal(SIGINT, animation_sighandler);
	signal(SIGTERM, animation_sighandler);

	struct timespec ts;

	do {
		for (int f = 0; f < anim->frame_count && anim_running; f++) {
			for (int r = 0; r < led->ring_count; r++) {
				led->brightness[r][RGB_R] = anim->frames[f].rgb[r][RGB_R];
				led->brightness[r][RGB_G] = anim->frames[f].rgb[r][RGB_G];
				led->brightness[r][RGB_B] = anim->frames[f].rgb[r][RGB_B];
			}
			atri_led_apply(led);
			if (anim->frames[f].duration_ms > 0) {
				ts.tv_sec = anim->frames[f].duration_ms / 1000;
				ts.tv_nsec = (anim->frames[f].duration_ms % 1000) * 1000000L;
				nanosleep(&ts, NULL);
			}
		}
	} while (loop && anim_running);

	return 0;
}

void atri_led_crossfade(struct atri_led *led,
	struct animation_frame *from, struct animation_frame *to,
	int steps, int delay_ms)
{
	for (int s = 0; s <= steps && anim_running; s++) {
		for (int r = 0; r < led->ring_count; r++) {
			uint8_t rf = from->rgb[r][RGB_R], rt = to->rgb[r][RGB_R];
			uint8_t gf = from->rgb[r][RGB_G], gt = to->rgb[r][RGB_G];
			uint8_t bf = from->rgb[r][RGB_B], bt = to->rgb[r][RGB_B];
			led->brightness[r][RGB_R] = rf + (rt - rf) * s / steps;
			led->brightness[r][RGB_G] = gf + (gt - gf) * s / steps;
			led->brightness[r][RGB_B] = bf + (bt - bf) * s / steps;
		}
		atri_led_apply(led);
		usleep(delay_ms * 1000);
	}
}

void atri_led_anim_blur(struct animation_frame *frames, int n, int radius)
{
	if (radius < 1) return;
	for (int f = 0; f < n; f++) {
		uint8_t tmp[N_RINGS][3];
		for (int ring = 0; ring < N_RINGS; ring++) {
			int r = 0, g = 0, b = 0, total = 0;
			for (int dx = -radius; dx <= radius; dx++) {
				int nr = ring + dx;
				if (nr < 0 || nr >= N_RINGS) continue;
				int w = radius + 1 - (dx < 0 ? -dx : dx);
				r += frames[f].rgb[nr][RGB_R] * w;
				g += frames[f].rgb[nr][RGB_G] * w;
				b += frames[f].rgb[nr][RGB_B] * w;
				total += w;
			}
			tmp[ring][RGB_R] = r / total;
			tmp[ring][RGB_G] = g / total;
			tmp[ring][RGB_B] = b / total;
		}
		for (int ring = 0; ring < N_RINGS; ring++) {
			frames[f].rgb[ring][RGB_R] = tmp[ring][RGB_R];
			frames[f].rgb[ring][RGB_G] = tmp[ring][RGB_G];
			frames[f].rgb[ring][RGB_B] = tmp[ring][RGB_B];
		}
	}
}

static int parabol(int t, int period)
{
	int x = t % period;
	if (x > period / 2) x = period - x;
	return 4 * x * (period / 2 - x) * 255 / ((period / 2) * (period / 2));
}

void atri_led_fill_wave(struct animation_frame *frames, int n, int ms,
	int nwaves, uint8_t r1, uint8_t g1, uint8_t b1,
	uint8_t r2, uint8_t g2, uint8_t b2)
{
	if (nwaves < 1) nwaves = 1;
	for (int f = 0; f < n; f++) {
		frames[f].duration_ms = ms;
		for (int ring = 0; ring < N_RINGS; ring++) {
			/* one parabolic period per (N_RINGS/nwaves) LEDs:
			 * phase step = 2*nwaves per ring */
			int phase = (ring * 2 * nwaves + f * 4) % (N_RINGS * 2);
			int frac = parabol(phase, N_RINGS * 2);
			int inv = 255 - frac;
			frames[f].rgb[ring][RGB_R] = (r1 * inv + r2 * frac) / 255;
			frames[f].rgb[ring][RGB_G] = (g1 * inv + g2 * frac) / 255;
			frames[f].rgb[ring][RGB_B] = (b1 * inv + b2 * frac) / 255;
		}
	}
}

void atri_led_fill_wave_slow(struct animation_frame *frames, int n, int ms,
	int nwaves, uint8_t r1, uint8_t g1, uint8_t b1,
	uint8_t r2, uint8_t g2, uint8_t b2)
{
	if (nwaves < 1) nwaves = 1;
	for (int f = 0; f < n; f++) {
		frames[f].duration_ms = ms;
		for (int ring = 0; ring < N_RINGS; ring++) {
			int phase = (ring * 2 * nwaves + f) % (N_RINGS * 2);
			int frac = parabol(phase, N_RINGS * 2);
			int inv = 255 - frac;
			frames[f].rgb[ring][RGB_R] = (r1 * inv + r2 * frac) / 255;
			frames[f].rgb[ring][RGB_G] = (g1 * inv + g2 * frac) / 255;
			frames[f].rgb[ring][RGB_B] = (b1 * inv + b2 * frac) / 255;
		}
	}
}

void atri_led_fill_breathe(struct animation_frame *frames, int n, int ms,
	uint8_t r, uint8_t g, uint8_t b)
{
	int half = n / 2;
	if (half < 1) half = 1;

	for (int f = 0; f < n; f++) {
		frames[f].duration_ms = ms;
		int frac;
		if (f < half)
			frac = parabol(f * 2, half * 2);
		else
			frac = parabol((n - f) * 2, half * 2);

		for (int ring = 0; ring < N_RINGS; ring++) {
			frames[f].rgb[ring][RGB_R] = r * frac / 255;
			frames[f].rgb[ring][RGB_G] = g * frac / 255;
			frames[f].rgb[ring][RGB_B] = b * frac / 255;
		}
	}
}

void atri_led_fill_heart(struct animation_frame *frames, int n, int ms,
	uint8_t r, uint8_t g, uint8_t b,
	uint8_t rp, uint8_t gp, uint8_t bp)
{
	int pump = n / 10;
	if (pump < 2) pump = 2;
	int rest = n - pump * 2;

	for (int f = 0; f < n; f++) {
		frames[f].duration_ms = ms;
		if (f < pump) {
			int frac = parabol(f * 4, pump * 4);
			for (int ring = 0; ring < N_RINGS; ring++) {
				frames[f].rgb[ring][RGB_R] = r + (rp - r) * frac / 255;
				frames[f].rgb[ring][RGB_G] = g + (gp - g) * frac / 255;
				frames[f].rgb[ring][RGB_B] = b + (bp - b) * frac / 255;
			}
		} else if (f < pump * 2) {
			int t = f - pump;
			int frac = parabol(t * 4, pump * 4);
			int level = 255 - frac;
			for (int ring = 0; ring < N_RINGS; ring++) {
				frames[f].rgb[ring][RGB_R] = r + (rp - r) * level / 255;
				frames[f].rgb[ring][RGB_G] = g + (gp - g) * level / 255;
				frames[f].rgb[ring][RGB_B] = b + (bp - b) * level / 255;
			}
		} else {
			int t = f - pump * 2;
			int frac = (rest - t) * 255 / rest;
			int level = frac * frac / 255;
			for (int ring = 0; ring < N_RINGS; ring++) {
				frames[f].rgb[ring][RGB_R] = rp * level / 255;
				frames[f].rgb[ring][RGB_G] = gp * level / 255;
				frames[f].rgb[ring][RGB_B] = bp * level / 255;
			}
		}
	}
}

void atri_led_fill_sparkle(struct animation_frame *frames, int n, int ms,
	uint8_t r, uint8_t g, uint8_t b,
	uint8_t rp, uint8_t gp, uint8_t bp)
{
	int fade = n / 6;
	if (fade < 4) fade = 4;
	int hold = n - fade * 2;
	if (hold < 0) hold = 0;

	for (int f = 0; f < n; f++) {
		frames[f].duration_ms = ms;
		for (int ring = 0; ring < N_RINGS; ring++) {
			int seed = (ring * 137 + f * 53) % 100;
			if (seed < 8) {
				float bg;
				if (f < fade)
					bg = parabol(f, fade);
				else if (f < fade + hold)
					bg = 255;
				else
					bg = parabol(fade - (f - fade - hold), fade);
				frames[f].rgb[ring][RGB_R] = rp * bg / 255;
				frames[f].rgb[ring][RGB_G] = gp * bg / 255;
				frames[f].rgb[ring][RGB_B] = bp * bg / 255;
			} else {
				int faint = (seed % 20) * 2;
				frames[f].rgb[ring][RGB_R] = r * faint / 255;
				frames[f].rgb[ring][RGB_G] = g * faint / 255;
				frames[f].rgb[ring][RGB_B] = b * faint / 255;
			}
		}
	}
}

void atri_led_fill_fire(struct animation_frame *frames, int n, int ms,
	uint8_t r1, uint8_t g1, uint8_t b1,
	uint8_t r2, uint8_t g2, uint8_t b2)
{
	for (int f = 0; f < n; f++) {
		frames[f].duration_ms = ms;
		for (int ring = 0; ring < N_RINGS; ring++) {
			int flicker = (ring * 31 + f * 17) % 40;
			int heat = N_RINGS - ring;
			int val = heat * 20 + flicker * 3;
			if (val > 255) val = 255;
			int blend = ring * 8 - f * 2;
			if (blend < 0) blend = 0;
			if (blend > 255) blend = 255;
			int inv = 255 - blend;
			frames[f].rgb[ring][RGB_R] = (r1 * inv + r2 * blend) / 255;
			int gr = (g1 * inv + g2 * blend) / 255;
			frames[f].rgb[ring][RGB_G] = gr * val / 255;
			int bl = (b1 * inv + b2 * blend) / 255;
			frames[f].rgb[ring][RGB_B] = bl * val / 255;
		}
	}
}

void atri_led_fill_morph(struct animation_frame *frames, int n, int ms,
	uint8_t r1, uint8_t g1, uint8_t b1,
	uint8_t r2, uint8_t g2, uint8_t b2,
	uint8_t r3, uint8_t g3, uint8_t b3)
{
	int third = n / 3;
	if (third < 1) third = 1;

	for (int f = 0; f < n; f++) {
		frames[f].duration_ms = ms;
		int frac, inv;
		uint8_t r0, g0, b0, rt, gt, bt;

		if (f < third) {
			frac = f * 255 / third;
			r0 = r1; g0 = g1; b0 = b1;
			rt = r2; gt = g2; bt = b2;
		} else if (f < third * 2) {
			frac = (f - third) * 255 / third;
			r0 = r2; g0 = g2; b0 = b2;
			rt = r3; gt = g3; bt = b3;
		} else {
			frac = (f - third * 2) * 255 / (n - third * 2);
			r0 = r3; g0 = g3; b0 = b3;
			rt = r1; gt = g1; bt = b1;
		}

		inv = 255 - frac;
		for (int ring = 0; ring < N_RINGS; ring++) {
			int offset = ring * 5;
			int p = frac + offset;
			if (p > 255) p = 255;
			int q = inv - offset;
			if (q < 0) q = 0;
			frames[f].rgb[ring][RGB_R] = (r0 * q + rt * p) / 255;
			frames[f].rgb[ring][RGB_G] = (g0 * q + gt * p) / 255;
			frames[f].rgb[ring][RGB_B] = (b0 * q + bt * p) / 255;
		}
	}
}

void atri_led_fill_meteor(struct animation_frame *frames, int n, int ms,
	uint8_t r, uint8_t g, uint8_t b,
	uint8_t rt, uint8_t gt, uint8_t bt)
{
	/* short decaying tail behind the head (was inverted: 3/4-ring tail) */
	const int tail_len = 6;
	for (int f = 0; f < n; f++) {
		frames[f].duration_ms = ms;
		int head = f * N_RINGS / n;
		for (int ring = 0; ring < N_RINGS; ring++) {
			int dist = (head - ring + N_RINGS) % N_RINGS;
			int bright = 0;
			if (dist < tail_len)
				bright = (tail_len - dist) * 255 / tail_len;
			int blend = ring * 255 / N_RINGS;
			int inv = 255 - blend;
			frames[f].rgb[ring][RGB_R] = (r * inv + rt * blend) * bright / 255 / 255;
			frames[f].rgb[ring][RGB_G] = (g * inv + gt * blend) * bright / 255 / 255;
			frames[f].rgb[ring][RGB_B] = (b * inv + bt * blend) * bright / 255 / 255;
		}
	}
}

void atri_led_fill_cycle(struct animation_frame *frames, int n, int ms,
	int speed, uint8_t bright)
{
	for (int f = 0; f < n; f++) {
		frames[f].duration_ms = ms;
		for (int ring = 0; ring < N_RINGS; ring++) {
			int hue = (ring * 360 / N_RINGS + f * speed) % 360;
			uint8_t rv, gv, bv;
			int sector = hue / 60;
			int phase = hue % 60;
			switch (sector) {
			case 0: rv = bright; gv = phase * bright / 60; bv = 0; break;
			case 1: rv = (60 - phase) * bright / 60; gv = bright; bv = 0; break;
			case 2: rv = 0; gv = bright; bv = phase * bright / 60; break;
			case 3: rv = 0; gv = (60 - phase) * bright / 60; bv = bright; break;
			case 4: rv = phase * bright / 60; gv = 0; bv = bright; break;
			default: rv = bright; gv = 0; bv = (60 - phase) * bright / 60; break;
			}
			frames[f].rgb[ring][RGB_R] = rv;
			frames[f].rgb[ring][RGB_G] = gv;
			frames[f].rgb[ring][RGB_B] = bv;
		}
	}
}

void atri_led_fill_storm(struct animation_frame *frames, int n, int ms,
	uint8_t r, uint8_t g, uint8_t b,
	uint8_t rp, uint8_t gp, uint8_t bp)
{
	int half = n / 2;
	if (half < 1) half = 1;
	for (int f = 0; f < n; f++) {
		frames[f].duration_ms = ms;
		int fade = f % half;
		int flash = fade < 3;
		int dim = flash ? 255 : (half - fade) * 4;
		if (dim < 0) dim = 0;

		for (int ring = 0; ring < N_RINGS; ring++) {
			int is_center = ring > N_RINGS / 3 && ring < N_RINGS * 2 / 3;
			if (flash && is_center) {
				int rnd = (ring * f * 7) % 30;
				frames[f].rgb[ring][RGB_R] = rp;
				frames[f].rgb[ring][RGB_G] = gp;
				frames[f].rgb[ring][RGB_B] = bp;
				if (rnd < 10) {
					frames[f].rgb[ring][RGB_R] = 220;
					frames[f].rgb[ring][RGB_G] = 220;
					frames[f].rgb[ring][RGB_B] = 255;
				}
			} else {
				frames[f].rgb[ring][RGB_R] = r * dim / 255;
				frames[f].rgb[ring][RGB_G] = g * dim / 255;
				frames[f].rgb[ring][RGB_B] = b * dim / 255;
			}
		}
	}
}

static void fill_solid(struct animation_frame *frames, int n, int ms,
	uint8_t r, uint8_t g, uint8_t b)
{
	for (int f = 0; f < n; f++) {
		frames[f].duration_ms = ms;
		for (int ring = 0; ring < N_RINGS; ring++) {
			frames[f].rgb[ring][RGB_R] = r;
			frames[f].rgb[ring][RGB_G] = g;
			frames[f].rgb[ring][RGB_B] = b;
		}
	}
}

static void fill_chase(struct animation_frame *frames, int n, int ms,
	uint8_t r, uint8_t g, uint8_t b)
{
	for (int f = 0; f < n; f++) {
		frames[f].duration_ms = ms;
		for (int ring = 0; ring < N_RINGS; ring++) {
			frames[f].rgb[ring][RGB_R] = 0;
			frames[f].rgb[ring][RGB_G] = 0;
			frames[f].rgb[ring][RGB_B] = 0;
		}
		int on = f % N_RINGS;
		frames[f].rgb[on][RGB_R] = r;
		frames[f].rgb[on][RGB_G] = g;
		frames[f].rgb[on][RGB_B] = b;
	}
}

static void fill_colors(struct animation_frame *frames, int n, int ms)
{
	uint8_t colors[][3] = {
		{255, 0, 0},
		{0, 255, 0},
		{0, 0, 255},
		{255, 255, 255},
	};
	int ncolors = sizeof(colors) / sizeof(colors[0]);
	for (int f = 0; f < n; f++) {
		frames[f].duration_ms = ms;
		int c = f % ncolors;
		for (int ring = 0; ring < N_RINGS; ring++) {
			frames[f].rgb[ring][RGB_R] = colors[c][0];
			frames[f].rgb[ring][RGB_G] = colors[c][1];
			frames[f].rgb[ring][RGB_B] = colors[c][2];
		}
	}
}

static void fill_rainbow(struct animation_frame *frames, int n, int ms)
{
	for (int f = 0; f < n; f++) {
		frames[f].duration_ms = ms;
		for (int ring = 0; ring < N_RINGS; ring++) {
			int hue = (f * 5 + ring * 15) % 360;
			uint8_t r, g, b;
			int sector = hue / 60;
			int phase = hue % 60;
			switch (sector) {
			case 0: r = 255; g = phase * 255 / 60; b = 0; break;
			case 1: r = (60 - phase) * 255 / 60; g = 255; b = 0; break;
			case 2: r = 0; g = 255; b = phase * 255 / 60; break;
			case 3: r = 0; g = (60 - phase) * 255 / 60; b = 255; break;
			case 4: r = phase * 255 / 60; g = 0; b = 255; break;
			default: r = 255; g = 0; b = (60 - phase) * 255 / 60; break;
			}
			frames[f].rgb[ring][RGB_R] = r;
			frames[f].rgb[ring][RGB_G] = g;
			frames[f].rgb[ring][RGB_B] = b;
		}
	}
}

/* Larson scanner: dot with fading trail bouncing around the ring */
void atri_led_fill_scan(struct animation_frame *frames, int n, int ms,
	uint8_t r, uint8_t g, uint8_t b)
{
	if (n < 2) n = 2;
	const int trail = 4;
	/* path: 0..N-1..0 (bounce), length 2*N-2 */
	int path = 2 * N_RINGS - 2;
	for (int f = 0; f < n; f++) {
		frames[f].duration_ms = ms;
		int pos = f % path;
		if (pos >= N_RINGS) pos = path - pos;
		for (int ring = 0; ring < N_RINGS; ring++) {
			int d = pos - ring;
			if (d < 0) d = -d;
			int bright = (d < trail) ? (trail - d) * 255 / trail : 0;
			frames[f].rgb[ring][RGB_R] = r * bright / 255;
			frames[f].rgb[ring][RGB_G] = g * bright / 255;
			frames[f].rgb[ring][RGB_B] = b * bright / 255;
		}
	}
}

/* twinkle: each LED randomly fades in/out (deterministic LCG per ring) */
void atri_led_fill_twinkle(struct animation_frame *frames, int n, int ms,
	uint8_t r, uint8_t g, uint8_t b)
{
	for (int f = 0; f < n; f++) {
		frames[f].duration_ms = ms;
		for (int ring = 0; ring < N_RINGS; ring++) {
			/* pseudo-random smooth phase per ring */
			unsigned int seed = ring * 1103515245u + 12345u;
			int period = 12 + (seed >> 16) % 9;      /* 12..20 frames */
			int phase = (f + (seed >> 8) % period) % period;
			/* triangular envelope with idle gaps */
			int env = 0;
			if (phase < period / 3) {
				int t = phase * 3 * 255 / period;
				env = t < 128 ? t * 2 : (255 - t) * 2;
			}
			frames[f].rgb[ring][RGB_R] = r * env / 255;
			frames[f].rgb[ring][RGB_G] = g * env / 255;
			frames[f].rgb[ring][RGB_B] = b * env / 255;
		}
	}
}

/* rainbow wave: hue rotates around the ring (spatial gradient + motion) */
void atri_led_fill_rainbow_wave(struct animation_frame *frames, int n, int ms,
	int hue_step, uint8_t bright)
{
	for (int f = 0; f < n; f++) {
		frames[f].duration_ms = ms;
		for (int ring = 0; ring < N_RINGS; ring++) {
			int hue = (ring * 360 / N_RINGS + f * hue_step) % 360;
			uint8_t rv, gv, bv;
			atri_led_hsv_to_rgb(hue, 255, bright, &rv, &gv, &bv);
			frames[f].rgb[ring][RGB_R] = rv;
			frames[f].rgb[ring][RGB_G] = gv;
			frames[f].rgb[ring][RGB_B] = bv;
		}
	}
}

/* sinusoidal breathe — smoother than the parabolic one */
void atri_led_fill_breathe_sine(struct animation_frame *frames, int n, int ms,
	uint8_t r, uint8_t g, uint8_t b)
{
	for (int f = 0; f < n; f++) {
		frames[f].duration_ms = ms;
		double phase = 2.0 * M_PI * f / n;
		/* shifted sine 0..1, ease dwell at the bottom */
		double s = (1.0 - cos(phase)) / 2.0;
		int frac = (int)(s * s * 255.0 + 0.5);
		for (int ring = 0; ring < N_RINGS; ring++) {
			frames[f].rgb[ring][RGB_R] = r * frac / 255;
			frames[f].rgb[ring][RGB_G] = g * frac / 255;
			frames[f].rgb[ring][RGB_B] = b * frac / 255;
		}
	}
}

/* rotating two-color gradient around the ring */
void atri_led_fill_gradient(struct animation_frame *frames, int n, int ms,
	uint8_t r1, uint8_t g1, uint8_t b1,
	uint8_t r2, uint8_t g2, uint8_t b2)
{
	for (int f = 0; f < n; f++) {
		frames[f].duration_ms = ms;
		for (int ring = 0; ring < N_RINGS; ring++) {
			/* triangle wave position along the ring, rotated by frame */
			int p = (ring + f * N_RINGS / n) % N_RINGS;
			int t = p <= N_RINGS / 2 ? p * 255 / (N_RINGS / 2)
				: (N_RINGS - p) * 255 / (N_RINGS / 2);
			int inv = 255 - t;
			frames[f].rgb[ring][RGB_R] = (r1 * inv + r2 * t) / 255;
			frames[f].rgb[ring][RGB_G] = (g1 * inv + g2 * t) / 255;
			frames[f].rgb[ring][RGB_B] = (b1 * inv + b2 * t) / 255;
		}
	}
}

#define FRAME_MS 50

static struct animation_frame red_frames[1];
static struct animation_frame green_frames[1];
static struct animation_frame blue_frames[1];
static struct animation_frame white_frames[1];
static struct animation_frame off_frames[1];
static struct animation_frame chase_frames[24];
static struct animation_frame colors_frames[4];
static struct animation_frame rainbow_frames[72];

static void init_extra_animations(void);

void atri_led_anim_set_blend_tables(const uint8_t *fwd, const uint8_t *inv);

void atri_led_init_animations(void)
{
	atri_led_anim_set_blend_tables(atri_led_get_gamma_lut(),
				       atri_led_get_gamma_inv());

	fill_solid(red_frames, 1, FRAME_MS, 255, 0, 0);
	fill_solid(green_frames, 1, FRAME_MS, 0, 255, 0);
	fill_solid(blue_frames, 1, FRAME_MS, 0, 0, 255);
	fill_solid(white_frames, 1, FRAME_MS, 255, 255, 255);
	fill_solid(off_frames, 1, FRAME_MS, 0, 0, 0);
	fill_chase(chase_frames, 24, 60, 255, 0, 0);
	fill_colors(colors_frames, 4, 500);
	fill_rainbow(rainbow_frames, 72, 60);
	init_extra_animations();
}

/*
 * Builtin animation registry — framework discovers animations from this
 * table; adding a new one = frames array + struct + one entry here.
 */
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
extern struct animation anim_scan;
extern struct animation anim_twinkle;
extern struct animation anim_rainbow_wave;
extern struct animation anim_breathe;
extern struct animation anim_gradient;

struct animation *atri_led_builtin_animations[] = {
	&anim_red,
	&anim_green,
	&anim_blue,
	&anim_white,
	&anim_off,
	&anim_chase,
	&anim_colors,
	&anim_rainbow,
	&anim_happy,
	&anim_focused,
	&anim_calming,
	&anim_love,
	&anim_night,
	&anim_excited,
	&anim_scan,
	&anim_twinkle,
	&anim_rainbow_wave,
	&anim_breathe,
	&anim_gradient,
	NULL,
};

struct animation *atri_led_find_builtin(const char *name)
{
	for (int i = 0; atri_led_builtin_animations[i]; i++) {
		if (strcmp(name, atri_led_builtin_animations[i]->name) == 0)
			return atri_led_builtin_animations[i];
	}
	return NULL;
}

int atri_led_builtin_count(void)
{
	int n = 0;
	while (atri_led_builtin_animations[n]) n++;
	return n;
}

struct animation *atri_led_builtin_get(int idx)
{
	if (idx < 0 || idx >= atri_led_builtin_count()) return NULL;
	return atri_led_builtin_animations[idx];
}

struct animation anim_red = {
	.name = "red",
	.frame_count = 1,
	.frames = red_frames,
};

struct animation anim_green = {
	.name = "green",
	.frame_count = 1,
	.frames = green_frames,
};

struct animation anim_blue = {
	.name = "blue",
	.frame_count = 1,
	.frames = blue_frames,
};

struct animation anim_white = {
	.name = "white",
	.frame_count = 1,
	.frames = white_frames,
};

struct animation anim_off = {
	.name = "off",
	.frame_count = 1,
	.frames = off_frames,
};

struct animation anim_chase = {
	.name = "chase",
	.frame_count = 24,
	.frames = chase_frames,
};

struct animation anim_colors = {
	.name = "colors",
	.frame_count = 4,
	.frames = colors_frames,
};

struct animation anim_rainbow = {
	.name = "rainbow",
	.frame_count = 72,
	.frames = rainbow_frames,
};

static struct animation_frame happy_frames[60];
static struct animation_frame focused_frames[60];
static struct animation_frame calming_frames[40];
static struct animation_frame love_frames[40];
static struct animation_frame night_frames[60];
static struct animation_frame excited_frames[60];
static struct animation_frame scan_frames[46];
static struct animation_frame twinkle_frames[40];
static struct animation_frame rainbow_wave_frames[72];
static struct animation_frame breathe_frames[48];
static struct animation_frame gradient_frames[48];

static void init_extra_animations(void)
{
	atri_led_fill_wave(happy_frames, 60, 4000, 1, 255, 100, 50, 100, 255, 50);
	atri_led_fill_breathe(focused_frames, 60, 4000, 80, 60, 180);
	atri_led_fill_wave_slow(calming_frames, 40, 6000, 1, 50, 100, 255, 100, 50, 200);
	atri_led_fill_heart(love_frames, 40, 3000, 255, 50, 100, 255, 50, 100);
	atri_led_fill_sparkle(night_frames, 60, 5000, 20, 20, 80, 200, 220, 255);
	atri_led_fill_fire(excited_frames, 60, 2500, 255, 100, 0, 255, 200, 0);
	atri_led_fill_scan(scan_frames, 46, 60, 80, 120, 255);
	atri_led_fill_twinkle(twinkle_frames, 40, 90, 255, 220, 120);
	atri_led_fill_rainbow_wave(rainbow_wave_frames, 72, 50, 5, 200);
	atri_led_fill_breathe_sine(breathe_frames, 48, 80, 60, 120, 255);
	atri_led_fill_gradient(gradient_frames, 48, 60, 255, 60, 0, 0, 80, 255);
}

struct animation anim_happy = {
	.name = "happy",
	.frame_count = 60,
	.num_init = init_extra_animations,
	.frames = happy_frames,
};

struct animation anim_focused = {
	.name = "focused",
	.frame_count = 60,
	.num_init = init_extra_animations,
	.frames = focused_frames,
};

struct animation anim_calming = {
	.name = "calming",
	.frame_count = 40,
	.num_init = init_extra_animations,
	.frames = calming_frames,
};

struct animation anim_love = {
	.name = "love",
	.frame_count = 40,
	.num_init = init_extra_animations,
	.frames = love_frames,
};

struct animation anim_night = {
	.name = "night",
	.frame_count = 60,
	.num_init = init_extra_animations,
	.frames = night_frames,
};

struct animation anim_excited = {
	.name = "excited",
	.num_init = init_extra_animations,
	.frames = excited_frames,
	.frame_count = 60,
};

struct animation anim_scan = {
	.name = "scan",
	.frames = scan_frames,
	.frame_count = 46,
};

struct animation anim_twinkle = {
	.name = "twinkle",
	.frames = twinkle_frames,
	.frame_count = 40,
};

struct animation anim_rainbow_wave = {
	.name = "rainbow_wave",
	.frames = rainbow_wave_frames,
	.frame_count = 72,
};

struct animation anim_breathe = {
	.name = "breathe",
	.frames = breathe_frames,
	.frame_count = 48,
};

struct animation anim_gradient = {
	.name = "gradient",
	.frames = gradient_frames,
	.frame_count = 48,
};

/* ---- smooth playback: timeline + linear interpolation ---- */

int atri_led_timeline_build(const struct animation *a, int loop,
	struct anim_timeline *tl)
{
	int i;

	memset(tl, 0, sizeof(*tl));
	if (!a || a->frame_count <= 0 || !a->frames)
		return -1;

	tl->n = a->frame_count;
	tl->loop = loop;
	tl->cum_ms = calloc(tl->n + 1, sizeof(long));
	if (!tl->cum_ms)
		return -1;

	for (i = 0; i < tl->n; i++) {
		long d = a->frames[i].duration_ms;
		if (d <= 0)
			d = 20;	/* degenerate duration guard */
		tl->cum_ms[i + 1] = tl->cum_ms[i] + d;
	}
	tl->total_ms = tl->cum_ms[tl->n];
	return 0;
}

void atri_led_timeline_free(struct anim_timeline *tl)
{
	if (!tl) return;
	free(tl->cum_ms);
	memset(tl, 0, sizeof(*tl));
}

/*
 * Perceptual blending: interpolate in emitted-light space (through the
 * gamma LUT and its inverse), so fades have no dark mid-point dip.
 * Falls back to plain lerp until atri_led_anim_set_blend_tables().
 */
static const uint8_t *blend_fwd;
static const uint8_t *blend_inv;

void atri_led_anim_set_blend_tables(const uint8_t *fwd, const uint8_t *inv)
{
	blend_fwd = fwd;
	blend_inv = inv;
}

static inline uint8_t chan_blend(int f, int t, int frac)
{
	if (frac <= 0) return (uint8_t)f;	/* exact endpoints */
	if (frac >= 255) return (uint8_t)t;
	if (blend_fwd && blend_inv) {
		int lin = (int)blend_fwd[f] +
			(((int)blend_fwd[t] - (int)blend_fwd[f]) * frac) / 255;
		return blend_inv[lin];
	}
	return (uint8_t)(f + (t - f) * frac / 255);
}

static void frame_lerp(const struct animation_frame *from,
	const struct animation_frame *to, int frac /*0..255*/,
	struct animation_frame *out, int ring_count)
{
	int r, c;

	for (r = 0; r < ring_count && r < ATRI_LED_MAX_RINGS; r++)
		for (c = 0; c < 3; c++)
			out->rgb[r][c] =
				chan_blend(from->rgb[r][c], to->rgb[r][c], frac);
	out->duration_ms = from->duration_ms;
}

int atri_led_timeline_sample(const struct anim_timeline *tl,
	const struct animation *a, long t_ms, struct animation_frame *out)
{
	long pos;
	int i;

	if (!tl || !a || tl->n <= 0)
		return 0;

	if (tl->loop) {
		pos = t_ms % tl->total_ms;
		if (pos < 0) pos += tl->total_ms;
	} else {
		if (t_ms >= tl->total_ms) {
			*out = a->frames[tl->n - 1];	/* hold last */
			return 0;
		}
		pos = t_ms;
	}

	/* frames advance monotonically within a cycle; linear scan from
	 * the start is fine at these sizes (< few hundred frames) */
	i = 0;
	while (i < tl->n - 1 && pos >= tl->cum_ms[i + 1])
		i++;
	{
		long seg = tl->cum_ms[i + 1] - tl->cum_ms[i];
		int frac = (int)(((pos - tl->cum_ms[i]) * 255) / (seg > 0 ? seg : 1));
		const struct animation_frame *next =
			&a->frames[(i + 1) % tl->n];
		frame_lerp(&a->frames[i], next, frac, out, ATRI_LED_MAX_RINGS);
	}
	return 1;
}
