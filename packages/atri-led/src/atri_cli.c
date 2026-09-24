/*
 * atri_cli.c - Unified command-line interface for AtriOS smart speaker platform
 *
 * Provides a single umbrella command: `atri`
 *
 * Subcommands:
 *   atri status              Comprehensive system, audio, display, sensor & radio status
 *   atri sound [...]         Audio hardware diagnostics, tones, sweep, mic array (atri-sound-test)
 *   atri matrix [...]        25x16 LED screen controls, demo, tests, backlight (atri-matrix)
 *   atri led [...]           RGB ring animations, colors, effects (atri-led-ctl)
 *   atri als [...]           Ambient light sensor lux probe and watch (LTR-308ALS)
 *   atri autobrightness [...] Adaptive brightness regulator control (on/off/auto-off/status)
 *   atri display [...]       Front screen interface daemon client (atri-display)
 *   atri zigbee [...]        Zigbee coordinator management and firmware update (atri-zigbee)
 *   atri probe [...]         Hardware audit, PCBA EEPROM, I2C/SPI bus scan (atri-hwprobe)
 *   atri wifi [...]          Wireless diagnostics and status (atri-wifi-diag)
 *   atri volume [...]        Volume dial monitor and daemon (atrivolume)
 *   atri version             Display AtriOS platform version and build metadata
 *
 * Copyright (c) 2026 leftymods / AtriOS Project
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>

#define ATRIOS_VERSION "2.1.0-nebula"

#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_CYAN    "\033[1;36m"
#define COLOR_GREEN   "\033[1;32m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_RED     "\033[1;31m"
#define COLOR_MAGENTA "\033[1;35m"

static void print_banner(void)
{
	printf("%s", COLOR_CYAN);
	printf("   ___  __       _  ____  _____\n");
	printf("  / _ |/ /______(_)/ __ \\/ __/\n");
	printf(" / __ / __/ __/ / / /_/ /\\ \\   \n");
	printf("/_/ |_\\__/_/ /_/_/\\____/___/   \n");
	printf("%s", COLOR_RESET);
	printf(" %sAtriOS — Autonomous Open Smart Speaker Platform%s\n", COLOR_BOLD, COLOR_RESET);
	printf(" Release: %s (Linux mainline 6.18+)\n\n", ATRIOS_VERSION);
}

static void print_usage(const char *prog)
{
	print_banner();
	printf("%sUsage:%s %s <command> [arguments...]\n\n", COLOR_BOLD, COLOR_RESET, prog);
	printf("%sCommands:%s\n", COLOR_BOLD, COLOR_RESET);
	printf("  %smenu%s, %ssetup%s         Launch interactive TUI configurator (Wi-Fi, audio, LEDs)\n", COLOR_GREEN, COLOR_RESET, COLOR_GREEN, COLOR_RESET);
	printf("  %sonboard%s               Start phone onboarding (BLE stealth / Classic visible)\n", COLOR_GREEN, COLOR_RESET);
	printf("  %sstatus%s                Show comprehensive status of all hardware subsystems\n", COLOR_GREEN, COLOR_RESET);
	printf("  %sclock%s, %seyes%s          Display digital clock or expressive eyes on front matrix\n", COLOR_GREEN, COLOR_RESET, COLOR_GREEN, COLOR_RESET);
	printf("  %sbtaudio%s [on|off|stat] Bluetooth wireless speaker mode (A2DP sink)\n", COLOR_GREEN, COLOR_RESET);
	printf("  %ssound%s [subcmd...]     Audio tests, tone generator, frequency sweep, mics\n", COLOR_GREEN, COLOR_RESET);
	printf("  %smatrix%s [subcmd...]    25x16 LED screen: text, demo, test, on/off, brightness\n", COLOR_GREEN, COLOR_RESET);
	printf("  %sled%s [subcmd...]       24-RGB ring: animations, color, volume arc, off\n", COLOR_GREEN, COLOR_RESET);
	printf("  %sals%s [subcmd...]       Ambient light sensor (lux reading, watch mode)\n", COLOR_GREEN, COLOR_RESET);
	printf("  %sautobrightness%s [...]  Auto-brightness daemon control (status, on, off, auto-off)\n", COLOR_GREEN, COLOR_RESET);
	printf("  %sdisplay%s [subcmd...]   Front screen user interface daemon CLI (clock/eyes/temp)\n", COLOR_GREEN, COLOR_RESET);
	printf("  %stz%s [Europe/Moscow]    Get or set system timezone\n", COLOR_GREEN, COLOR_RESET);
	printf("  %slang%s [ru|en]          Get or set system locale\n", COLOR_GREEN, COLOR_RESET);
	printf("  %szigbee%s [subcmd...]    Tuya TZ9213 radio: reset, firmware flash, listen\n", COLOR_GREEN, COLOR_RESET);
	printf("  %sprobe%s [subcmd...]     Board probe: PCBA EEPROM, I2C/SPI bus scan, buttons\n", COLOR_GREEN, COLOR_RESET);
	printf("  %swifi%s [subcmd...]      Wireless network diagnostics and status\n", COLOR_GREEN, COLOR_RESET);
	printf("  %svolume%s [subcmd...]    Volume encoder daemon and monitor\n", COLOR_GREEN, COLOR_RESET);
	printf("  %sversion%s               Show platform and release version\n\n", COLOR_GREEN, COLOR_RESET);
	printf("Run '%s <command> --help' for options specific to any subcommand.\n", prog);
}

static bool check_path(const char *path)
{
	return access(path, F_OK) == 0;
}

static int read_sysfs_string(const char *path, char *out, size_t maxlen)
{
	FILE *f = fopen(path, "r");
	if (!f) return -1;
	if (!fgets(out, maxlen, f)) { fclose(f); return -1; }
	fclose(f);
	out[strcspn(out, "\r\n")] = '\0';
	return 0;
}

static void show_status(void)
{
	print_banner();

	/* OS & System */
	struct utsname u;
	uname(&u);
	struct sysinfo s;
	sysinfo(&s);

	printf("%s[%sSYSTEM%s]%s\n", COLOR_BOLD, COLOR_CYAN, COLOR_RESET, COLOR_BOLD);
	printf("  Kernel:     %s %s (%s)\n", u.sysname, u.release, u.machine);
	printf("  Uptime:     %ld hours, %ld mins\n", s.uptime / 3600, (s.uptime % 3600) / 60);
	printf("  Memory:     %ld MB used / %ld MB total\n", (s.totalram - s.freeram) / 1024 / 1024, s.totalram / 1024 / 1024);

	/* Sound Subsystem */
	printf("\n%s[%sSOUND SUBSYSTEM%s]%s\n", COLOR_BOLD, COLOR_CYAN, COLOR_RESET, COLOR_BOLD);
	bool snd_card = check_path("/proc/asound/ATRISTATION");
	printf("  ALSA Card:  %s%s%s (ATRISTATION)\n",
	       snd_card ? COLOR_GREEN : COLOR_RED,
	       snd_card ? "DETECTED & READY" : "NOT FOUND / DEFERRED",
	       COLOR_RESET);

	bool pcm_play = check_path("/dev/snd/pcmC0D0p");
	bool pcm_cap = check_path("/dev/snd/pcmC0D0c") || check_path("/dev/snd/pcmC0D1c");
	printf("  Endpoints:  Playback: %s%s%s | Capture: %s%s%s\n",
	       pcm_play ? COLOR_GREEN : COLOR_YELLOW, pcm_play ? "pcmC0D0p (OK)" : "N/A", COLOR_RESET,
	       pcm_cap ? COLOR_GREEN : COLOR_YELLOW, pcm_cap ? "4-channel DMIC (OK)" : "N/A", COLOR_RESET);

	bool pipewire_ok = (system("pidof pipewire >/dev/null 2>&1") == 0);
	printf("  PipeWire:   %s%s%s (2.1 Crossover + WebRTC AEC)\n",
	       pipewire_ok ? COLOR_GREEN : COLOR_YELLOW,
	       pipewire_ok ? "RUNNING" : "STOPPED / INACTIVE",
	       COLOR_RESET);

	/* Display & Screen Matrix */
	printf("\n%s[%sDISPLAY & LEDS%s]%s\n", COLOR_BOLD, COLOR_CYAN, COLOR_RESET, COLOR_BOLD);
	bool fb_ok = check_path("/dev/fb0");
	char br_buf[32] = "N/A";
	read_sysfs_string("/sys/class/backlight/gowin-backlight/brightness", br_buf, sizeof(br_buf));
	if (!strcmp(br_buf, "N/A")) {
		read_sysfs_string("/sys/class/backlight/atri_led_panel/brightness", br_buf, sizeof(br_buf));
	}
	printf("  Matrix:     %s%s%s | Backlight: %s/200\n",
	       fb_ok ? COLOR_GREEN : COLOR_YELLOW,
	       fb_ok ? "25x16 Gowin FPGA (/dev/fb0)" : "NO FRAMEBUFFER",
	       COLOR_RESET, br_buf);

	bool atriled_sock = check_path("/run/atriled.sock");
	printf("  RGB Ring:   %s%s%s (24-channel IS31FL3236)\n",
	       atriled_sock ? COLOR_GREEN : COLOR_YELLOW,
	       atriled_sock ? "DAEMON ACTIVE (/run/atriled.sock)" : "DAEMON NOT RUNNING",
	       COLOR_RESET);

	/* Ambient Light Sensor & Auto-Brightness */
	printf("\n%s[%sSENSORS & AUTO-BRIGHTNESS%s]%s\n", COLOR_BOLD, COLOR_CYAN, COLOR_RESET, COLOR_BOLD);
	char lux_str[32] = "N/A";
	if (read_sysfs_string("/sys/bus/iio/devices/iio:device0/in_illuminance_raw", lux_str, sizeof(lux_str)) != 0) {
		read_sysfs_string("/sys/bus/iio/devices/iio:device0/in_illuminance_input", lux_str, sizeof(lux_str));
	}
	printf("  ALS Sensor: %s (LTR-308ALS: %s lux)\n",
	       strcmp(lux_str, "N/A") ? "ONLINE" : "OFFLINE", lux_str);

	bool auto_status = check_path("/run/atri_autobrightness.status");
	printf("  Auto-Light: %s%s%s (Auto-off in dark enabled)\n",
	       auto_status ? COLOR_GREEN : COLOR_YELLOW,
	       auto_status ? "ACTIVE REGULATION" : "STANDBY / INACTIVE",
	       COLOR_RESET);

	/* Wireless & Radios */
	printf("\n%s[%sCOMMUNICATIONS%s]%s\n", COLOR_BOLD, COLOR_CYAN, COLOR_RESET, COLOR_BOLD);
	bool wlan0 = check_path("/sys/class/net/wlan0");
	bool hci0 = check_path("/sys/class/bluetooth/hci0");
	bool zigbee = check_path("/dev/ttyAML2") || check_path("/dev/ttyZigbee");

	printf("  WiFi:       %s%s%s (RTL8822CS SDIO)\n",
	       wlan0 ? COLOR_GREEN : COLOR_RED,
	       wlan0 ? "wlan0 UP" : "INTERFACE DOWN",
	       COLOR_RESET);
	printf("  Bluetooth:  %s%s%s (RTL8822CS UART)\n",
	       hci0 ? COLOR_GREEN : COLOR_YELLOW,
	       hci0 ? "hci0 UP" : "DOWN",
	       COLOR_RESET);
	printf("  Zigbee:     %s%s%s (Tuya TZ9213 /dev/ttyAML2)\n",
	       zigbee ? COLOR_GREEN : COLOR_YELLOW,
	       zigbee ? "PORT READY" : "PORT ABSENT",
	       COLOR_RESET);
	printf("\n");
}

static void handle_autobrightness(int argc, char **argv)
{
	if (argc <= 2) {
		/* Show status */
		system("atri-autobrightness --status");
		return;
	}
	const char *action = argv[2];
	if (!strcmp(action, "on") || !strcmp(action, "start") || !strcmp(action, "enable")) {
		printf("Starting atri-autobrightness service...\n");
		system("systemctl start atri-autobrightness.service 2>/dev/null || atri-autobrightness --daemon &");
	} else if (!strcmp(action, "off") || !strcmp(action, "stop") || !strcmp(action, "disable")) {
		printf("Stopping atri-autobrightness service...\n");
		system("systemctl stop atri-autobrightness.service 2>/dev/null || pkill -f atri-autobrightness");
	} else if (!strcmp(action, "status")) {
		system("atri-autobrightness --status");
	} else if (!strcmp(action, "once")) {
		system("atri-autobrightness --once");
	} else if (!strcmp(action, "auto-off")) {
		if (argc >= 4) {
			int val = (!strcmp(argv[3], "on") || !strcmp(argv[3], "1")) ? 1 : 0;
			char cmd[128];
			snprintf(cmd, sizeof(cmd), "atri-autobrightness --auto-off %d", val);
			system(cmd);
		} else {
			printf("Usage: atri autobrightness auto-off <on|off>\n");
		}
	} else {
		char cmd[256] = "atri-autobrightness";
		for (int i = 2; i < argc; i++) {
			strncat(cmd, " ", sizeof(cmd) - strlen(cmd) - 1);
			strncat(cmd, argv[i], sizeof(cmd) - strlen(cmd) - 1);
		}
		system(cmd);
	}
}

static void handle_als(int argc, char **argv)
{
	if (argc <= 2) {
		system("atri-hwprobe als --once");
		return;
	}
	if (!strcmp(argv[2], "watch")) {
		system("atri-hwprobe als --watch");
	} else if (!strcmp(argv[2], "autobrightness")) {
		handle_autobrightness(argc - 1, argv + 1);
	} else {
		system("atri-hwprobe als");
	}
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		print_usage(argv[0]);
		return 0;
	}

	const char *cmd = argv[1];

	if (!strcmp(cmd, "status") || !strcmp(cmd, "info")) {
		show_status();
		return 0;
	}
	if (!strcmp(cmd, "version") || !strcmp(cmd, "--version") || !strcmp(cmd, "-v")) {
		printf("AtriOS Platform Utility (atri) v%s\n", ATRIOS_VERSION);
		printf("Target hardware: AtriStation (SM1 / S905X3) & Station Max (G12A / S905X2)\n");
		return 0;
	}
	if (!strcmp(cmd, "help") || !strcmp(cmd, "--help") || !strcmp(cmd, "-h")) {
		print_usage(argv[0]);
		return 0;
	}

	if (!strcmp(cmd, "menu") || !strcmp(cmd, "setup") || !strcmp(cmd, "tui")) {
		argv[1] = "atri-tui";
		execvp("atri-tui", &argv[1]);
		perror("execvp atri-tui");
		return 1;
	}
	if (!strcmp(cmd, "onboard") || !strcmp(cmd, "phone-setup")) {
		argv[1] = "atri-onboard";
		execvp("atri-onboard", &argv[1]);
		perror("execvp atri-onboard");
		return 1;
	}

	if (!strcmp(cmd, "clock") || !strcmp(cmd, "time")) {
		char *new_argv[] = { "atri-display", "clock", NULL };
		execvp("atri-display", new_argv);
		perror("execvp atri-display");
		return 1;
	}
	if (!strcmp(cmd, "eyes")) {
		char *new_argv[] = { "atri-display", "eyes", NULL };
		execvp("atri-display", new_argv);
		perror("execvp atri-display");
		return 1;
	}
	if (!strcmp(cmd, "temp")) {
		const char *tval = (argc >= 3) ? argv[2] : "+22";
		char *new_argv[] = { "atri-display", "temp", (char*)tval, NULL };
		execvp("atri-display", new_argv);
		perror("execvp atri-display");
		return 1;
	}
	if (!strcmp(cmd, "msg")) {
		argv[1] = "msg";
		char *new_argv[32];
		new_argv[0] = "atri-display";
		for (int i = 1; i < argc && i < 30; i++) new_argv[i] = argv[i];
		new_argv[argc] = NULL;
		execvp("atri-display", new_argv);
		perror("execvp atri-display");
		return 1;
	}
	if (!strcmp(cmd, "btaudio")) {
		if (argc >= 3 && (!strcmp(argv[2], "off") || !strcmp(argv[2], "stop") || !strcmp(argv[2], "disable"))) {
			system("hciconfig hci0 noscan 2>/dev/null || true; bluetoothctl discoverable off >/dev/null 2>&1 &");
			printf("Bluetooth audio discoverability disabled.\n");
		} else if (argc >= 3 && !strcmp(argv[2], "status")) {
			system("bluetoothctl devices Connected 2>/dev/null || bluetoothctl devices");
		} else {
			system("hciconfig hci0 up 2>/dev/null || true; "
			       "hciconfig hci0 piscan 2>/dev/null || true; "
			       "hciconfig hci0 class 0x200414 2>/dev/null || true; "
			       "bluetoothctl discoverable on >/dev/null 2>&1 & "
			       "bluetoothctl pairable on >/dev/null 2>&1 &");
			printf("Bluetooth A2DP Audio Sink mode enabled. Device is discoverable as a wireless speaker.\n");
		}
		return 0;
	}
	if (!strcmp(cmd, "tz") || !strcmp(cmd, "timezone")) {
		if (argc >= 3) {
			char sys_cmd[128];
			snprintf(sys_cmd, sizeof(sys_cmd), "timedatectl set-timezone '%s'", argv[2]);
			system(sys_cmd);
		} else {
			system("timedatectl status | grep 'Time zone'");
		}
		return 0;
	}
	if (!strcmp(cmd, "lang") || !strcmp(cmd, "locale")) {
		if (argc >= 3) {
			const char *target = (!strcmp(argv[2], "ru") || strstr(argv[2], "ru")) ? "ru_RU.UTF-8" : "en_US.UTF-8";
			char sys_cmd[128];
			snprintf(sys_cmd, sizeof(sys_cmd), "localectl set-locale LANG='%s'", target);
			system(sys_cmd);
			printf("System locale set to %s\n", target);
		} else {
			system("localectl status");
		}
		return 0;
	}

	if (!strcmp(cmd, "sound")) {
		argv[1] = "atri-sound-test";
		execvp("atri-sound-test", &argv[1]);
		perror("execvp atri-sound-test");
		return 1;
	}
	if (!strcmp(cmd, "matrix")) {
		argv[1] = "atri-matrix";
		execvp("atri-matrix", &argv[1]);
		perror("execvp atri-matrix");
		return 1;
	}
	if (!strcmp(cmd, "led")) {
		argv[1] = "atri-led-ctl";
		execvp("atri-led-ctl", &argv[1]);
		perror("execvp atri-led-ctl");
		return 1;
	}
	if (!strcmp(cmd, "display")) {
		argv[1] = "atri-display";
		execvp("atri-display", &argv[1]);
		perror("execvp atri-display");
		return 1;
	}
	if (!strcmp(cmd, "zigbee")) {
		argv[1] = "atri-zigbee";
		execvp("atri-zigbee", &argv[1]);
		perror("execvp atri-zigbee");
		return 1;
	}
	if (!strcmp(cmd, "probe")) {
		argv[1] = "atri-hwprobe";
		execvp("atri-hwprobe", &argv[1]);
		perror("execvp atri-hwprobe");
		return 1;
	}
	if (!strcmp(cmd, "wifi")) {
		argv[1] = "atri-wifi-diag";
		execvp("atri-wifi-diag", &argv[1]);
		perror("execvp atri-wifi-diag");
		return 1;
	}
	if (!strcmp(cmd, "volume")) {
		argv[1] = "atrivolume";
		execvp("atrivolume", &argv[1]);
		perror("execvp atrivolume");
		return 1;
	}
	if (!strcmp(cmd, "als")) {
		handle_als(argc, argv);
		return 0;
	}
	if (!strcmp(cmd, "autobrightness")) {
		handle_autobrightness(argc, argv);
		return 0;
	}

	fprintf(stderr, "Unknown command '%s'. Run '%s help' for available commands.\n", cmd, argv[0]);
	return 1;
}
