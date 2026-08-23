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

/* ---- daemon state machine ---- */

struct led_state {
	struct atri_led led;
	struct animation *builtin;	/* non-NULL if playing a builtin */
	struct animation file_anim;	/* loaded file animation (owns frames) */
	int use_file;			/* 1 = play file_anim, 0 = builtin/none */
	int frame_idx;
	int loop;
	int playing;			/* 0 = idle (hold current LEDs) */
	char current_name[128];
};

static struct animation *state_current_anim(struct led_state *st)
{
	if (st->playing == 0) return NULL;
	return st->use_file ? &st->file_anim : st->builtin;
}

static void state_render_frame(struct led_state *st)
{
	struct animation *a = state_current_anim(st);
	if (!a || st->frame_idx >= a->frame_count) return;
	for (int r = 0; r < st->led.ring_count && r < ATRI_LED_MAX_RINGS; r++) {
		st->led.brightness[r][RGB_R] = a->frames[st->frame_idx].rgb[r][RGB_R];
		st->led.brightness[r][RGB_G] = a->frames[st->frame_idx].rgb[r][RGB_G];
		st->led.brightness[r][RGB_B] = a->frames[st->frame_idx].rgb[r][RGB_B];
	}
	atri_led_apply(&st->led);
}

static void state_free_file(struct led_state *st)
{
	if (st->use_file && st->file_anim.frames) {
		free(st->file_anim.frames);
		st->file_anim.frames = NULL;
	}
	st->use_file = 0;
}

/* crossfade from current LED state to first frame of the new animation */
static void state_transition(struct led_state *st, struct animation_frame *target)
{
	const int steps = 8, step_ms = 18;
	uint8_t from[ATRI_LED_MAX_RINGS][3];
	memcpy(from, st->led.brightness, sizeof(from));
	for (int s = 1; s <= steps && running; s++) {
		for (int r = 0; r < st->led.ring_count && r < ATRI_LED_MAX_RINGS; r++) {
			for (int c = 0; c < 3; c++) {
				int f = from[r][c], t = target->rgb[r][c];
				st->led.brightness[r][c] = f + (t - f) * s / steps;
			}
		}
		atri_led_apply(&st->led);
		usleep(step_ms * 1000);
	}
}

static int state_play(struct led_state *st, const char *name, int loop)
{
	struct animation *a = atri_led_find_builtin(name);
	struct animation_frame first;
	int new_use_file = 0;

	if (!a) {
		char path[512];
		snprintf(path, sizeof(path), "%s/%s.anim", anim_dir, name);
		struct animation tmp;
		memset(&tmp, 0, sizeof(tmp));
		if (atri_led_load_animation(path, &tmp) < 0) {
			snprintf(path, sizeof(path), "%s/%s.led", anim_dir, name);
			if (atri_led_load_animation(path, &tmp) < 0)
				return -1;
		}
		if (tmp.frame_count <= 0 || !tmp.frames) {
			free(tmp.frames);
			return -1;
		}
		state_free_file(st);
		st->file_anim = tmp;
		a = &st->file_anim;
		new_use_file = 1;
	}

	memset(&first, 0, sizeof(first));
	memcpy(&first, &a->frames[0], sizeof(first));

	st->use_file = new_use_file;
	st->builtin = new_use_file ? NULL : a;
	st->frame_idx = 0;
	st->loop = loop;
	st->playing = 1;
	snprintf(st->current_name, sizeof(st->current_name), "%s", name);

	state_transition(st, &first);
	return 0;
}

static void state_stop(struct led_state *st)
{
	st->playing = 0;
	st->current_name[0] = '\0';
	state_free_file(st);
}

/* advance animation; returns ms until next frame, -1 if idle */
static long state_tick(struct led_state *st)
{
	struct animation *a = state_current_anim(st);
	if (!a) return -1;

	state_render_frame(st);

	int dur = a->frames[st->frame_idx].duration_ms;
	st->frame_idx++;
	if (st->frame_idx >= a->frame_count) {
		if (st->loop) {
			st->frame_idx = 0;
		} else {
			/* hold last frame, go idle */
			st->playing = 0;
			state_free_file(st);
			return -1;
		}
	}
	return dur > 0 ? dur : 20;
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
		/* optional explicit loop arg: "play name 1" */
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
		if (!st->playing) atri_led_apply(&st->led);

	} else if (strcmp(verb, "gamma") == 0) {
		char *v = strtok_r(NULL, " \t", &save);
		if (!v) return;
		atri_led_set_gamma(&st->led, atoi(v));
		if (!st->playing) atri_led_apply(&st->led);

	} else if (strcmp(verb, "volume") == 0) {
		char *v = strtok_r(NULL, " \t", &save);
		if (!v) return;
		int pct = atoi(v);
		uint8_t r, g, b;
		atri_led_hsv_to_rgb((100 - pct) * 240 / 100, 255, 200, &r, &g, &b);
		state_stop(st);
		atri_led_render_arc(&st->led, pct, r, g, b, 0, 1);
		atri_led_apply(&st->led);

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
 * Main loop: single process, poll() multiplexes socket commands and
 * animation frame timing. No forks — commands always responsive.
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
		fprintf(stderr, "atriled: start animation '%s' not found\n", start_anim);

	while (running) {
		long tick = state_tick(st);
		long timeout = tick < 0 ? -1 : tick;	/* idle: wait for commands only */

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
		if (state_play(&st, argv[2], 0) < 0) {
			fprintf(stderr, "Animation not found: %s\n", argv[2]);
			return 1;
		}
		while (st.playing && running) {
			long tick = state_tick(&st);
			if (tick > 0) usleep(tick * 1000);
		}
		return 0;

	} else if (strcmp(cmd, "loop") == 0) {
		if (argc < 3) { fprintf(stderr, "Usage: %s loop <name>\n", argv[0]); return 1; }
		if (state_play(&st, argv[2], 1) < 0) {
			fprintf(stderr, "Animation not found: %s\n", argv[2]);
			return 1;
		}
		while (running) {
			long tick = state_tick(&st);
			if (tick > 0) usleep(tick * 1000);
		}
		return 0;

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
			for (int f = 0; f < a->frame_count && running; f++) {
				for (int r = 0; r < st.led.ring_count; r++)
					for (int c = 0; c < 3; c++)
						st.led.brightness[r][c] = a->frames[f].rgb[r][c];
				atri_led_apply(&st.led);
				usleep((a->frames[f].duration_ms > 0 ?
					a->frames[f].duration_ms : 20) * 1000);
			}
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
				if (state_play(&st, name, 0) == 0) {
					while (st.playing && running) {
						long tick = state_tick(&st);
						if (tick > 0) usleep(tick * 1000);
					}
				}
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
