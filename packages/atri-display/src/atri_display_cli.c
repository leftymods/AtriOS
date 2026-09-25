/*
 * atri_display_cli.c - CLI tool to control atri-displayd
 *
 * Copyright (c) 2026 leftymods / AtriOS Project
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCK_PATH "/run/atri-display.sock"

static void print_usage(const char *prog)
{
	printf("Usage: %s <command> [arguments]\n\n", prog);
	printf("Commands:\n");
	printf("  clock              Switch to digital clock mode (HH:MM)\n");
	printf("  msg <text>         Display scrolling text message\n");
	printf("  temp <+22|-5>      Show temperature display\n");
	printf("  vol <0..100>       Display volume level overlay\n");
	printf("  ip                 Scroll device primary IP address\n");
	printf("  eyes               Play playful eye blinking animation\n");
	printf("  brightness <val>   Set brightness (0..200 or 'auto')\n");
	printf("  clear              Clear screen\n");
	printf("  status             Query daemon status\n");
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		print_usage(argv[0]);
		return 1;
	}

	char cmd[256] = {0};
	const char *action = argv[1];

	if (strcasecmp(action, "clock") == 0 || strcasecmp(action, "time") == 0) {
		snprintf(cmd, sizeof(cmd), "CLOCK\n");
	} else if (strcasecmp(action, "msg") == 0) {
		if (argc < 3) {
			fprintf(stderr, "Error: msg command requires text argument\n");
			return 1;
		}
		/* Join remaining args */
		char text[200] = {0};
		for (int i = 2; i < argc; i++) {
			strncat(text, argv[i], sizeof(text) - strlen(text) - 1);
			if (i < argc - 1) strncat(text, " ", sizeof(text) - strlen(text) - 1);
		}
		snprintf(cmd, sizeof(cmd), "MSG %s\n", text);
	} else if (strcasecmp(action, "temp") == 0) {
		if (argc < 3) {
			fprintf(stderr, "Error: temp command requires value (e.g. +22)\n");
			return 1;
		}
		snprintf(cmd, sizeof(cmd), "TEMP %s\n", argv[2]);
	} else if (strcasecmp(action, "vol") == 0 || strcasecmp(action, "volume") == 0) {
		if (argc < 3) {
			fprintf(stderr, "Error: vol command requires level (0..100)\n");
			return 1;
		}
		snprintf(cmd, sizeof(cmd), "VOL %s\n", argv[2]);
	} else if (strcasecmp(action, "ip") == 0) {
		snprintf(cmd, sizeof(cmd), "IP\n");
	} else if (strcasecmp(action, "eyes") == 0) {
		snprintf(cmd, sizeof(cmd), "EYES\n");
	} else if (strcasecmp(action, "clear") == 0) {
		snprintf(cmd, sizeof(cmd), "CLEAR\n");
	} else if (strcasecmp(action, "brightness") == 0) {
		if (argc < 3) {
			fprintf(stderr, "Error: brightness command requires value (0..200 or 'auto')\n");
			return 1;
		}
		snprintf(cmd, sizeof(cmd), "BRIGHTNESS %s\n", argv[2]);
	} else if (strcasecmp(action, "status") == 0) {
		snprintf(cmd, sizeof(cmd), "STATUS\n");
	} else {
		/* Send raw */
		snprintf(cmd, sizeof(cmd), "%s\n", action);
	}

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("socket");
		return 1;
	}

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "Error: cannot connect to atri-displayd (%s): %s\n",
			SOCK_PATH, strerror(errno));
		close(fd);
		return 1;
	}

	if (write(fd, cmd, strlen(cmd)) < 0) {
		perror("write");
		close(fd);
		return 1;
	}

	char resp[256];
	ssize_t n = read(fd, resp, sizeof(resp) - 1);
	if (n > 0) {
		resp[n] = '\0';
		printf("%s", resp);
	}
	close(fd);
	return 0;
}
