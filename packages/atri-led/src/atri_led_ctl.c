#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <signal.h>

static const char *sock_path = "/run/atriled.sock";
static const char *pid_file = "/run/atriled.pid";

static void print_usage(const char *prog)
{
	printf("Usage: %s <command> [args]\n\n", prog);
	printf("Commands:\n");
	printf("  play <name>            Play an animation (once)\n");
	printf("  loop <name>            Loop an animation\n");
	printf("  color <r> <g> <b>      Set solid color (0-255)\n");
	printf("  brightness <0-100>     Master brightness\n");
	printf("  gamma <0|1>            Gamma correction on/off\n");
	printf("  volume <0-100>         Show volume arc\n");
	printf("  stop                   Stop animation (hold frame)\n");
	printf("  off                    Turn off all LEDs\n");
	printf("  list                   List available animations\n");
	printf("  status                 Query playback state from daemon\n");
	printf("  kill                   Stop the daemon\n\n");
	printf("Animations: builtins + files from /etc/atriled/animations\n");
}

static int send_cmd(const char *cmd)
{
	int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0) { perror("socket"); return -1; }
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);
	if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		close(fd);
		fprintf(stderr, "atriled daemon not running (socket: %s)\n", sock_path);
		return -1;
	}
	int ret = write(fd, cmd, strlen(cmd)) < 0 ? -1 : 0;
	close(fd);
	return ret;
}

static void list_animations(void)
{
	DIR *d = opendir("/etc/atriled/animations");
	if (!d) { printf("No animations directory\n"); return; }
	struct dirent *de;
	int n = 0;
	while ((de = readdir(d)) != NULL) {
		char *ext = strstr(de->d_name, ".anim");
		if (!ext) ext = strstr(de->d_name, ".led");
		if (ext) {
			*ext = '\0';
			printf("  %s\n", de->d_name);
			n++;
		}
	}
	closedir(d);
	if (n == 0) printf("(no animations found)\n");
}

/*
 * Live status query: one datagram round-trip with the daemon. The client
 * binds a private path in /tmp so the daemon can reply to the sender.
 */
static int query_status(void)
{
	char buf[256];
	struct sockaddr_un cli;
	struct sockaddr_un addr;
	int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0) { perror("socket"); return 1; }

	memset(&cli, 0, sizeof(cli));
	cli.sun_family = AF_UNIX;
	snprintf(cli.sun_path, sizeof(cli.sun_path),
		 "/tmp/atrledctl-%ld.sock", (long)getpid());
	unlink(cli.sun_path);
	if (bind(fd, (struct sockaddr *)&cli, sizeof(cli)) < 0) {
		perror("bind");
		close(fd);
		return 1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

	struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	if (sendto(fd, "status", 6, 0,
		   (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "atriled: cannot reach daemon (%s)\n",
			sock_path);
		unlink(cli.sun_path);
		close(fd);
		return 1;
	}

	ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0, NULL, NULL);
	if (n < 0) {
		fprintf(stderr, "atriled: status timeout\n");
		unlink(cli.sun_path);
		close(fd);
		return 1;
	}
	buf[n] = '\0';
	printf("%s\n", buf);
	unlink(cli.sun_path);
	close(fd);
	return 0;
}

int main(int argc, char *argv[])
{
	if (argc < 2) { print_usage(argv[0]); return 1; }

	const char *cmd = argv[1];

	if (strcmp(cmd, "play") == 0 || strcmp(cmd, "loop") == 0) {
		if (argc < 3) {
			fprintf(stderr, "Usage: %s %s <name>\n", argv[0], cmd);
			return 1;
		}
		int loop = (strcmp(cmd, "loop") == 0) ? 1 : 0;
		char buf[256];
		snprintf(buf, sizeof(buf), "play %s %d", argv[2], loop);
		return send_cmd(buf);

	} else if (strcmp(cmd, "color") == 0) {
		if (argc < 5) {
			fprintf(stderr, "Usage: %s color <r> <g> <b>\n", argv[0]);
			return 1;
		}
		char buf[64];
		snprintf(buf, sizeof(buf), "color %s %s %s", argv[2], argv[3], argv[4]);
		return send_cmd(buf);

	} else if (strcmp(cmd, "brightness") == 0) {
		if (argc < 3) {
			fprintf(stderr, "Usage: %s brightness <0-100>\n", argv[0]);
			return 1;
		}
		char buf[32];
		snprintf(buf, sizeof(buf), "brightness %s", argv[2]);
		return send_cmd(buf);

	} else if (strcmp(cmd, "gamma") == 0) {
		if (argc < 3) {
			fprintf(stderr, "Usage: %s gamma <0|1>\n", argv[0]);
			return 1;
		}
		char buf[32];
		snprintf(buf, sizeof(buf), "gamma %s", argv[2]);
		return send_cmd(buf);

	} else if (strcmp(cmd, "volume") == 0) {
		if (argc < 3) {
			fprintf(stderr, "Usage: %s volume <0-100>\n", argv[0]);
			return 1;
		}
		char buf[32];
		snprintf(buf, sizeof(buf), "volume %s", argv[2]);
		return send_cmd(buf);

	} else if (strcmp(cmd, "stop") == 0) {
		return send_cmd("stop");

	} else if (strcmp(cmd, "off") == 0) {
		return send_cmd("off");

	} else if (strcmp(cmd, "list") == 0) {
		list_animations();

	} else if (strcmp(cmd, "status") == 0) {
		return query_status();

	} else if (strcmp(cmd, "kill") == 0) {
		FILE *pf = fopen(pid_file, "r");
		if (!pf) { printf("atriled: not running\n"); return 1; }
		int pid;
		if (fscanf(pf, "%d", &pid) != 1) { fclose(pf); return 1; }
		fclose(pf);
		if (kill(pid, SIGTERM) == 0) {
			printf("atriled: stopped\n");
			unlink(pid_file);
			unlink(sock_path);
		} else {
			perror("kill");
			return 1;
		}

	} else {
		fprintf(stderr, "Unknown command: %s\n", cmd);
		print_usage(argv[0]);
		return 1;
	}

	return 0;
}
