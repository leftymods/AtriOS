// SPDX-License-Identifier: GPL-2.0
/*
 * atri-buttons - AtriStation button tester.
 * Listens on all /dev/input/event* devices and reports KEY presses,
 * highlighting the board buttons (VOICECOMMAND / MICMUTE).
 *
 * Usage: atri-buttons [-t sec]   (default: run until Ctrl+C)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <linux/input.h>
#include <time.h>

struct key_name { unsigned short code; const char *name; };
static const struct key_name known[] = {
	{ KEY_VOICECOMMAND,	"VOICECOMMAND (function)" },
	{ KEY_MICMUTE,		"MICMUTE" },
	{ KEY_VOLUMEUP,		"VOLUMEUP" },
	{ KEY_VOLUMEDOWN,	"VOLUMEDOWN" },
	{ KEY_PLAYPAUSE,	"PLAYPAUSE" },
	{ KEY_POWER,		"POWER" },
};
static const char *keyname(unsigned short c)
{
	static char unk[32];
	for (size_t i = 0; i < sizeof(known)/sizeof(known[0]); i++)
		if (known[i].code == c) return known[i].name;
	snprintf(unk, sizeof(unk), "code=%u", c);
	return unk;
}

static int fds[64];
static char names[64][64];
static int nfds;
static int counts[64][2]; /* per-fd press/release counters */

static void scan_devices(void)
{
	DIR *d = opendir("/dev/input");
	struct dirent *de;
	char path[128], name[sizeof(names[0])];

	while ((de = readdir(d))) {
		if (strncmp(de->d_name, "event", 5)) continue;
		if (nfds >= 64) break;
		snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0) continue;
		if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0)
			name[0] = '\0';
		fds[nfds] = fd;
		snprintf(names[nfds], sizeof(names[nfds]), "%s (%s)",
			 de->d_name, name);
		nfds++;
	}
	closedir(d);
}

int main(int argc, char **argv)
{
	int timeout_s = argc > 1 ? atoi(argv[1]) : -1;

	scan_devices();
	if (!nfds) { fprintf(stderr, "no input devices\n"); return 1; }

	printf("== atri-buttons: listening on %d device(s) ==\n", nfds);
	for (int i = 0; i < nfds; i++)
		printf("  [%d] %s\n", i, names[i]);
	printf("Press the FUNCTION / MIC-MUTE buttons. Ctrl+C to stop.\n\n");

	time_t start = time(NULL);
	struct pollfd pfds[64];
	for (;;) {
		for (int i = 0; i < nfds; i++) { pfds[i].fd = fds[i]; pfds[i].events = POLLIN; }
		int r = poll(pfds, nfds, 200);
		if (r < 0 && errno != EINTR) break;

		for (int i = 0; i < nfds; i++) {
			if (!(pfds[i].revents & POLLIN)) continue;
			struct input_event ev;
			while (read(fds[i], &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
				if (ev.type != EV_KEY) continue;
				const char *state =
					ev.value == 1 ? "PRESS  " :
					ev.value == 0 ? "release" : "repeat ";
				if (ev.value == 1) counts[i][0]++;
				printf("[%s] %-24s %s\n", names[i],
				       keyname(ev.code), state);
			}
		}
		if (timeout_s > 0 && time(NULL) - start >= timeout_s) break;
	}

	printf("\n== summary ==\n");
	return 0;
}
