#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
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
	printf("  list                   List available animations\n");
	printf("  status                 Check if daemon is running\n");
	printf("  stop                   Stop the daemon\n");
	printf("  off                    Turn off all LEDs\n\n");
	printf("Animations are loaded from: /etc/atriled/animations\n");
	printf("Use 'list' to show available animations.\n");
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
	write(fd, cmd, strlen(cmd));
	close(fd);
	return 0;
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

static int show_status(void)
{
	FILE *pf = fopen(pid_file, "r");
	if (!pf) { printf("atriled: not running\n"); return 1; }
	int pid;
	if (fscanf(pf, "%d", &pid) != 1) { fclose(pf); return 1; }
	fclose(pf);
	if (kill(pid, 0) == 0)
		printf("atriled: running (pid %d)\n", pid);
	else {
		printf("atriled: not running\n");
		unlink(pid_file);
		return 1;
	}
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
			printf("atriled: stopped\n");
			unlink(pid_file);
			unlink(sock_path);
		} else {
			perror("kill");
			return 1;
		}

	} else if (strcmp(cmd, "off") == 0) {
		return send_cmd("play off 0");

	} else {
		fprintf(stderr, "Unknown command: %s\n", cmd);
		print_usage(argv[0]);
		return 1;
	}

	return 0;
}