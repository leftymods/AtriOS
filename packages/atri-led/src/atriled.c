#include "atri_led.h"
#include "atri_led_anim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

static const char *anim_dir = "/etc/atriled/animations";
static const char *pid_file = "/run/atriled.pid";
static volatile int running = 1;

static void handle_signal(int sig)
{
	(void)sig;
	running = 0;
}

static int load_and_play(struct atri_led *led, const char *name, int loop)
{
	struct animation anim;
	memset(&anim, 0, sizeof(anim));

	char path[512];
	snprintf(path, sizeof(path), "%s/%s.anim", anim_dir, name);

	int ret = atri_led_load_animation(path, &anim);
	if (ret < 0) {
		snprintf(path, sizeof(path), "%s/%s.led", anim_dir, name);
		ret = atri_led_load_animation(path, &anim);
	}
	if (ret < 0) {
		fprintf(stderr, "Failed to load animation: %s\n", name);
		return -1;
	}

	struct timespec ts;
	ts.tv_sec = 0;

	while (running) {
		for (int f = 0; f < anim.frame_count && running; f++) {
			for (int r = 0; r < led->ring_count; r++) {
				led->brightness[r][RGB_R] = anim.frames[f].rgb[r][RGB_R];
				led->brightness[r][RGB_G] = anim.frames[f].rgb[r][RGB_G];
				led->brightness[r][RGB_B] = anim.frames[f].rgb[r][RGB_B];
			}
			atri_led_apply(led);
			if (anim.frames[f].duration_ms > 0) {
				ts.tv_nsec = anim.frames[f].duration_ms * 1000000L;
				nanosleep(&ts, NULL);
			}
		}
		if (!loop) break;
	}

	free(anim.frames);
	return 0;
}

static int daemonize(void)
{
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid > 0) {
		printf("atriled started (pid %d)\n", pid);
		_exit(0);
	}
	if (setsid() < 0) return -1;
	signal(SIGHUP, SIG_IGN);
	pid = fork();
	if (pid < 0) return -1;
	if (pid > 0) _exit(0);
	chdir("/");
	umask(0);
	fclose(stdin);
	fclose(stdout);
	fclose(stderr);
	FILE *pf = fopen(pid_file, "w");
	if (pf) { fprintf(pf, "%d\n", getpid()); fclose(pf); }
	return 0;
}

static void list_animations(void)
{
	DIR *d = opendir(anim_dir);
	if (!d) {
		printf("No animations found (directory: %s)\n", anim_dir);
		return;
	}
	struct dirent *de;
	int n = 0;
	while ((de = readdir(d)) != NULL) {
		if (strstr(de->d_name, ".anim") || strstr(de->d_name, ".led")) {
			char name[128];
			memcpy(name, de->d_name, sizeof(name) - 1);
			name[sizeof(name) - 1] = '\0';
			char *dot = strstr(name, ".anim");
			if (!dot) dot = strstr(name, ".led");
			if (dot) *dot = '\0';
			printf("  %s\n", name);
			n++;
		}
	}
	closedir(d);
	if (n == 0) printf("(no .anim or .led files found in %s)\n", anim_dir);
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
	printf("  daemon [anim]              Run as daemon with optional animation\n");
	printf("  play <name>                Play an animation file (once)\n");
	printf("  loop <name>                Loop an animation file\n");
	printf("  list                       List available animations\n");
	printf("  status                     Check if daemon is running\n");
	printf("  stop                       Stop the daemon\n");
	printf("  off                        Turn off all LEDs\n\n");
	printf("Animations are loaded from: %s\n", anim_dir);
	printf("Use 'list' to show available animations.\n");
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		print_usage(argv[0]);
		return 1;
	}

	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);
	signal(SIGPIPE, SIG_IGN);

	struct atri_led led;
	if (atri_led_init(&led) < 0) {
		fprintf(stderr, "Failed to initialize LED control\n");
		return 1;
	}

	const char *cmd = argv[1];

	if (strcmp(cmd, "daemon") == 0) {
		const char *anim_name = argc > 2 ? argv[2] : "happy";
		if (daemonize() < 0) {
			fprintf(stderr, "Failed to daemonize\n");
			return 1;
		}
		atri_led_init(&led);
		load_and_play(&led, anim_name, 1);
		atri_led_off(&led);
		unlink(pid_file);

	} else if (strcmp(cmd, "play") == 0) {
		if (argc < 3) { fprintf(stderr, "Usage: %s play <name>\n", argv[0]); return 1; }
		return load_and_play(&led, argv[2], 0);

	} else if (strcmp(cmd, "loop") == 0) {
		if (argc < 3) { fprintf(stderr, "Usage: %s loop <name>\n", argv[0]); return 1; }
		return load_and_play(&led, argv[2], 1);

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
		atri_led_off(&led);
		printf("LEDs turned off\n");

	} else {
		fprintf(stderr, "Unknown command: %s\n", cmd);
		print_usage(argv[0]);
		return 1;
	}

	return 0;
}
