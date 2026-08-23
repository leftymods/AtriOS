/*
 * test_atri_led.c — host-side unit tests for the atri-led library.
 * Built and run by `make test`. Exit code = number of failures.
 */

#include "atri_led.h"
#include "atri_led_anim.h"
#include <stdio.h>
#include <string.h>

static struct animation_frame fr[4];
static int failures;

#define CHECK(cond, fmt, ...) \
	do { if (!(cond)) { failures++; \
		fprintf(stderr, "FAIL: " fmt "\n", ##__VA_ARGS__); } } while (0)

static void test_timeline(void)
{
	struct animation a = { .name = "t", .frames = fr, .frame_count = 4 };
	struct anim_timeline tl;
	struct animation_frame out;

	memset(fr, 0, sizeof(fr));
	fr[1].rgb[0][RGB_R] = 255;
	fr[2].rgb[0][RGB_G] = 255;
	fr[3].rgb[0][RGB_B] = 255;
	for (int i = 0; i < 4; i++) fr[i].duration_ms = 100;

	CHECK(atri_led_timeline_build(&a, 1, &tl) == 0, "timeline build");
	CHECK(tl.total_ms == 400, "total %ld != 400", tl.total_ms);

	atri_led_timeline_sample(&tl, &a, 50, &out);
	/* perceptual blend: half light = cmd ~186 (gamma 2.2), not 128 */
	CHECK(out.rgb[0][RGB_R] >= 175 && out.rgb[0][RGB_R] <= 195,
	      "midpoint lerp R=%d (expect ~186)", out.rgb[0][RGB_R]);

	atri_led_timeline_sample(&tl, &a, 300, &out);
	CHECK(out.rgb[0][RGB_B] == 255, "exact frame start B=%d",
	      out.rgb[0][RGB_B]);

	tl.loop = 0;
	int active = atri_led_timeline_sample(&tl, &a, 9999, &out);
	CHECK(active == 0 && out.rgb[0][RGB_B] == 255,
	      "one-shot end hold-last");
	atri_led_timeline_free(&tl);
	printf("ok: timeline\n");
}

static void test_blend_tables(void)
{
	struct atri_led led;
	const uint8_t *fwd, *inv;

	atri_led_init(&led);
	fwd = atri_led_get_gamma_lut();
	inv = atri_led_get_gamma_inv();

	/* perceptual midpoint of black<->white */
	int lin = (fwd[0] + fwd[255]) / 2;
	CHECK(inv[lin] >= 175 && inv[lin] <= 195,
	      "blend midpoint cmd=%d (expect ~186)", inv[lin]);

	/* roundtrip within one step */
	for (int lvl = 0; lvl <= 255; lvl += 17) {
		int back = fwd[inv[lvl]];
		CHECK(back >= lvl - 1 && back <= lvl + 1,
		      "roundtrip lvl=%d -> %d", lvl, back);
	}
	printf("ok: blend tables\n");
}

static void test_arc(void)
{
	struct atri_led led;
	int full;

	atri_led_init(&led);
	led.ring_count = 24;
	memset(led.brightness, 0xAA, sizeof(led.brightness));

	atri_led_render_arc(&led, 50, 100, 100, 100, 0, 1);
	full = 0;
	for (int i = 0; i < 24; i++)
		if (led.brightness[i][RGB_R] == 100) full++;
	CHECK(full >= 11 && full <= 13, "arc 50%% -> %d full rings", full);

	/* endpoints */
	atri_led_render_arc(&led, 0, 100, 100, 100, 0, 1);
	CHECK(led.brightness[0][RGB_R] == 0, "arc 0%% not dark");
	atri_led_render_arc(&led, 100, 100, 100, 100, 0, 1);
	CHECK(led.brightness[23][RGB_R] == 100, "arc 100%% not full");
	printf("ok: arc\n");
}

int main(void)
{
	atri_led_init_animations();
	test_timeline();
	test_blend_tables();
	test_arc();

	if (failures) {
		fprintf(stderr, "== %d FAILURE(S) ==\n", failures);
		return 1;
	}
	printf("ALL TESTS PASSED\n");
	return 0;
}
