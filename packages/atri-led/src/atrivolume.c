#include "atri_led.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <alsa/asoundlib.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

#define ADC_PATH "/sys/bus/iio/devices/iio:device0/in_voltage0_raw"
#define POLL_MS 30
#define ADC_MAX 1023
#define IDLE_TIMEOUT_MS 1500
#define FADE_STEPS 50
#define HYSTERESIS 1		/* min % change to apply */

static snd_mixer_t *mixer;
static snd_mixer_elem_t *elem;
static long vol_min, vol_max;
static int running = 1;
static int prev_pct = -1;
static struct atri_led led;
static int led_inited = 0;
static int daemon_avail = 0;

static int last_r, last_g, last_b;

/* ---- daemon socket (arc rendered by atrled; fallback: direct sysfs) ---- */

static int daemon_send(const char *cmd)
{
	int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0) return -1;
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, "/run/atriled.sock", sizeof(addr.sun_path) - 1);
	if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		close(fd);
		return -1;
	}
	int ret = write(fd, cmd, strlen(cmd));
	close(fd);
	return ret < 0 ? -1 : 0;
}

static int daemon_probe(void)
{
	/* connect-only probe: must NOT send a command (would disturb
	 * the daemon's current animation) */
	int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0) return 0;
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, "/run/atriled.sock", sizeof(addr.sun_path) - 1);
	int ok = connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0;
	close(fd);
	return ok;
}

static void show_color(int r, int g, int b)
{
	if (!led_inited) return;
	for (int i = 0; i < led.ring_count; i++)
		atri_led_set_ring_rgb(&led, i, r, g, b);
	atri_led_apply(&led);
}

static void show_volume(int pct)
{
	uint8_t r, g, b;
	atri_led_hsv_to_rgb((100 - pct) * 240 / 100, 255, 200, &r, &g, &b);
	last_r = r; last_g = g; last_b = b;

	if (daemon_avail) {
		char cmd[32];
		/* restore full brightness (may have been dimmed by fade) */
		daemon_send("brightness 100");
		snprintf(cmd, sizeof(cmd), "volume %d", pct);
		if (daemon_send(cmd) == 0)
			return;
		daemon_avail = 0;	/* daemon went away: direct mode */
	}

	/* direct fallback: arc on the ring */
	if (led_inited) {
		atri_led_render_arc(&led, pct, r, g, b, 0, 1);
		atri_led_apply(&led);
	}
}

static void fade_out(int step, int total)
{
	int t = step * 255 / total;
	int inv = 255 - t;
	/* smoothstep on integer math */
	int s = inv * inv * (3 * 255 - 2 * inv) / (255 * 255);
	if (daemon_avail) {
		/* daemon holds the arc; just dim it via brightness */
		char cmd[32];
		snprintf(cmd, sizeof(cmd), "brightness %d", s * 100 / 255);
		daemon_send(cmd);
		return;
	}
	show_color(last_r * s / 255, last_g * s / 255, last_b * s / 255);
}

static void volume_off(void)
{
	if (daemon_avail)
		daemon_send("brightness 100");
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
	int filt = -1;		/* EMA-filtered pct, -1 = uninitialized */

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handle_sig;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
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

	daemon_avail = daemon_probe();
	fprintf(stderr, "atrivolume: volume range %ld-%ld, daemon: %s\n",
		vol_min, vol_max, daemon_avail ? "yes" : "no (direct mode)");

	while (running) {
		int adc = read_adc();
		if (adc >= 0) {
			int raw = (ADC_MAX - adc) * 100 / ADC_MAX;
			/* EMA filter: smooths ADC jitter, responsive to any
			 * turn speed (fixes lost slow-turn updates) */
			if (filt < 0) filt = raw;
			filt = (filt * 3 + raw) / 4;
			int pct = filt;

			if (prev_pct < 0 || abs(pct - prev_pct) > HYSTERESIS) {
				set_volume(pct);
				show_volume(pct);
				prev_pct = pct;
				idle_ms = 0;
				fade_step = -1;
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
