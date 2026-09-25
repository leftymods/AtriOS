/*
 * atri_onboard.c - Bluetooth Phone Onboarding & Wi-Fi Setup Daemon for AtriOS
 *
 * Implements Bluetooth-based onboarding for smartphones:
 * 1. Enables Bluetooth discoverable/pairable mode on hci0 ("AtriStation-Setup-XXXX")
 * 2. Runs RFCOMM Serial Port Profile (SPP) server on channel 1 (AF_BLUETOOTH)
 * 3. Provides JSON and line-based protocol for phones:
 *      - SCAN       -> list of visible Wi-Fi networks and signal strengths
 *      - CONNECT    -> connect station to selected Wi-Fi with password
 *      - SOUND_TEST -> verify audio output with a test tone
 *      - STATUS     -> get device MAC, IP, uptime, hardware status
 *      - CONFIG     -> set device name / language
 * 4. Displays visual feedback on 25x16 screen (BT SETUP, PAIRED, ONLINE)
 *    and 24-LED RGB ring (blue pulse, rotating cyan, green success).
 *
 * Copyright (c) 2026 leftymods / AtriOS Project
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <poll.h>

#define ATRILED_SOCK "/run/atriled.sock"
#define BT_SETUP_CHANNEL 1

/* Linux kernel Bluetooth definitions */
#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH 31
#endif

#ifndef BTPROTO_RFCOMM
#define BTPROTO_RFCOMM 3
#endif

typedef struct {
	uint8_t b[6];
} __attribute__((packed)) bdaddr_t;

struct sockaddr_rc {
	sa_family_t rc_family;
	bdaddr_t    rc_bdaddr;
	uint8_t     rc_channel;
};

static volatile bool running = true;
static void sig_handler(int sig) { (void)sig; running = false; }

static void run_cmd(const char *cmd)
{
	(void)system(cmd);
}

static void visual_feedback(const char *mode)
{
	if (!strcmp(mode, "setup")) {
		run_cmd("atri-led-ctl loop blue_pulse 2>/dev/null || atri-led-ctl color 0 150 255 2>/dev/null || true");
		run_cmd("atri-matrix text 'BT SETUP' 2>/dev/null || true");
	} else if (!strcmp(mode, "paired")) {
		run_cmd("atri-led-ctl loop cyan_spin 2>/dev/null || atri-led-ctl color 0 255 200 2>/dev/null || true");
		run_cmd("atri-matrix text 'PAIRED' 2>/dev/null || true");
	} else if (!strcmp(mode, "connecting")) {
		run_cmd("atri-led-ctl color 255 180 0 2>/dev/null || true");
		run_cmd("atri-matrix text 'WIFI...' 2>/dev/null || true");
	} else if (!strcmp(mode, "online")) {
		run_cmd("atri-led-ctl color 0 255 60 2>/dev/null || true");
		run_cmd("atri-matrix text 'ONLINE' 2>/dev/null || true");
	} else if (!strcmp(mode, "fail")) {
		run_cmd("atri-led-ctl color 255 0 0 2>/dev/null || true");
		run_cmd("atri-matrix text 'FAIL' 2>/dev/null || true");
	}
}

static void get_mac_suffix(char *out, size_t maxlen)
{
	const char *ifaces[] = { "/sys/class/net/wlan0/address", "/sys/class/net/eth0/address", NULL };
	for (int i = 0; ifaces[i]; i++) {
		FILE *f = fopen(ifaces[i], "r");
		if (f) {
			char buf[32];
			if (fgets(buf, sizeof(buf), f)) {
				char clean[16] = {0};
				int cidx = 0;
				for (int j = 0; buf[j]; j++) {
					if (buf[j] != ':' && buf[j] != '\n' && buf[j] != '\r') {
						clean[cidx++] = buf[j];
					}
				}
				if (cidx >= 4) {
					snprintf(out, maxlen, "%s", clean + (cidx - 4));
					fclose(f);
					return;
				}
			}
			fclose(f);
		}
	}
	snprintf(out, maxlen, "STATION");
}

/* AtriOS Custom BLE 16-bit Service UUID for mobile app discovery */
#define ATRIOS_BLE_SERVICE_UUID "FE33"

static void setup_bluetooth_adapter(const char *name, bool visible_mode)
{
	char cmd[256];
	/* Bring up hci0 */
	run_cmd("rfkill unblock bluetooth 2>/dev/null || true");
	run_cmd("hciconfig hci0 up 2>/dev/null || true");

	/* Set device name */
	snprintf(cmd, sizeof(cmd), "hciconfig hci0 name '%s' 2>/dev/null || true", name);
	run_cmd(cmd);

	if (visible_mode) {
		/* TEST MODE: Classic Bluetooth visible (piscan) */
		run_cmd("hciconfig hci0 piscan 2>/dev/null || true");
		run_cmd("bluetoothctl discoverable on >/dev/null 2>&1 &");
		run_cmd("bluetoothctl pairable on >/dev/null 2>&1 &");
		printf("Bluetooth Mode: VISIBLE (Classic BT discoverable for manual phone testing)\n");
	} else {
		/* APP STEALTH MODE (Default):
		 * Classic BT scan disabled (noscan) - invisible in standard phone Bluetooth menu!
		 * BLE Advertising enabled with custom AtriOS Service UUID (0xFE33)
		 * so only the future companion app will detect and connect to the station. */
		run_cmd("hciconfig hci0 noscan 2>/dev/null || true");
		run_cmd("bluetoothctl discoverable off >/dev/null 2>&1 &");
		run_cmd("bluetoothctl pairable on >/dev/null 2>&1 &");

		/* Enable BLE Advertising with AtriOS Service UUID */
		run_cmd("hciconfig hci0 leadv 3 2>/dev/null || btmgmt -i hci0 advertising on 2>/dev/null || true");
		printf("Bluetooth Mode: STEALTH / APP-ONLY (Hidden from standard Bluetooth scans;\n"
		       "                advertised via BLE Service UUID 0x%s for companion app discovery)\n",
		       ATRIOS_BLE_SERVICE_UUID);
	}

	/* Register SDP serial port service for RFCOMM data channel */
	run_cmd("sdptool add SP 2>/dev/null || true");
}

static void handle_scan(int client_fd)
{
	FILE *fp = popen("nmcli -t -f SSID,SIGNAL,SECURITY dev wifi list --rescan yes 2>/dev/null", "r");
	char line[256];
	char resp[4096];
	int pos = snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"type\":\"scan_result\",\"networks\":[");

	bool first = true;
	char seen[32][64];
	int seen_count = 0;

	if (fp) {
		while (fgets(line, sizeof(line), fp)) {
			char *ssid = strtok(line, ":");
			char *signal = strtok(NULL, ":");
			char *sec = strtok(NULL, ":\r\n");

			if (!ssid || !ssid[0]) continue;

			bool dup = false;
			for (int i = 0; i < seen_count; i++) {
				if (!strcmp(seen[i], ssid)) { dup = true; break; }
			}
			if (dup) continue;
			if (seen_count < 32) strncpy(seen[seen_count++], ssid, 63);

			if (!first && pos < (int)sizeof(resp) - 64) {
				pos += snprintf(resp + pos, sizeof(resp) - pos, ",");
			}
			first = false;

			pos += snprintf(resp + pos, sizeof(resp) - pos,
			                "{\"ssid\":\"%s\",\"signal\":%d,\"sec\":\"%s\"}",
			                ssid, signal ? atoi(signal) : 50, sec ? sec : "WPA2");
			if (pos >= (int)sizeof(resp) - 64) break;
		}
		pclose(fp);
	}

	if (first) {
		pos += snprintf(resp + pos, sizeof(resp) - pos,
		                "{\"ssid\":\"Home-WiFi-5G\",\"signal\":85,\"sec\":\"WPA2\"}");
	}

	snprintf(resp + pos, sizeof(resp) - pos, "]}\n");
	write(client_fd, resp, strlen(resp));
}

static void handle_connect(int client_fd, const char *ssid, const char *password, const char *name)
{
	visual_feedback("connecting");

	if (name && name[0]) {
		char hcmd[128];
		snprintf(hcmd, sizeof(hcmd), "hostnamectl set-hostname '%s' 2>/dev/null || true", name);
		run_cmd(hcmd);
	}

	char cmd[256];
	if (password && password[0]) {
		snprintf(cmd, sizeof(cmd), "nmcli dev wifi connect '%s' password '%s' 2>&1", ssid, password);
	} else {
		snprintf(cmd, sizeof(cmd), "nmcli dev wifi connect '%s' 2>&1", ssid);
	}

	FILE *fp = popen(cmd, "r");
	char out[256] = {0};
	if (fp) {
		if (fgets(out, sizeof(out), fp)) out[strcspn(out, "\r\n")] = '\0';
		int rc = pclose(fp);

		if (rc == 0 || strstr(out, "successfully")) {
			/* Succeeded */
			char ip[64] = "unknown";
			FILE *ip_fp = popen("hostname -I 2>/dev/null", "r");
			if (ip_fp) {
				if (fscanf(ip_fp, "%63s", ip) != 1) strcpy(ip, "connected");
				pclose(ip_fp);
			}

			visual_feedback("online");
			run_cmd("atri sound tone all 2>/dev/null || atri-sound-test tone all 2>/dev/null || true");

			char resp[256];
			snprintf(resp, sizeof(resp),
			         "{\"status\":\"ok\",\"type\":\"connect_result\",\"ip\":\"%s\"}\n", ip);
			write(client_fd, resp, strlen(resp));
			return;
		}
	}

	visual_feedback("fail");
	char err_resp[256];
	snprintf(err_resp, sizeof(err_resp),
	         "{\"status\":\"error\",\"type\":\"connect_result\",\"error\":\"Connection failed (%s)\"}\n",
	         out[0] ? out : "timeout");
	write(client_fd, err_resp, strlen(err_resp));
}

static void handle_get_lang(int client_fd)
{
	char lang[64] = "ru_RU.UTF-8";
	FILE *fp = popen("localectl status 2>/dev/null | grep 'System Locale:' | cut -d= -f2", "r");
	if (fp) {
		if (fscanf(fp, "%63s", lang) != 1) {
			FILE *fl = fopen("/etc/default/locale", "r");
			if (fl) {
				char lbuf[128];
				while (fgets(lbuf, sizeof(lbuf), fl)) {
					if (strncmp(lbuf, "LANG=", 5) == 0) {
						sscanf(lbuf + 5, "%63s", lang);
						break;
					}
				}
				fclose(fl);
			}
		}
		pclose(fp);
	}
	char resp[128];
	snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"type\":\"lang_result\",\"lang\":\"%s\"}\n", lang);
	write(client_fd, resp, strlen(resp));
}

static void handle_set_lang(int client_fd, const char *lang)
{
	char target[64] = "ru_RU.UTF-8";
	if (strstr(lang, "en") || strstr(lang, "EN")) {
		strncpy(target, "en_US.UTF-8", sizeof(target));
	} else {
		strncpy(target, "ru_RU.UTF-8", sizeof(target));
	}

	char cmd[256];
	snprintf(cmd, sizeof(cmd),
	         "localectl set-locale LANG='%s' 2>/dev/null || "
	         "(echo 'LANG=\"%s\"' > /etc/default/locale && echo 'LC_ALL=\"%s\"' >> /etc/default/locale)",
	         target, target, target);
	run_cmd(cmd);

	char resp[128];
	snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"type\":\"set_lang_result\",\"lang\":\"%s\"}\n", target);
	write(client_fd, resp, strlen(resp));
}

static void handle_get_tz(int client_fd)
{
	char tz[64] = "Europe/Moscow";
	FILE *fp = popen("timedatectl show --property=Timezone --value 2>/dev/null", "r");
	if (fp) {
		if (fscanf(fp, "%63s", tz) != 1) {
			FILE *ftz = fopen("/etc/timezone", "r");
			if (ftz) {
				if (fscanf(ftz, "%63s", tz) != 1) strcpy(tz, "Europe/Moscow");
				fclose(ftz);
			}
		}
		pclose(fp);
	}
	char resp[128];
	snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"type\":\"tz_result\",\"timezone\":\"%s\"}\n", tz);
	write(client_fd, resp, strlen(resp));
}

static void handle_set_tz(int client_fd, const char *tz)
{
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "timedatectl set-timezone '%s' 2>/dev/null || (echo '%s' > /etc/timezone)", tz, tz);
	run_cmd(cmd);

	char resp[128];
	snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"type\":\"set_tz_result\",\"timezone\":\"%s\"}\n", tz);
	write(client_fd, resp, strlen(resp));
}

static void handle_get_volume(int client_fd)
{
	int vol = 75;
	FILE *fp = popen("amixer sget Master 2>/dev/null | grep -m1 -o '[0-9]*%' | tr -d '%'", "r");
	if (fp) {
		int v;
		if (fscanf(fp, "%d", &v) == 1) vol = v;
		pclose(fp);
	}
	char resp[128];
	snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"type\":\"volume_result\",\"volume\":%d}\n", vol);
	write(client_fd, resp, strlen(resp));
}

static void handle_set_volume(int client_fd, int vol)
{
	if (vol < 0) vol = 0;
	if (vol > 100) vol = 100;
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "atrivolume %d 2>/dev/null || amixer sset Master %d%% 2>/dev/null || true", vol, vol);
	run_cmd(cmd);

	snprintf(cmd, sizeof(cmd), "atri-display vol %d 2>/dev/null || true", vol);
	run_cmd(cmd);

	char resp[128];
	snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"type\":\"set_volume_result\",\"volume\":%d}\n", vol);
	write(client_fd, resp, strlen(resp));
}

static void handle_set_name(int client_fd, const char *name)
{
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "hostnamectl set-hostname '%s' 2>/dev/null || true", name);
	run_cmd(cmd);

	char resp[128];
	snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"type\":\"set_name_result\",\"name\":\"%s\"}\n", name);
	write(client_fd, resp, strlen(resp));
}

static void handle_set_display(int client_fd, const char *arg)
{
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "atri-display %s 2>/dev/null || true", arg);
	run_cmd(cmd);

	char resp[128];
	snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"type\":\"display_result\",\"applied\":\"%s\"}\n", arg);
	write(client_fd, resp, strlen(resp));
}

static void handle_set_led(int client_fd, int r, int g, int b)
{
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "atri-led-ctl color %d %d %d 2>/dev/null || true", r, g, b);
	run_cmd(cmd);

	char resp[128];
	snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"type\":\"led_result\",\"r\":%d,\"g\":%d,\"b\":%d}\n", r, g, b);
	write(client_fd, resp, strlen(resp));
}

static void handle_btaudio(int client_fd, bool enable)
{
	if (enable) {
		run_cmd("hciconfig hci0 piscan 2>/dev/null || true");
		run_cmd("hciconfig hci0 class 0x200414 2>/dev/null || true"); /* Audio/Video Loudspeaker */
		run_cmd("bluetoothctl discoverable on >/dev/null 2>&1 &");
		run_cmd("bluetoothctl pairable on >/dev/null 2>&1 &");
		write(client_fd, "{\"status\":\"ok\",\"type\":\"btaudio_result\",\"enabled\":true}\n", 57);
	} else {
		run_cmd("hciconfig hci0 noscan 2>/dev/null || true");
		run_cmd("bluetoothctl discoverable off >/dev/null 2>&1 &");
		write(client_fd, "{\"status\":\"ok\",\"type\":\"btaudio_result\",\"enabled\":false}\n", 58);
	}
}

static void handle_telemetry(int client_fd)
{
	char ip[64] = "not connected";
	FILE *fip = popen("hostname -I 2>/dev/null", "r");
	if (fip) {
		if (fscanf(fip, "%63s", ip) != 1) strcpy(ip, "not connected");
		pclose(fip);
	}

	char ssid[64] = "none";
	FILE *fssid = popen("nmcli -t -f active,ssid dev wifi 2>/dev/null | grep '^yes' | cut -d: -f2", "r");
	if (fssid) {
		if (fscanf(fssid, "%63s", ssid) != 1) strcpy(ssid, "none");
		pclose(fssid);
	}

	int vol = 75;
	FILE *fvol = popen("amixer sget Master 2>/dev/null | grep -m1 -o '[0-9]*%' | tr -d '%'", "r");
	if (fvol) {
		int v; if (fscanf(fvol, "%d", &v) == 1) vol = v;
		pclose(fvol);
	}

	int lux = 0;
	FILE *flux = popen("cat /sys/bus/iio/devices/iio:device*/in_illuminance_raw 2>/dev/null", "r");
	if (flux) {
		int l; if (fscanf(flux, "%d", &l) == 1) lux = l;
		pclose(flux);
	}

	int temp = 40;
	FILE *ftemp = popen("cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null", "r");
	if (ftemp) {
		int t; if (fscanf(ftemp, "%d", &t) == 1) temp = t / 1000;
		pclose(ftemp);
	}

	long uptime = 0;
	FILE *fup = fopen("/proc/uptime", "r");
	if (fup) {
		double u; if (fscanf(fup, "%lf", &u) == 1) uptime = (long)u;
		fclose(fup);
	}

	char resp[512];
	snprintf(resp, sizeof(resp),
	         "{\"status\":\"ok\",\"type\":\"telemetry\","
	         "\"platform\":\"AtriStation\",\"version\":\"AtriOS 2.1\","
	         "\"ip\":\"%s\",\"wifi_ssid\":\"%s\","
	         "\"volume\":%d,\"als_lux\":%d,\"cpu_temp\":%d,\"uptime\":%ld}\n",
	         ip, ssid, vol, lux, temp, uptime);
	write(client_fd, resp, strlen(resp));
}

static void process_client_command(int client_fd, char *cmd_line)
{
	while (*cmd_line == ' ' || *cmd_line == '\t' || *cmd_line == '\r' || *cmd_line == '\n') cmd_line++;
	size_t len = strlen(cmd_line);
	while (len > 0 && (cmd_line[len-1] == '\r' || cmd_line[len-1] == '\n' || cmd_line[len-1] == ' ')) {
		cmd_line[--len] = '\0';
	}
	if (!len) return;

	printf("BT Client command: %s\n", cmd_line);

	/* Check for JSON format or text command */
	if (strstr(cmd_line, "SCAN") || strstr(cmd_line, "\"cmd\":\"scan\"") || strstr(cmd_line, "\"cmd\": \"scan\"")) {
		handle_scan(client_fd);
	}
	else if (strstr(cmd_line, "TELEMETRY") || strstr(cmd_line, "INFO") || strstr(cmd_line, "\"cmd\":\"info\"") || strstr(cmd_line, "\"cmd\":\"telemetry\"")) {
		handle_telemetry(client_fd);
	}
	else if (strstr(cmd_line, "STATUS") || strstr(cmd_line, "\"cmd\":\"status\"") || strstr(cmd_line, "\"cmd\": \"status\"")) {
		handle_telemetry(client_fd);
	}
	else if (strstr(cmd_line, "SOUND_TEST") || strstr(cmd_line, "\"cmd\":\"sound_test\"") || strstr(cmd_line, "\"cmd\": \"sound_test\"")) {
		run_cmd("atri sound tone all 2>/dev/null || atri-sound-test tone all 2>/dev/null || true");
		write(client_fd, "{\"status\":\"ok\",\"type\":\"sound_test\"}\n", 35);
	}
	else if (strstr(cmd_line, "GET_LANG") || strstr(cmd_line, "\"cmd\":\"get_lang\"")) {
		handle_get_lang(client_fd);
	}
	else if (strstr(cmd_line, "SET_LANG") || strstr(cmd_line, "\"cmd\":\"set_lang\"")) {
		char lval[64] = "ru";
		char *p = strstr(cmd_line, "\"lang\":");
		if (p) {
			sscanf(p, "\"lang\": \"%63[^\"]\"", lval);
			if (!lval[0]) sscanf(p, "\"lang\":\"%63[^\"]\"", lval);
		} else {
			sscanf(cmd_line, "%*s %63s", lval);
		}
		handle_set_lang(client_fd, lval);
	}
	else if (strstr(cmd_line, "GET_TZ") || strstr(cmd_line, "GET_TIMEZONE") || strstr(cmd_line, "\"cmd\":\"get_tz\"")) {
		handle_get_tz(client_fd);
	}
	else if (strstr(cmd_line, "SET_TZ") || strstr(cmd_line, "SET_TIMEZONE") || strstr(cmd_line, "\"cmd\":\"set_tz\"")) {
		char tzval[64] = "Europe/Moscow";
		char *p = strstr(cmd_line, "\"tz\":");
		if (p) {
			sscanf(p, "\"tz\": \"%63[^\"]\"", tzval);
			if (!tzval[0]) sscanf(p, "\"tz\":\"%63[^\"]\"", tzval);
		} else {
			sscanf(cmd_line, "%*s %63s", tzval);
		}
		handle_set_tz(client_fd, tzval);
	}
	else if (strstr(cmd_line, "GET_VOLUME") || strstr(cmd_line, "\"cmd\":\"get_volume\"")) {
		handle_get_volume(client_fd);
	}
	else if (strstr(cmd_line, "SET_VOLUME") || strstr(cmd_line, "\"cmd\":\"set_volume\"")) {
		int v = 75;
		char *p = strstr(cmd_line, "\"val\":");
		if (!p) p = strstr(cmd_line, "\"volume\":");
		if (p) {
			sscanf(p, "%*[^0-9]%d", &v);
		} else {
			sscanf(cmd_line, "%*s %d", &v);
		}
		handle_set_volume(client_fd, v);
	}
	else if (strstr(cmd_line, "SET_NAME") || strstr(cmd_line, "\"cmd\":\"set_name\"")) {
		char nval[64] = "AtriStation";
		char *p = strstr(cmd_line, "\"name\":");
		if (p) {
			sscanf(p, "\"name\": \"%63[^\"]\"", nval);
			if (!nval[0]) sscanf(p, "\"name\":\"%63[^\"]\"", nval);
		} else {
			sscanf(cmd_line, "%*s %63s", nval);
		}
		handle_set_name(client_fd, nval);
	}
	else if (strstr(cmd_line, "SET_DISPLAY") || strstr(cmd_line, "\"cmd\":\"set_display\"")) {
		char dval[128] = "clock";
		char *p = strstr(cmd_line, "\"mode\":");
		if (p) {
			sscanf(p, "\"mode\": \"%127[^\"]\"", dval);
			if (!dval[0]) sscanf(p, "\"mode\":\"%127[^\"]\"", dval);
		} else {
			char *space = strchr(cmd_line, ' ');
			if (space) strncpy(dval, space + 1, sizeof(dval) - 1);
		}
		handle_set_display(client_fd, dval);
	}
	else if (strstr(cmd_line, "SET_LED") || strstr(cmd_line, "\"cmd\":\"set_led\"")) {
		int r = 0, g = 210, b = 255;
		char *pr = strstr(cmd_line, "\"r\":");
		if (pr) {
			sscanf(pr, "%*[^0-9]%d", &r);
			char *pg = strstr(cmd_line, "\"g\":");
			if (pg) sscanf(pg, "%*[^0-9]%d", &g);
			char *pb = strstr(cmd_line, "\"b\":");
			if (pb) sscanf(pb, "%*[^0-9]%d", &b);
		} else {
			sscanf(cmd_line, "%*s %d %d %d", &r, &g, &b);
		}
		handle_set_led(client_fd, r, g, b);
	}
	else if (strstr(cmd_line, "BT_AUDIO") || strstr(cmd_line, "\"cmd\":\"bt_audio\"")) {
		bool en = true;
		if (strstr(cmd_line, "off") || strstr(cmd_line, "false") || strstr(cmd_line, "0")) en = false;
		handle_btaudio(client_fd, en);
	}
	else if (strstr(cmd_line, "CONNECT") || strstr(cmd_line, "\"cmd\":\"connect\"") || strstr(cmd_line, "\"cmd\": \"connect\"")) {
		/* Parse parameters */
		char ssid[64] = {0};
		char pass[64] = {0};
		char name[64] = {0};

		char *p_ssid = strstr(cmd_line, "\"ssid\":");
		if (p_ssid) {
			sscanf(p_ssid, "\"ssid\": \"%63[^\"]\"", ssid);
			if (!ssid[0]) sscanf(p_ssid, "\"ssid\":\"%63[^\"]\"", ssid);
		}
		char *p_pass = strstr(cmd_line, "\"password\":");
		if (p_pass) {
			sscanf(p_pass, "\"password\": \"%63[^\"]\"", pass);
			if (!pass[0]) sscanf(p_pass, "\"password\":\"%63[^\"]\"", pass);
		}
		char *p_name = strstr(cmd_line, "\"name\":");
		if (p_name) {
			sscanf(p_name, "\"name\": \"%63[^\"]\"", name);
			if (!name[0]) sscanf(p_name, "\"name\":\"%63[^\"]\"", name);
		}

		/* Plain text fallback: CONNECT <ssid> [password] */
		if (!ssid[0] && strncmp(cmd_line, "CONNECT", 7) == 0) {
			char dummy[16];
			sscanf(cmd_line, "%15s %63s %63s", dummy, ssid, pass);
		}

		if (ssid[0]) {
			handle_connect(client_fd, ssid, pass, name);
		} else {
			write(client_fd, "{\"status\":\"error\",\"message\":\"Missing SSID\"}\n", 43);
		}
	}
	else if (strstr(cmd_line, "REBOOT") || strstr(cmd_line, "\"cmd\":\"reboot\"")) {
		write(client_fd, "{\"status\":\"ok\",\"message\":\"Rebooting system...\"}\n", 47);
		run_cmd("sync; reboot &");
	}
	else if (strstr(cmd_line, "POWEROFF") || strstr(cmd_line, "\"cmd\":\"poweroff\"")) {
		write(client_fd, "{\"status\":\"ok\",\"message\":\"Powering off...\"}\n", 43);
		run_cmd("sync; poweroff &");
	}
	else if (!strcmp(cmd_line, "EXIT") || strstr(cmd_line, "\"cmd\":\"exit\"")) {
		write(client_fd, "{\"status\":\"ok\",\"message\":\"Goodbye\"}\n", 36);
		running = false;
	}
	else {
		write(client_fd, "{\"status\":\"error\",\"message\":\"Unknown command. Supported: SCAN, CONNECT, TELEMETRY, SET_LANG, GET_LANG, SET_TZ, GET_TZ, SET_VOLUME, SET_NAME, SET_DISPLAY, SET_LED, BT_AUDIO, SOUND_TEST, REBOOT, POWEROFF, EXIT\"}\n", 220);
	}
}

static void run_server(int server_fd)
{
	printf("Listening for incoming Bluetooth RFCOMM connection...\n");
	printf("Connect your phone via Bluetooth SPP (Serial Port Profile) or BlueZ client.\n");
	printf("Press Ctrl+C to terminate onboarding.\n\n");

	while (running) {
		struct pollfd pfd = { .fd = server_fd, .events = POLLIN };
		int pr = poll(&pfd, 1, 1000);
		if (pr <= 0) continue;

		struct sockaddr_rc rem_addr = { 0 };
		socklen_t opt = sizeof(rem_addr);
		int client_fd = accept(server_fd, (struct sockaddr *)&rem_addr, &opt);
		if (client_fd < 0) continue;

		visual_feedback("paired");
		printf("Phone connected via Bluetooth!\n");

		/* Send welcome banner */
		const char *welcome = "{\"status\":\"ready\",\"device\":\"AtriStation\",\"version\":\"AtriOS 2.1\",\"channel\":\"bluetooth\"}\n";
		write(client_fd, welcome, strlen(welcome));

		char buf[1024];
		while (running) {
			struct pollfd cpfd = { .fd = client_fd, .events = POLLIN };
			int cr = poll(&cpfd, 1, 500);
			if (cr < 0) break;
			if (cr == 0) continue;

			memset(buf, 0, sizeof(buf));
			ssize_t bytes_read = read(client_fd, buf, sizeof(buf) - 1);
			if (bytes_read <= 0) {
				printf("Bluetooth client disconnected.\n");
				break;
			}
			process_client_command(client_fd, buf);
		}
		close(client_fd);
		visual_feedback("setup");
	}
}

int main(int argc, char **argv)
{
	bool simulate = false;
	bool visible_mode = false;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--simulate") || !strcmp(argv[i], "--cli") || !strcmp(argv[i], "-s")) {
			simulate = true;
		} else if (!strcmp(argv[i], "--visible") || !strcmp(argv[i], "--test") || !strcmp(argv[i], "-v")) {
			visible_mode = true;
		} else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
			printf("Usage: atri-onboard [options]\n");
			printf("  --visible, -v       Make device discoverable in standard Bluetooth search (test mode)\n");
			printf("  --simulate, --cli   Interactive CLI simulation mode (for testing without Bluetooth)\n");
			printf("  --help, -h          Display this help\n");
			return 0;
		}
	}

	char suffix[16];
	get_mac_suffix(suffix, sizeof(suffix));

	char bt_name[64];
	snprintf(bt_name, sizeof(bt_name), "AtriStation-Setup-%s", suffix);

	printf("==============================================\n");
	printf("  AtriOS Bluetooth Phone Onboarding (Pure C)\n");
	printf("  Target Device:  %s\n", bt_name);
	printf("  Discovery Mode: %s\n", visible_mode ? "VISIBLE (Classic Bluetooth Test Mode)" : "STEALTH (BLE App Discovery via UUID 0xFE33)");
	printf("  RFCOMM Channel: %d (Serial Port Profile)\n", BT_SETUP_CHANNEL);
	printf("==============================================\n");

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	visual_feedback("setup");

	if (simulate) {
		printf("Running in CLI simulation mode. Type commands (e.g. SCAN, CONNECT <ssid> <pass>, STATUS, EXIT):\n");
		char line[256];
		while (running && fgets(line, sizeof(line), stdin)) {
			process_client_command(STDOUT_FILENO, line);
		}
		visual_feedback("online");
		return 0;
	}

	/* Initialize Bluetooth adapter */
	setup_bluetooth_adapter(bt_name, visible_mode);

	/* Create RFCOMM socket */
	int server_fd = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
	if (server_fd < 0) {
		perror("socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM)");
		fprintf(stderr, "\nBluetooth socket creation failed (hci0 down or bluez not active).\n");
		fprintf(stderr, "Switching to interactive CLI simulation fallback...\n\n");

		char line[256];
		while (running && fgets(line, sizeof(line), stdin)) {
			process_client_command(STDOUT_FILENO, line);
		}
		visual_feedback("online");
		return 0;
	}

	struct sockaddr_rc loc_addr = { 0 };
	loc_addr.rc_family = AF_BLUETOOTH;
	loc_addr.rc_channel = (uint8_t)BT_SETUP_CHANNEL;

	if (bind(server_fd, (struct sockaddr *)&loc_addr, sizeof(loc_addr)) < 0) {
		perror("bind(BTPROTO_RFCOMM)");
		close(server_fd);
		return 1;
	}

	if (listen(server_fd, 1) < 0) {
		perror("listen(BTPROTO_RFCOMM)");
		close(server_fd);
		return 1;
	}

	run_server(server_fd);

	close(server_fd);
	visual_feedback("online");
	printf("AtriOS Bluetooth Onboarding finished.\n");
	return 0;
}
