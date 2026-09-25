/*
 * atri_autobrightness.c - Adaptive brightness controller for AtriOS
 *
 * Automatically calibrates and regulates LED matrix and RGB ring brightness
 * based on ambient light sensor readings (LTR-308ALS via IIO).
 *
 * Features:
 *   - Auto-detects IIO ambient light sensor (in_illuminance_raw / input)
 *   - Auto-detects Gowin LED matrix backlight sysfs endpoint
 *   - Communicates with atriled daemon via /run/atriled.sock
 *   - Exponential Moving Average (EMA) filtering to prevent flicker
 *   - Piecewise-smooth calibration curve (night -> indoor -> bright daylight)
 *   - Auto-Off mode: turns off matrix and ring completely in total darkness (< 2 lux)
 *   - Configurable via /etc/atri/autobrightness.conf
 *   - Live status reporting via /run/atri_autobrightness.status
 *
 * Copyright (c) 2026 leftymods / AtriOS Project
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <math.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#define DEFAULT_CONF_PATH "/etc/atri/autobrightness.conf"
#define STATUS_PATH       "/run/atri_autobrightness.status"
#define ATRILED_SOCK      "/run/atriled.sock"

struct config {
	int enabled;
	int darkness_auto_off;
	double darkness_lux;
	int min_matrix;
	int max_matrix;
	int min_ring;
	int max_ring;
	int poll_interval_ms;
	double smooth_alpha;
};

static struct config cfg = {
	.enabled = 1,
	.darkness_auto_off = 1,
	.darkness_lux = 2.0,
	.min_matrix = 5,
	.max_matrix = 200,
	.min_ring = 5,
	.max_ring = 100,
	.poll_interval_ms = 1000,
	.smooth_alpha = 0.25,
};

static volatile bool running = true;
static void sig_handler(int sig) { (void)sig; running = false; }

static char iio_lux_path[256] = {0};
static char backlight_path[256] = {0};

/* Locate IIO ambient light sensor node */
static bool find_iio_sensor(void)
{
	DIR *d = opendir("/sys/bus/iio/devices");
	if (!d) return false;
	struct dirent *de;
	while ((de = readdir(d))) {
		if (strncmp(de->d_name, "iio:device", 10) != 0) continue;

		char raw[256], input[256];
		snprintf(raw, sizeof(raw), "/sys/bus/iio/devices/%s/in_illuminance_raw", de->d_name);
		snprintf(input, sizeof(input), "/sys/bus/iio/devices/%s/in_illuminance_input", de->d_name);

		if (access(input, R_OK) == 0) {
			strncpy(iio_lux_path, input, sizeof(iio_lux_path) - 1);
			closedir(d);
			return true;
		}
		if (access(raw, R_OK) == 0) {
			strncpy(iio_lux_path, raw, sizeof(iio_lux_path) - 1);
			closedir(d);
			return true;
		}
	}
	closedir(d);
	return false;
}

/* Locate Gowin LED matrix backlight sysfs node */
static bool find_backlight(void)
{
	const char *candidates[] = {
		"/sys/class/backlight/gowin-backlight/brightness",
		"/sys/class/backlight/atri_led_panel/brightness",
		"/sys/class/backlight/gowin_led/brightness",
		"/sys/class/backlight/led_screen/brightness",
		NULL
	};
	for (int i = 0; candidates[i]; i++) {
		if (access(candidates[i], W_OK) == 0) {
			strncpy(backlight_path, candidates[i], sizeof(backlight_path) - 1);
			return true;
		}
	}
	return false;
}

static double read_lux(void)
{
	if (!iio_lux_path[0]) {
		if (!find_iio_sensor()) return -1.0;
	}
	FILE *f = fopen(iio_lux_path, "r");
	if (!f) return -1.0;
	double val = 0.0;
	if (fscanf(f, "%lf", &val) != 1) val = -1.0;
	fclose(f);
	return val;
}

static int set_matrix_brightness(int val)
{
	if (!backlight_path[0]) {
		if (!find_backlight()) return -1;
	}
	if (val < 0) val = 0;
	if (val > 200) val = 200;

	FILE *f = fopen(backlight_path, "w");
	if (!f) return -1;
	fprintf(f, "%d\n", val);
	fclose(f);
	return 0;
}

static int set_ring_cmd(const char *cmd)
{
	int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0) return -1;
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, ATRILED_SOCK, sizeof(addr.sun_path) - 1);
	int ret = -1;
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
		if (write(fd, cmd, strlen(cmd)) > 0) ret = 0;
	}
	close(fd);
	return ret;
}

static int set_ring_brightness(int val)
{
	if (val < 0) val = 0;
	if (val > 100) val = 100;
	char cmd[32];
	snprintf(cmd, sizeof(cmd), "brightness %d", val);
	return set_ring_cmd(cmd);
}

/*
 * Piecewise mapping curve: lux -> target brightness
 * Range:
 *   Matrix: 0..200
 *   Ring:   0..100
 */
static void calculate_targets(double lux, int *out_matrix, int *out_ring, bool *out_is_dark)
{
	if (lux < cfg.darkness_lux) {
		*out_is_dark = true;
		if (cfg.darkness_auto_off) {
			*out_matrix = 0;
			*out_ring = 0;
			return;
		}
		*out_matrix = cfg.min_matrix;
		*out_ring = cfg.min_ring;
		return;
	}
	*out_is_dark = false;

	/* Smooth logarithmic-linear scaling curve */
	double norm;
	if (lux < 10.0) {
		/* Dim ambient (2 .. 10 lux) */
		norm = (lux - cfg.darkness_lux) / (10.0 - cfg.darkness_lux);
		*out_matrix = cfg.min_matrix + (int)(norm * 25.0);
		*out_ring = cfg.min_ring + (int)(norm * 15.0);
	} else if (lux < 60.0) {
		/* Cozy room light (10 .. 60 lux) */
		norm = (lux - 10.0) / 50.0;
		*out_matrix = 30 + (int)(norm * 50.0);
		*out_ring = 20 + (int)(norm * 25.0);
	} else if (lux < 250.0) {
		/* Daytime indoor (60 .. 250 lux) */
		norm = (lux - 60.0) / 190.0;
		*out_matrix = 80 + (int)(norm * 70.0);
		*out_ring = 45 + (int)(norm * 35.0);
	} else {
		/* Bright direct light (> 250 lux) */
		norm = (lux - 250.0) / 750.0;
		if (norm > 1.0) norm = 1.0;
		*out_matrix = 150 + (int)(norm * 50.0);
		*out_ring = 80 + (int)(norm * 20.0);
	}

	if (*out_matrix > cfg.max_matrix) *out_matrix = cfg.max_matrix;
	if (*out_matrix < cfg.min_matrix) *out_matrix = cfg.min_matrix;
	if (*out_ring > cfg.max_ring) *out_ring = cfg.max_ring;
	if (*out_ring < cfg.min_ring) *out_ring = cfg.min_ring;
}

static void load_config(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) return;
	char line[128];
	while (fgets(line, sizeof(line), f)) {
		char key[64], val[64];
		if (sscanf(line, " %63[^=]=%63s", key, val) == 2) {
			if (!strcmp(key, "enabled")) cfg.enabled = atoi(val);
			else if (!strcmp(key, "darkness_auto_off")) cfg.darkness_auto_off = atoi(val);
			else if (!strcmp(key, "darkness_lux")) cfg.darkness_lux = atof(val);
			else if (!strcmp(key, "min_matrix")) cfg.min_matrix = atoi(val);
			else if (!strcmp(key, "max_matrix")) cfg.max_matrix = atoi(val);
			else if (!strcmp(key, "min_ring")) cfg.min_ring = atoi(val);
			else if (!strcmp(key, "max_ring")) cfg.max_ring = atoi(val);
			else if (!strcmp(key, "poll_interval_ms")) cfg.poll_interval_ms = atoi(val);
			else if (!strcmp(key, "smooth_alpha")) cfg.smooth_alpha = atof(val);
		}
	}
	fclose(f);
}

static void write_status_file(double raw_lux, double smooth_lux, int cur_matrix, int cur_ring, bool is_dark)
{
	FILE *f = fopen(STATUS_PATH, "w");
	if (!f) return;
	fprintf(f, "raw_lux=%.2f\n", raw_lux);
	fprintf(f, "smooth_lux=%.2f\n", smooth_lux);
	fprintf(f, "matrix_brightness=%d\n", cur_matrix);
	fprintf(f, "ring_brightness=%d\n", cur_ring);
	fprintf(f, "state=%s\n", is_dark ? (cfg.darkness_auto_off ? "DARK_OFF" : "DIM") : "ACTIVE");
	fprintf(f, "auto_off=%d\n", cfg.darkness_auto_off);
	fprintf(f, "enabled=%d\n", cfg.enabled);
	fclose(f);
}

static void print_status(void)
{
	FILE *f = fopen(STATUS_PATH, "r");
	if (!f) {
		printf("Autobrightness daemon status: NOT RUNNING (no %s)\n", STATUS_PATH);
		double lux = read_lux();
		if (lux >= 0) printf("Direct sensor probe: %.2f lux\n", lux);
		return;
	}
	printf("=== AtriOS Auto-Brightness Status ===\n");
	char line[128];
	while (fgets(line, sizeof(line), f)) {
		printf("  %s", line);
	}
	fclose(f);
}

int main(int argc, char **argv)
{
	bool daemon_mode = false;
	bool once_mode = false;
	const char *conf_path = DEFAULT_CONF_PATH;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--daemon") || !strcmp(argv[i], "-d")) daemon_mode = true;
		else if (!strcmp(argv[i], "--status") || !strcmp(argv[i], "status")) {
			print_status();
			return 0;
		}
		else if (!strcmp(argv[i], "--once")) once_mode = true;
		else if (!strcmp(argv[i], "--auto-off")) {
			if (i + 1 < argc) {
				int val = atoi(argv[++i]);
				printf("Setting darkness_auto_off=%d\n", val);
				cfg.darkness_auto_off = val;
			}
		}
		else if (!strcmp(argv[i], "--config") && i + 1 < argc) {
			conf_path = argv[++i];
		}
		else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
			printf("Usage: atri-autobrightness [options]\n");
			printf("  --daemon, -d        Run as persistent background regulator\n");
			printf("  --status            Display live status and lux reading\n");
			printf("  --once              Sample once and set brightness\n");
			printf("  --auto-off <0|1>    Toggle complete shutdown in dark (< 2 lux)\n");
			printf("  --config <path>     Path to autobrightness.conf\n");
			return 0;
		}
	}

	load_config(conf_path);

	if (!find_iio_sensor()) {
		fprintf(stderr, "atri-autobrightness: WARNING - LTR-308ALS IIO sensor not detected yet\n");
	}
	find_backlight();

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	double smooth_lux = -1.0;
	int cur_matrix = -1;
	int cur_ring = -1;
	bool was_dark = false;

	printf("atri-autobrightness: started (darkness_auto_off=%d, threshold=%.1f lux)\n",
	       cfg.darkness_auto_off, cfg.darkness_lux);

	while (running) {
		double raw_lux = read_lux();
		if (raw_lux < 0.0) raw_lux = 0.0;

		if (smooth_lux < 0.0) smooth_lux = raw_lux;
		else smooth_lux = (cfg.smooth_alpha * raw_lux) + ((1.0 - cfg.smooth_alpha) * smooth_lux);

		int target_matrix = 0;
		int target_ring = 0;
		bool is_dark = false;

		calculate_targets(smooth_lux, &target_matrix, &target_ring, &is_dark);

		if (is_dark && cfg.darkness_auto_off) {
			if (!was_dark) {
				set_matrix_brightness(0);
				set_ring_cmd("off");
				cur_matrix = 0;
				cur_ring = 0;
				was_dark = true;
			}
		} else {
			was_dark = false;
			/* Smooth ramp to target brightness */
			if (cur_matrix < 0) cur_matrix = target_matrix;
			else {
				int diff = target_matrix - cur_matrix;
				if (abs(diff) <= 4) cur_matrix = target_matrix;
				else cur_matrix += (diff > 0 ? 4 : -4);
			}

			if (cur_ring < 0) cur_ring = target_ring;
			else {
				int diff_ring = target_ring - cur_ring;
				if (abs(diff_ring) <= 3) cur_ring = target_ring;
				else cur_ring += (diff_ring > 0 ? 3 : -3);
			}

			set_matrix_brightness(cur_matrix);
			set_ring_brightness(cur_ring);
		}

		write_status_file(raw_lux, smooth_lux, cur_matrix, cur_ring, is_dark);

		if (once_mode || !daemon_mode) {
			printf("Lux: raw=%.2f smooth=%.2f -> Matrix: %d/200, Ring: %d%% (%s)\n",
			       raw_lux, smooth_lux, cur_matrix, cur_ring,
			       is_dark ? (cfg.darkness_auto_off ? "AUTO-OFF" : "DIM") : "ACTIVE");
			break;
		}

		usleep(cfg.poll_interval_ms * 1000);
	}

	unlink(STATUS_PATH);
	return 0;
}
