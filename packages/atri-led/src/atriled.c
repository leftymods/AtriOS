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
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>

static const char *anim_dir = "/etc/atriled/animations";
static const char *pid_file = "/run/atriled.pid";
static const char *sock_path = "/run/atriled.sock";
static volatile int running = 1;
static char current_anim[128] = "";
static int current_loop = 1;

static void handle_signal(int sig)
{
	(void)sig;
	running = 0;
}

static int load_and_play(struct atri_led *led, const char *name, int loop)
{
	struct animation anim;
	memset(&anim, 0, sizeof(anim));

	strncpy(current_anim, name, sizeof(current_anim) - 1);
	current_loop = loop;

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
			if (anim.frames[f].duration_ms < 0) {
				running = 0;
				break;
			}
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
		if (!running) break;
	}

	free(anim.frames);
	return 0;
}

static int daemonize(void)
{
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid > 0) {
		_exit(0);
	}
	if (setsid() < 0) return -1;
	signal(SIGHUP, SIG_IGN);
	pid = fork();
	if (pid < 0) return -1;
	if (pid > 0) _exit(0);
	chdir("/");
	umask(0);
	FILE *pf = fopen(pid_file, "w");
	if (pf) { fprintf(pf, "%d\n", getpid()); fclose(pf); }
	return 0;
}

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

static void handle_command(struct atri_led *led, const char *cmd)
{
	if (strncmp(cmd, "play ", 5) == 0) {
		char name[128];
		strncpy(name, cmd + 5, sizeof(name) - 1);
		name[sizeof(name) - 1] = '\0';
		char *nl = strchr(name, '\n');
		if (nl) *nl = '\0';
		char *sp = strchr(name, ' ');
		int loop = 0;
		if (sp) { *sp = '\0'; loop = atoi(sp + 1); }
		running = 0;
		usleep(100000);
		running = 1;
		load_and_play(led, name, loop);
	}
}

static void socket_loop(struct atri_led *led)
{
	int sock = create_socket();
	if (sock < 0) return;

	char buf[256];
	struct sockaddr_un client;
	socklen_t len = sizeof(client);
	while (1) {
		int n = recvfrom(sock, buf, sizeof(buf) - 1, 0,
			(struct sockaddr*)&client, &len);
		if (n > 0) {
			buf[n] = '\0';
			handle_command(led, buf);
		}
	}
	close(sock);
	unlink(sock_path);
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
	printf("  off                        Turn off all LEDs\n");
	printf("  test                       Play all animations sequentially\n\n");
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
		const char *anim_name = argc > 2 ? argv[2] : "notification_passive";
		if (daemonize() < 0) {
			fprintf(stderr, "Failed to daemonize\n");
			return 1;
		}
		atri_led_init(&led);
		/* Start animation in a separate process, then listen for commands */
		pid_t apid = fork();
		if (apid == 0) {
			load_and_play(&led, anim_name, 1);
			atri_led_off(&led);
			_exit(0);
		}
		socket_loop(&led);
		kill(apid, SIGTERM);
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

	} else if (strcmp(cmd, "test") == 0) {
		DIR *d = opendir(anim_dir);
		if (!d) { fprintf(stderr, "No animations directory\n"); return 1; }
		struct dirent *de;
		int count = 0;
		while ((de = readdir(d)) != NULL) {
			if (strstr(de->d_name, ".led") || strstr(de->d_name, ".anim")) {
				char name[128];
				memcpy(name, de->d_name, sizeof(name) - 1);
				name[sizeof(name) - 1] = '\0';
				char *dot = strstr(name, ".anim");
				if (!dot) dot = strstr(name, ".led");
				if (dot) *dot = '\0';
				printf("[%d] Playing: %s\n", ++count, name);
				load_and_play(&led, name, 0);
				usleep(500000);
			}
		}
		closedir(d);
		printf("Test complete: %d animations played\n", count);

	} else {
		fprintf(stderr, "Unknown command: %s\n", cmd);
		print_usage(argv[0]);
		return 1;
	}

	return 0;
}
