/*
 * atrivolume — volume knob + LED feedback for AtriStation.
 *
 * Hardware chain: laser interrupter -> quadrature A/B chip -> GPIO ->
 * rotary-poll daemon (uinput) -> REL_DIAL events -> this daemon.
 *
 * Each dial tick steps the ALSA mixer volume and shows an arc on the
 * LED ring. Arc rendering is delegated to the atrled daemon over its
 * unix socket; if the daemon is absent, fall back to direct sysfs.
 *
 * Usage: atrivolume [mixer-element]   (default "PCM")
 */

#include "atri_led.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <alsa/asoundlib.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/input.h>
#include <poll.h>

#define IDLE_TIMEOUT_MS 1500
#define FADE_STEPS 50
#define VOLUME_STEP 2		/* percent per dial tick */
#define ARC_TICK_MS 12		/* render cadence while easing */

static snd_mixer_t *mixer;
static snd_mixer_elem_t *elem;
static long vol_min, vol_max;
static int running = 1;

static struct atri_led led;
static int led_inited = 0;
static int daemon_avail = 0;

static int cur_pct = -1;	/* current applied volume */
static int arc_pct = -1;	/* what the ring currently displays */
static int uinput_fd = -1;

/* ---- logging ---- */
#define LOGI(fmt, ...) fprintf(stderr, "atrivolume: " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) \
	fprintf(stderr, "atrivolume ERROR: " fmt " (%s)\n", \
		##__VA_ARGS__, strerror(errno))

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
	/* connect-only: sending anything would disturb the animation */
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

/* ---- mixer ---- */

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
		/* fall back to first active playback element */
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
		LOGI("mixer element '%s' not found, using '%s'",
		     selem_name, snd_mixer_selem_get_name(elem));
	}

	long cur;
	snd_mixer_selem_get_playback_volume_range(elem, &vol_min, &vol_max);
	if (snd_mixer_selem_get_playback_volume(elem, SND_MIXER_SCHN_FRONT_LEFT,
						&cur) == 0 && vol_max > vol_min)
		cur_pct = (int)((cur - vol_min) * 100 / (vol_max - vol_min));
	else
		cur_pct = 50;
	return 0;

err:
	snd_mixer_close(mixer);
	mixer = NULL;
	return ret;
}

static void set_volume(int pct)
{
	long val;
	if (!elem) return;
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	val = vol_min + (vol_max - vol_min) * pct / 100;
	if (snd_mixer_selem_set_playback_volume_all(elem, val) < 0)
		LOGE("set_playback_volume_all");
}

/* ---- ring rendering ---- */

static void show_arc(int pct)
{
	uint8_t r, g, b;
	char cmd[48];

	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	atri_led_hsv_to_rgb((100 - pct) * 240 / 100, 255, 200, &r, &g, &b);

	if (daemon_avail) {
		daemon_send("brightness 100");
		snprintf(cmd, sizeof(cmd), "volume %d", pct);
		if (daemon_send(cmd) == 0)
			return;
		LOGI("atrled went away, switching to direct mode");
		daemon_avail = 0;
	}

	if (led_inited) {
		atri_led_render_arc(&led, pct, r, g, b, 0, 1);
		atri_led_apply(&led);
	}
}

static void arc_off(void)
{
	if (daemon_avail)
		daemon_send("brightness 100");
	if (led_inited)
		atri_led_off(&led);
}

/* fade the arc out smoothly after idle period */
static void arc_fade_out(void)
{
	int s;

	if (!led_inited && !daemon_avail)
		return;

	for (s = FADE_STEPS; s >= 0 && running; s--) {
		if (daemon_avail) {
			char cmd[32];
			snprintf(cmd, sizeof(cmd), "brightness %d",
				 s * 100 / FADE_STEPS);
			daemon_send(cmd);
		} else if (led_inited) {
			uint8_t r, g, b;
			atri_led_hsv_to_rgb((100 - cur_pct) * 240 / 100,
					    255, 200 * s / FADE_STEPS,
					    &r, &g, &b);
			atri_led_render_arc(&led, cur_pct, r, g, b, 0, 1);
			atri_led_apply(&led);
		}
		usleep(ARC_TICK_MS * 1000);
	}
	arc_off();
}

/* ---- input device discovery (rotary-poll uinput, REL_DIAL) ---- */

static int try_open_input(const char *path)
{
	int fd = open(path, O_RDONLY | O_NONBLOCK);
	uint8_t rel_bits[REL_MAX / 8 + 1];

	if (fd < 0) return -1;
	memset(rel_bits, 0, sizeof(rel_bits));
	if (ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel_bits)), rel_bits) < 0) {
		close(fd);
		return -1;
	}
	/* need a relative axis; prefer devices exposing DIAL/WHEEL only */
	if ((rel_bits[REL_DIAL / 8] & (1 << (REL_DIAL % 8))) ||
	    (rel_bits[REL_WHEEL / 8] & (1 << (REL_WHEEL % 8))))
		return fd;
	close(fd);
	return -1;
}

static int find_knob_input(char *out_name, size_t name_len)
{
	DIR *d = opendir("/dev/input");
	struct dirent *de;
	int fd;

	if (!d) {
		LOGE("opendir /dev/input");
		return -1;
	}
	while ((de = readdir(d))) {
		char path[300];
		if (strncmp(de->d_name, "event", 5) != 0)
			continue;
		snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
		fd = try_open_input(path);
		if (fd >= 0) {
			if (ioctl(fd, EVIOCGNAME(name_len), out_name) < 0)
				snprintf(out_name, name_len, "%s", de->d_name);
			closedir(d);
			LOGI("knob input: %s (%s)", path, out_name);
			return fd;
		}
	}
	closedir(d);
	return -1;
}

/* ---- main loop ---- */

static void handle_sig(int sig)
{
	(void)sig;
	running = 0;
}

int main(int argc, char **argv)
{
	const char *selem_name = argc > 1 ? argv[1] : "PCM";
	struct input_event ev;
	char devname[256] = "?";
	long last_render_ms = 0;
	long now;

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handle_sig;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	if (init_mixer(selem_name) < 0) {
		LOGE("failed to init ALSA mixer");
		return 1;
	}
	LOGI("volume range %ld-%ld, start at %d%%", vol_min, vol_max, cur_pct);

	if (atri_led_init(&led) == 0) {
		led_inited = 1;
		LOGI("LED ring ready (%d rings)", led.ring_count);
	} else {
		LOGI("no LED ring");
	}

	daemon_avail = daemon_probe();
	LOGI("atrled daemon: %s",
	     daemon_avail ? "connected" : "absent (direct mode)");

	uinput_fd = find_knob_input(devname, sizeof(devname));
	if (uinput_fd < 0) {
		LOGE("no rotary/knob input device found — waiting for it "
		     "(is rotary-poll running?)");
	}

	arc_pct = cur_pct;
	show_arc(cur_pct);	/* initial position */

	/* main loop with CLOCK_MONOTONIC pacing */
	{
		struct timespec ts;
		int pending_arc = -1;
		long idle_ms = -1;

		clock_gettime(CLOCK_MONOTONIC, &ts);
		long t0 = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
		long last_event_ms = t0;

		while (running) {
			struct pollfd pfd = {
				.fd = uinput_fd >= 0 ? uinput_fd : -1,
				.events = POLLIN,
			};
			int timeout = uinput_fd >= 0 ? 200 : 1000;
			int ret = poll(&pfd, uinput_fd >= 0 ? 1u : 0u, timeout);

			clock_gettime(CLOCK_MONOTONIC, &ts);
			now = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

			if (ret > 0 && (pfd.revents & POLLIN)) {
				while (read(uinput_fd, &ev, sizeof(ev)) ==
				       sizeof(ev)) {
					if (ev.type != EV_REL)
						continue;
					int dir = (ev.code == REL_DIAL ||
						   ev.code == REL_WHEEL)
						? (ev.value > 0 ? 1 : -1)
						: 0;
					if (!dir || ev.value == 0)
						continue;
					cur_pct += dir * VOLUME_STEP * ev.value;
					if (cur_pct < 0) cur_pct = 0;
					if (cur_pct > 100) cur_pct = 100;
					set_volume(cur_pct);
					pending_arc = cur_pct;
					last_event_ms = now;
					idle_ms = 0;
				}
			} else if (ret < 0 && errno != EINTR &&
				   errno != EAGAIN) {
				/* device vanished (rotary-poll restarted):
				 * reopen */
				if (uinput_fd >= 0) close(uinput_fd);
				usleep(500000);
				uinput_fd = find_knob_input(devname,
							    sizeof(devname));
			}

			/* throttle ring updates */
			if (pending_arc >= 0 &&
			    now - last_render_ms >= ARC_TICK_MS) {
				arc_pct = pending_arc;
				pending_arc = -1;
				show_arc(arc_pct);
				last_render_ms = now;
			}

			if (idle_ms >= 0 && now - last_event_ms >=
			    IDLE_TIMEOUT_MS) {
				arc_fade_out();
				idle_ms = -1;
			}
		}
	}

	arc_off();

	if (uinput_fd >= 0) close(uinput_fd);
	if (mixer) {
		snd_mixer_detach(mixer, "default");
		snd_mixer_close(mixer);
	}
	LOGI("exit");
	return 0;
}
