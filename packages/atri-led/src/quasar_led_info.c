#include "quasar_screen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct screen_info {
	char hostname[32];
	char ip[32];
	char uptime[32];
	char cpu[32];
	char mem[32];
};

static int read_line(const char *path, char *buf, int size)
{
	FILE *f = fopen(path, "r");
	if (!f) return -1;
	if (!fgets(buf, size, f)) { fclose(f); return -1; }
	fclose(f);
	int len = strlen(buf);
	while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == ' ')) buf[--len] = 0;
	return len;
}

static void get_info(struct screen_info *info)
{
	memset(info, 0, sizeof(*info));

	read_line("/proc/sys/kernel/hostname", info->hostname, sizeof(info->hostname));

	FILE *f = popen("ip -4 addr show | grep inet | head -1 | awk '{print $2}'", "r");
	if (f) {
		if (fgets(info->ip, sizeof(info->ip), f)) {
			int len = strlen(info->ip);
			if (len > 0 && info->ip[len-1] == '\n') info->ip[--len] = 0;
			char *slash = strchr(info->ip, '/');
			if (slash) *slash = 0;
		}
		pclose(f);
	}
	if (!info->ip[0]) strcpy(info->ip, "no network");

	read_line("/proc/uptime", info->uptime, sizeof(info->uptime));
	char *dot = strchr(info->uptime, '.');
	if (dot) *dot = 0;
	int secs = atoi(info->uptime);
	int h = secs / 3600, m = (secs % 3600) / 60, s = secs % 60;
	snprintf(info->uptime, sizeof(info->uptime), "%02d:%02d:%02d", h, m, s);

	f = popen("grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ *//' | cut -d' ' -f1-2", "r");
	if (f) {
		if (fgets(info->cpu, sizeof(info->cpu), f)) {
			int len = strlen(info->cpu);
			if (len > 0 && info->cpu[len-1] == '\n') info->cpu[--len] = 0;
		}
		pclose(f);
	}
	if (!info->cpu[0]) {
		f = popen("grep -m1 'Hardware' /proc/cpuinfo | cut -d: -f2 | sed 's/^ *//'", "r");
		if (f) {
			fgets(info->cpu, sizeof(info->cpu), f);
			int len = strlen(info->cpu);
			if (len > 0 && info->cpu[len-1] == '\n') info->cpu[--len] = 0;
			pclose(f);
		}
	}
	if (!info->cpu[0]) strcpy(info->cpu, "S905X3");

	f = popen("free -m | grep Mem | awk '{print $3\"/\"$2\"M\"}'", "r");
	if (f) {
		fgets(info->mem, sizeof(info->mem), f);
		int len = strlen(info->mem);
		if (len > 0 && info->mem[len-1] == '\n') info->mem[--len] = 0;
		pclose(f);
	}
	if (!info->mem[0]) strcpy(info->mem, "?M");
}

int main(int argc, char *argv[])
{
	int loop = 0;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-l") == 0) loop = 1;
	}

	quasar_screen_t scr;
	if (screen_open(&scr, NULL) < 0) return 1;
	screen_reset(&scr);

	do {
		struct screen_info info;
		get_info(&info);

		screen_clear(&scr);
		screen_text(&scr, 0, 0, info.hostname, 1);
		screen_text(&scr, 0, 8, info.ip, 1);

		char line2[32];
		snprintf(line2, sizeof(line2), "up %s", info.uptime);
		screen_text(&scr, 0, 16, line2, 0);
		screen_flush(&scr);

		if (!loop) break;
		sleep(5);
	} while (loop);

	screen_close(&scr);
	return 0;
}
