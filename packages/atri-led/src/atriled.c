#include "atri_led.h"
#include "atri_led_anim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>

static const char *anim_dir = "/etc/atriled/animations";
static const char *pid_file = "/run/atriled.pid";
static const char *sock_path = "/run/atriled.sock";
static volatile sig_atomic_t running = 1;

/* render cadence and change threshold */
#define RENDER_INTERVAL_MS 8	/* ~125 Hz max render rate */
#define RENDER_MIN_DELTA 2	/* skip sysfs write if all channels moved < 2 */
#define FADE_STEPS 12
#define FADE_TICK_MS 14
#define ARC_TICK_MS 10

static void handle_signal(int sig)
{
	(void)sig;
	running = 0;
}

static void install_signals(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handle_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;	/* no SA_RESTART: poll() must wake on signal */
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);
}

static long long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/*
 * Daemon state machine.
 *
 * Playback is time-based: a timeline maps wall-clock ms onto frames and
 * every tick samples an interpolated frame (tweening). Absolute
 * deadlines mean socket command handling never shifts animation phase.
 * Switching animations crossfades asynchronously; volume arc eases to
 * its target instead of jumping.
 */
struct led_state {
	struct atri_led led;

	/* active animation */
	struct animation *builtin;	/* non-NULL if builtin */
	struct animation file_anim;	/* owns frames when use_file */
	int use_file;
	struct anim_timeline tl;
	long long t0;
	int loop;
	int playing;
	char current_name[128];

	uint8_t last_render[ATRI_LED_MAX_RINGS][3];
	int last_valid;
	int dirty;		/* master/gamma changed, force apply */

	/* pending switch (crossfade target) */
	struct animation pend_builtin;
	struct animation pend_file;	/* owns frames once loaded */
	int pend_use_file;
	char pend_name[128];
	int pend_loop;
	struct anim_timeline pend_tl;
	long long pend_t0;
	struct animation_frame fade_from;
	int fade_pos;		/* -1 = no fade in progress */

	/* volume arc easing */
	int arc_active;
	int arc_cur;
	int arc_target;
	uint8_t arc_rgb[3];
};

static void state_free_file(struct led_state *st)
{
	if (st->use_file && st->file_anim.frames) {
		free(st->file_anim.frames);
		memset(&st->file_anim, 0, sizeof(st->file_anim));
	}
	st->use_file = 0;
	st->playing = 0;
	st->current_name[0] = '\0';
	atri_led_timeline_free(&st->tl);
	st->last_valid = 0;
}

static struct animation *state_current_anim(struct led_state *st)
{
	return st->use_file ? &st->file_anim : st->builtin;
}

/* activate already-resolved animation (no fade): builds its timeline */
static void state_activate(struct led_state *st,
	struct animation *builtin, struct animation *file, int use_file,
	const char *name, int loop)
{
	state_free_file(st);

	st->builtin = builtin;
	if (use_file) {
		st->file_anim = *file;
		st->use_file = 1;
	} else {
		st->use_file = 0;
	}

	if (atri_led_timeline_build(state_current_anim(st), loop, &st->tl) == 0) {
		st->loop = loop;
		st->playing = 1;
		snprintf(st->current_name, sizeof(st->current_name), "%s", name);
		st->t0 = now_ms();
		st->dirty = 1;
	}
}

/*
 * Start playing `name` (builtin or file). If something is playing,
 * resolve the new animation now and crossfade to it asynchronously —
 * the socket loop keeps serving commands while fading.
 */
static int state_play(struct led_state *st, const char *name, int loop)
{
	struct animation *a = atri_led_find_builtin(name);
	struct animation tmp;
	int loaded = 0;

	if (!a) {
		char path[512];
		memset(&tmp, 0, sizeof(tmp));
		snprintf(path, sizeof(path), "%s/%s.anim", anim_dir, name);
		if (atri_led_load_animation(path, &tmp) < 0) {
			snprintf(path, sizeof(path), "%s/%s.led", anim_dir, name);
			if (atri_led_load_animation(path, &tmp) < 0)
				return -1;
		}
		if (tmp.frame_count <= 0 || !tmp.frames) {
			free(tmp.frames);
			return -1;
		}
		a = &tmp;
		loaded = 1;
	}

	if (st->fade_pos >= 0) {
		/* fade still running: finish it into the current target first */
		state_activate(st,
			st->pend_use_file ? NULL : &st->pend_builtin,
			&st->pend_file, st->pend_use_file,
			st->pend_name, st->pend_loop);
	}

	if (!st->playing) {
		if (loaded) {
			state_activate(st, NULL, &tmp, 1, name, loop);
		} else {
			state_activate(st, a, NULL, 0, name, loop);
		}
		return 0;
	}

	/* prepare async switch */
	memcpy(&st->fade_from, &st->led.brightness, sizeof(st->fade_from));
	st->pend_builtin = *a;	/* shallow: builtins are static */
	st->pend_use_file = loaded;
	if (loaded)
		st->pend_file = tmp;	/* ownership moves here */
	snprintf(st->pend_name, sizeof(st->pend_name), "%s", name);
	st->pend_loop = loop;
	atri_led_timeline_build(a, loop, &st->pend_tl);
	st->pend_t0 = now_ms();
	st->fade_pos = 0;
	return 0;
}

static void state_stop(struct led_state *st)
{
	st->fade_pos = -1;
	st->arc_active = 0;
	atri_led_timeline_free(&st->pend_tl);
	state_free_file(st);
}

static inline uint8_t chan_lerp(int from, int to, int frac)
{
	return (uint8_t)(from + ((to - from) * frac) / 255);
}

static void state_apply_if_changed(struct led_state *st,
	const struct animation_frame *f)
{
	int r, c, diff = 0;

	if (st->last_valid && !st->dirty) {
		for (r = 0; r < st->led.ring_count && !diff; r++)
			for (c = 0; c < 3; c++)
				if (abs((int)f->rgb[r][c] -
					(int)st->last_render[r][c]) >= RENDER_MIN_DELTA) {
					diff = 1;
					break;
				}
		if (!diff)
			return;
	}

	for (r = 0; r < st->led.ring_count && r < ATRI_LED_MAX_RINGS; r++) {
		for (c = 0; c < 3; c++) {
			st->led.brightness[r][c] = f->rgb[r][c];
			st->last_render[r][c] = f->rgb[r][c];
		}
	}
	st->last_valid = 1;
	st->dirty = 0;
	atri_led_apply(&st->led);
}

/*
 * One render pass. Returns ms until the next pass is due, -1 if idle.
 */
static long state_tick(struct led_state *st)
{
	struct animation_frame f;

	/* volume arc easing has priority while active */
	if (st->arc_active) {
		int step = abs(st->arc_target - st->arc_cur) / 4;
		if (step < 2) step = 2;
		if (st->arc_cur < st->arc_target)
			st->arc_cur += (st->arc_target - st->arc_cur < step)
				? st->arc_target - st->arc_cur : step;
		else if (st->arc_cur > st->arc_target)
			st->arc_cur -= (st->arc_cur - st->arc_target < step)
				? st->arc_cur - st->arc_target : step;

		atri_led_render_arc(&st->led, st->arc_cur,
			st->arc_rgb[0], st->arc_rgb[1], st->arc_rgb[2], 0, 1);
		atri_led_apply(&st->led);
		st->last_valid = 0;	/* arc bypasses change detection */

		if (st->arc_cur == st->arc_target)
			st->arc_active = 0;
		return ARC_TICK_MS;
	}

	/* asynchronous crossfade towards pending animation */
	if (st->fade_pos >= 0) {
		struct animation *pa = st->pend_use_file ? &st->pend_file
							 : &st->pend_builtin;
		int s = st->fade_pos * 255 / FADE_STEPS;
		int r, c;

		atri_led_timeline_sample(&st->pend_tl, pa,
					 now_ms() - st->pend_t0, &f);
		for (r = 0; r < st->led.ring_count && r < ATRI_LED_MAX_RINGS; r++)
			for (c = 0; c < 3; c++)
				st->led.brightness[r][c] =
					chan_lerp(st->fade_from.rgb[r][c],
						  f.rgb[r][c], s);
		atri_led_apply(&st->led);
		st->last_valid = 0;

		if (++st->fade_pos > FADE_STEPS) {
			struct animation file_copy = st->pend_file;
			int use_file = st->pend_use_file;
			char name[128];
			int loop = st->pend_loop;

			memcpy(name, st->pend_name, sizeof(name));
			memset(&st->pend_file, 0, sizeof(st->pend_file));
			atri_led_timeline_free(&st->pend_tl);
			st->fade_pos = -1;
			state_activate(st, use_file ? NULL : pa,
				       &file_copy, use_file, name, loop);
		}
		return FADE_TICK_MS;
	}

	/* normal interpolated playback */
	if (!st->playing || !state_current_anim(st))
		return -1;

	{
		struct animation *cur = state_current_anim(st);
		int active = atri_led_timeline_sample(&st->tl, cur,
			now_ms() - st->t0, &f);

		if (!active) {
			/* one-shot finished: hold the final frame, go idle */
			state_apply_if_changed(st, &f);
			state_stop(st);
			return -1;
		}
		state_apply_if_changed(st, &f);
	}
	return RENDER_INTERVAL_MS;
}

/* ---- command handling ---- */

static void handle_command(struct led_state *st, char *cmd)
{
	char *nl = strchr(cmd, '\n');
	if (nl) *nl = '\0';
	char *save = NULL;
	char *verb = strtok_r(cmd, " \t", &save);
	if (!verb) return;

	if (strcmp(verb, "play") == 0 || strcmp(verb, "loop") == 0) {
		char *name = strtok_r(NULL, " \t", &save);
		if (!name) return;
		int loop = (strcmp(verb, "loop") == 0);
		char *arg = strtok_r(NULL, " \t", &save);
		if (arg) loop = atoi(arg);
		if (state_play(st, name, loop) < 0)
			fprintf(stderr, "atriled: animation not found: %s\n", name);

	} else if (strcmp(verb, "color") == 0) {
		char *rs = strtok_r(NULL, " \t", &save);
		char *gs = strtok_r(NULL, " \t", &save);
		char *bs = strtok_r(NULL, " \t", &save);
		if (!rs || !gs || !bs) return;
		state_stop(st);
		atri_led_set_all_rgb(&st->led, atoi(rs), atoi(gs), atoi(bs));

	} else if (strcmp(verb, "brightness") == 0) {
		char *v = strtok_r(NULL, " \t", &save);
		if (!v) return;
		int pct = atoi(v);
		if (pct < 0) pct = 0;
		if (pct > 100) pct = 100;
		atri_led_set_master(&st->led, pct * 255 / 100);
		st->dirty = 1;
		if (!st->playing && !st->arc_active)
			atri_led_apply(&st->led);

	} else if (strcmp(verb, "gamma") == 0) {
		char *v = strtok_r(NULL, " \t", &save);
		if (!v) return;
		atri_led_set_gamma(&st->led, atoi(v));
		st->dirty = 1;
		if (!st->playing && !st->arc_active)
			atri_led_apply(&st->led);

	} else if (strcmp(verb, "volume") == 0) {
		char *v = strtok_r(NULL, " \t", &save);
		if (!v) return;
		int pct = atoi(v);
		uint8_t r, g, b;
		if (pct < 0) pct = 0;
		if (pct > 100) pct = 100;
		atri_led_hsv_to_rgb((100 - pct) * 240 / 100, 255, 200, &r, &g, &b);
		st->arc_rgb[0] = r; st->arc_rgb[1] = g; st->arc_rgb[2] = b;
		st->arc_target = pct;
		st->arc_active = 1;	/* eased in the tick loop */
		state_stop(st);

	} else if (strcmp(verb, "stop") == 0) {
		state_stop(st);

	} else if (strcmp(verb, "off") == 0) {
		state_stop(st);
		atri_led_off(&st->led);
	}
}

/* ---- socket ---- */

static int create_socket(void)
{
	unlink(sock_path);
	int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0) return -1;
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);
	if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		close(fd);
		return -1;
	}
	chmod(sock_path, 0666);
	return fd;
}

/*
 * Main loop: poll() multiplexes socket commands against animation
 * deadlines computed from CLOCK_MONOTONIC — command handling never
 * delays or shifts frame timing.
 */
static int daemon_loop(struct led_state *st, const char *start_anim)
{
	int sock = create_socket();
	if (sock < 0) {
		fprintf(stderr, "atriled: cannot create %s: %s\n",
			sock_path, strerror(errno));
		return 1;
	}

	if (start_anim && state_play(st, start_anim, 1) < 0)
		fprintf(stderr, "atriled: start animation '%s' not found\n",
			start_anim);

	while (running) {
		long timeout = state_tick(st);
		if (timeout < 0)
			timeout = -1;	/* idle: wait for commands only */

		struct pollfd pfd = { .fd = sock, .events = POLLIN };
		int ret = poll(&pfd, 1, (int)timeout);
		if (ret > 0 && (pfd.revents & POLLIN)) {
			char buf[256];
			int n = recvfrom(sock, buf, sizeof(buf) - 1, 0, NULL, NULL);
			if (n > 0) {
				buf[n] = '\0';
				handle_command(st, buf);
			}
		}
	}

	state_stop(st);
	atri_led_off(&st->led);
	close(sock);
	unlink(sock_path);
	return 0;
}

/* ---- daemonize (CLI use; systemd runs with -f) ---- */

static int daemonize(void)
{
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid > 0) _exit(0);
	if (setsid() < 0) return -1;
	signal(SIGHUP, SIG_IGN);
	pid = fork();
	if (pid < 0) return -1;
	if (pid > 0) _exit(0);
	if (chdir("/") < 0) { /* best effort */ }
	umask(0);
	return 0;
}

static void write_pidfile(void)
{
	FILE *pf = fopen(pid_file, "w");
	if (pf) { fprintf(pf, "%d\n", getpid()); fclose(pf); }
}

/* ---- CLI helpers ---- */

static void list_animations(void)
{
	printf("Builtin:\n");
	for (int i = 0; i < atri_led_builtin_count(); i++)
		printf("  %s\n", atri_led_builtin_get(i)->name);

	DIR *d = opendir(anim_dir);
	if (!d) {
		printf("Files: no directory %s\n", anim_dir);
		return;
	}
	printf("Files (%s):\n", anim_dir);
	struct dirent *de;
	int n = 0;
	while ((de = readdir(d)) != NULL) {
		if (strstr(de->d_name, ".anim") || strstr(de->d_name, ".led")) {
			char name[128];
			strncpy(name, de->d_name, sizeof(name) - 1);
			name[sizeof(name) - 1] = '\0';
			char *dot = strstr(name, ".anim");
			if (!dot) dot = strstr(name, ".led");
			if (dot) *dot = '\0';
			printf("  %s\n", name);
			n++;
		}
	}
	closedir(d);
	if (n == 0) printf("  (none)\n");
}

static int show_status(void)
{
	FILE *pf = fopen(pid_file, "r");
	if (!pf) {
		printf("atriled: not running\n");
		return 1;
	}
	int pid;
	if (fscanf(pf, "%d", &pid) != 1) {
		fclose(pf);
		printf("atriled: stale pid file\n");
		return 1;
	}
	fclose(pf);
	if (kill(pid, 0) == 0)
		printf("atriled: running (pid %d)\n", pid);
	else {
		printf("atriled: not running (stale pid %d)\n", pid);
		unlink(pid_file);
		return 1;
	}
	return 0;
}

/* shared foreground playback for play/loop CLI commands */
static int run_foreground(struct led_state *st, const char *name, int loop)
{
	struct animation *a = atri_led_find_builtin(name);
	struct animation tmp = {0};
	struct anim_timeline tl;
	int loaded = 0;

	if (!a) {
		char path[512];
		snprintf(path, sizeof(path), "%s/%s.anim", anim_dir, name);
		if (atri_led_load_animation(path, &tmp) < 0) {
			snprintf(path, sizeof(path), "%s/%s.led", anim_dir, name);
			if (atri_led_load_animation(path, &tmp) < 0) {
				fprintf(stderr, "Animation not found: %s\n", name);
				return 1;
			}
		}
		a = &tmp;
		loaded = 1;
	}
	if (atri_led_timeline_build(a, loop, &tl) != 0)
		return 1;

	long long t0 = now_ms();
	while (running) {
		struct animation_frame f;
		if (!atri_led_timeline_sample(&tl, a, now_ms() - t0, &f)) {
			if (!loop) break;
			t0 = now_ms();	/* safety, sampler wraps anyway */
			continue;
		}
		for (int r = 0; r < st->led.ring_count && r < ATRI_LED_MAX_RINGS; r++)
			for (int c = 0; c < 3; c++)
				st->led.brightness[r][c] = f.rgb[r][c];
		atri_led_apply(&st->led);
		usleep(RENDER_INTERVAL_MS * 1000);
		if (!loop)
			break;
	}

	if (loaded) free(tmp.frames);
	atri_led_timeline_free(&tl);
	return 0;
}

static void print_usage(const char *prog)
{
	printf("Usage: %s <command> [args]\n\n", prog);
	printf("Commands:\n");
	printf("  daemon [-f] [anim]       Run as daemon (-f = foreground for systemd)\n");
	printf("  play <name>              Play an animation (once, builtin or file)\n");
	printf("  loop <name>              Loop an animation\n");
	printf("  list                     List builtin + file animations\n");
	printf("  status                   Check if daemon is running\n");
	printf("  stop                     Stop the daemon\n");
	printf("  off                      Turn off all LEDs\n");
	printf("  color <r> <g> <b>        Set solid color and exit\n");
	printf("  brightness <0-100>       Set master brightness and exit\n");
	printf("  test                     Play all animations sequentially\n\n");
	printf("File animations are loaded from: %s\n", anim_dir);
	printf("Builtins are always available (see 'list').\n");
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		print_usage(argv[0]);
		return 1;
	}

	install_signals();

	atri_led_init_animations();

	struct led_state st;
	memset(&st, 0, sizeof(st));
	st.fade_pos = -1;
	if (atri_led_init(&st.led) < 0) {
		fprintf(stderr, "Failed to initialize LED control\n");
		return 1;
	}

	const char *cmd = argv[1];

	if (strcmp(cmd, "daemon") == 0) {
		int foreground = 0;
		const char *anim_name = "notification_passive";
		for (int i = 2; i < argc; i++) {
			if (strcmp(argv[i], "-f") == 0)
				foreground = 1;
			else
				anim_name = argv[i];
		}
		if (!foreground && daemonize() < 0) {
			fprintf(stderr, "Failed to daemonize\n");
			return 1;
		}
		write_pidfile();
		int ret = daemon_loop(&st, anim_name);
		unlink(pid_file);
		return ret;

	} else if (strcmp(cmd, "play") == 0) {
		if (argc < 3) { fprintf(stderr, "Usage: %s play <name>\n", argv[0]); return 1; }
		return run_foreground(&st, argv[2], 0);

	} else if (strcmp(cmd, "loop") == 0) {
		if (argc < 3) { fprintf(stderr, "Usage: %s loop <name>\n", argv[0]); return 1; }
		return run_foreground(&st, argv[2], 1);

	} else if (strcmp(cmd, "color") == 0) {
		if (argc < 5) { fprintf(stderr, "Usage: %s color <r> <g> <b>\n", argv[0]); return 1; }
		atri_led_set_all_rgb(&st.led, atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));
		return 0;

	} else if (strcmp(cmd, "brightness") == 0) {
		if (argc < 3) { fprintf(stderr, "Usage: %s brightness <0-100>\n", argv[0]); return 1; }
		int pct = atoi(argv[2]);
		if (pct < 0) pct = 0;
		if (pct > 100) pct = 100;
		atri_led_set_master(&st.led, pct * 255 / 100);
		atri_led_apply(&st.led);
		return 0;

	} else if (strcmp(cmd, "list") == 0) {
		list_animations();

	} else if (strcmp(cmd, "status") == 0) {
		return show_status();

	} else if (strcmp(cmd, "stop") == 0) {
		FILE *pf = fopen(pid_file, "r");
		if (!pf) { printf("atriled: not running\n"); return 1; }
		int pid;
		if (fscanf(pf, "%d", &pid) != 1) { fclose(pf); return 1; }
		fclose(pf);
		if (kill(pid, SIGTERM) == 0) {
			printf("atriled: stopped pid %d\n", pid);
			unlink(pid_file);
		} else {
			perror("kill");
			return 1;
		}

	} else if (strcmp(cmd, "off") == 0) {
		atri_led_off(&st.led);
		printf("LEDs turned off\n");

	} else if (strcmp(cmd, "test") == 0) {
		int count = 0;
		printf("== builtins ==\n");
		for (int i = 0; i < atri_led_builtin_count() && running; i++) {
			struct animation *a = atri_led_builtin_get(i);
			printf("[%d] %s\n", ++count, a->name);
			run_foreground(&st, a->name, 0);
		}
		DIR *d = opendir(anim_dir);
		if (d) {
			printf("== files ==\n");
			struct dirent *de;
			while ((de = readdir(d)) != NULL && running) {
				if (!strstr(de->d_name, ".led") && !strstr(de->d_name, ".anim"))
					continue;
				char name[128];
				strncpy(name, de->d_name, sizeof(name) - 1);
				name[sizeof(name) - 1] = '\0';
				char *dot = strstr(name, ".anim");
				if (!dot) dot = strstr(name, ".led");
				if (dot) *dot = '\0';
				printf("[%d] %s\n", ++count, name);
				run_foreground(&st, name, 0);
				usleep(300000);
			}
			closedir(d);
		}
		printf("Test complete: %d animations played\n", count);

	} else {
		fprintf(stderr, "Unknown command: %s\n", cmd);
		print_usage(argv[0]);
		return 1;
	}

	return 0;
}
