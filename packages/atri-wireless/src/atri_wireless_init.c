// SPDX-License-Identifier: GPL-2.0
/*
 * atri_wireless_init.c — First-boot unique MAC & eFuse provisioner for AtriStation
 *
 * Part of AtriOS BSP (packages/atri-led)
 * Author: leftymods / AtriOS Project
 *
 * Resolves unique hardware ID from:
 *   1. Board I2C EEPROM (@ 0x50)
 *   2. SoC Serial Number (/proc/cpuinfo, /sys/class/efuse/)
 *   3. /etc/machine-id (fallback)
 *
 * Generates deterministic locally administered MAC addresses for Wi-Fi and Bluetooth,
 * patches the physical RTL8822CS virtual eFuse file (/lib/firmware/rtw88/rtl8822cs_efuse.bin),
 * and creates /etc/atri-wireless/mac.conf and marker /etc/atri-wireless/.provisioned.
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
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#define PROVISION_MARKER "/etc/atri-wireless/.provisioned"
#define MAC_CONFIG_PATH  "/etc/atri-wireless/mac.conf"
#define EFUSE_TARGET_PATH "/lib/firmware/rtw88/rtl8822cs_efuse.bin"
#define EFUSE_VENDOR_PATH "/usr/share/atri-fw-vendor/rtl8822cs_efuse.bin"

#ifndef I2C_SLAVE_FORCE
#define I2C_SLAVE_FORCE 0x0706
#endif

/* 49-byte calibrated base physical eFuse map for RTL8822CS SDIO */
static const uint8_t base_phy_efuse[49] = {
	0x0e, 0x29, 0x81, 0xef, 0x2e, 0x7f, 0x3f, 0x0f,
	0x30, 0xff, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x2f, 0x38, 0x00, 0x00, 0x00, 0x45, 0x55,
	0xff, 0x4f, 0x32, 0x33, 0x33, 0x00, 0xff, 0x00,
	0xff, 0x6f, 0x38, 0x00, 0xff, 0x00, 0xff, 0x00,
	0xff, 0xaf, 0x51, 0x02, 0x1a, 0x2b, 0x00, 0x00,
	0x00
};

/* FNV-1a 64-bit hash */
static uint64_t fnv1a64(const char *str)
{
	uint64_t hash = 14695981039346656037ULL;
	while (*str) {
		hash ^= (uint8_t)(*str++);
		hash *= 1099511628211ULL;
	}
	return hash;
}

/* Try to read unique serial/MAC from board I2C EEPROM at 0x50 */
static bool read_eeprom_id(char *out, size_t outlen)
{
	const char *buses[] = { "/dev/i2c-0", "/dev/i2c-1", "/dev/i2c-2", NULL };
	for (int b = 0; buses[b]; b++) {
		int fd = open(buses[b], O_RDWR);
		if (fd < 0) continue;
		if (ioctl(fd, I2C_SLAVE_FORCE, 0x50) < 0 && ioctl(fd, I2C_SLAVE, 0x50) < 0) {
			close(fd);
			continue;
		}
		uint8_t off = 0;
		if (write(fd, &off, 1) != 1) { /* ignore */ }
		uint8_t buf[32];
		memset(buf, 0, sizeof(buf));
		int n = read(fd, buf, sizeof(buf));
		close(fd);
		if (n >= 8) {
			/* Check if contains printable non-blank characters */
			int printable = 0;
			for (int i = 0; i < n; i++) {
				if (buf[i] >= 0x20 && buf[i] < 0x7F) printable++;
			}
			if (printable >= 6) {
				snprintf(out, outlen, "%.*s", n, (char*)buf);
				return true;
			}
		}
	}
	return false;
}

/* Read SoC serial from /proc/cpuinfo */
static bool read_cpuinfo_serial(char *out, size_t outlen)
{
	FILE *fp = fopen("/proc/cpuinfo", "r");
	if (!fp) return false;
	char line[256];
	bool found = false;
	while (fgets(line, sizeof(line), fp)) {
		if (strncasecmp(line, "Serial", 6) == 0) {
			char *colon = strchr(line, ':');
			if (colon) {
				colon++;
				while (*colon == ' ' || *colon == '\t') colon++;
				char *end = colon + strlen(colon) - 1;
				while (end > colon && (*end == '\n' || *end == '\r' || *end == ' '))
					*end-- = '\0';
				if (strlen(colon) > 3 && strcmp(colon, "0000000000000000") != 0) {
					snprintf(out, outlen, "%s", colon);
					found = true;
					break;
				}
			}
		}
	}
	fclose(fp);
	return found;
}

/* Read machine-id fallback */
static bool read_machine_id(char *out, size_t outlen)
{
	FILE *fp = fopen("/etc/machine-id", "r");
	if (!fp) return false;
	char buf[128];
	if (fgets(buf, sizeof(buf), fp)) {
		char *end = buf + strlen(buf) - 1;
		while (end > buf && (*end == '\n' || *end == '\r' || *end == ' '))
			*end-- = '\0';
		snprintf(out, outlen, "%s", buf);
		fclose(fp);
		return true;
	}
	fclose(fp);
	return false;
}

static void ensure_dir(const char *path)
{
	char tmp[256];
	snprintf(tmp, sizeof(tmp), "%s", path);
	char *slash = strrchr(tmp, '/');
	if (slash) {
		*slash = '\0';
		mkdir(tmp, 0755);
	}
}

int main(int argc, char **argv)
{
	bool force = false;
	bool dry_run = false;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--force") == 0 || strcmp(argv[i], "-f") == 0)
			force = true;
		else if (strcmp(argv[i], "--dry-run") == 0 || strcmp(argv[i], "-n") == 0)
			dry_run = true;
	}

	if (!force && access(PROVISION_MARKER, F_OK) == 0) {
		printf("atri-wireless-init: wireless MAC and eFuse already provisioned.\n");
		return 0;
	}

	char hw_id[128] = "";
	const char *src_name = "fallback";

	if (read_eeprom_id(hw_id, sizeof(hw_id))) {
		src_name = "I2C EEPROM (0x50)";
	} else if (read_cpuinfo_serial(hw_id, sizeof(hw_id))) {
		src_name = "/proc/cpuinfo Serial";
	} else if (read_machine_id(hw_id, sizeof(hw_id))) {
		src_name = "/etc/machine-id";
	} else {
		snprintf(hw_id, sizeof(hw_id), "atri-station-fallback-%ld", (long)time(NULL));
	}

	uint64_t hash = fnv1a64(hw_id);

	/* Wi-Fi MAC: Locally Administered (bit 1=1), Unicast (bit 0=0) */
	uint8_t wifi_mac[6];
	wifi_mac[0] = 0x02;
	wifi_mac[1] = 0x1A;
	wifi_mac[2] = 0x2B;
	wifi_mac[3] = (uint8_t)((hash >> 16) & 0xFF);
	wifi_mac[4] = (uint8_t)((hash >> 8) & 0xFF);
	wifi_mac[5] = (uint8_t)(hash & 0xFF);

	/* Bluetooth MAC: distinct address */
	uint8_t bt_mac[6];
	memcpy(bt_mac, wifi_mac, 6);
	bt_mac[5] = (uint8_t)((wifi_mac[5] + 1) & 0xFF);

	printf("atri-wireless-init: resolved hardware ID from %s: '%s'\n", src_name, hw_id);
	printf("atri-wireless-init: generated Wi-Fi MAC:     %02X:%02X:%02X:%02X:%02X:%02X\n",
	       wifi_mac[0], wifi_mac[1], wifi_mac[2], wifi_mac[3], wifi_mac[4], wifi_mac[5]);
	printf("atri-wireless-init: generated Bluetooth MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
	       bt_mac[0], bt_mac[1], bt_mac[2], bt_mac[3], bt_mac[4], bt_mac[5]);

	if (dry_run) {
		printf("atri-wireless-init: [dry-run] no files written.\n");
		return 0;
	}

	/* Prepare 512-byte eFuse binary */
	uint8_t efuse_data[512];
	memset(efuse_data, 0xFF, sizeof(efuse_data));

	/* Check if existing efuse binary is present in vendor share */
	int fd_tmpl = open(EFUSE_VENDOR_PATH, O_RDONLY);
	if (fd_tmpl < 0)
		fd_tmpl = open(EFUSE_TARGET_PATH, O_RDONLY);

	if (fd_tmpl >= 0) {
		ssize_t rd = read(fd_tmpl, efuse_data, sizeof(efuse_data));
		close(fd_tmpl);
		if (rd < 49) {
			memcpy(efuse_data, base_phy_efuse, sizeof(base_phy_efuse));
		}
	} else {
		memcpy(efuse_data, base_phy_efuse, sizeof(base_phy_efuse));
	}

	/* Offset 43..48 is the MAC address in the blk 45 physical stream */
	memcpy(&efuse_data[43], wifi_mac, 6);

	/* Write to /lib/firmware/rtw88/rtl8822cs_efuse.bin */
	ensure_dir(EFUSE_TARGET_PATH);
	int fd_ef = open(EFUSE_TARGET_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd_ef >= 0) {
		if (write(fd_ef, efuse_data, sizeof(efuse_data)) != sizeof(efuse_data))
			fprintf(stderr, "atri-wireless-init: error writing %s\n", EFUSE_TARGET_PATH);
		close(fd_ef);
		printf("atri-wireless-init: updated %s\n", EFUSE_TARGET_PATH);
	}

	/* Also update vendor staging path */
	ensure_dir(EFUSE_VENDOR_PATH);
	fd_ef = open(EFUSE_VENDOR_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd_ef >= 0) {
		write(fd_ef, efuse_data, sizeof(efuse_data));
		close(fd_ef);
	}

	/* Write /etc/atri-wireless/mac.conf */
	ensure_dir(MAC_CONFIG_PATH);
	FILE *fp_conf = fopen(MAC_CONFIG_PATH, "w");
	if (fp_conf) {
		fprintf(fp_conf, "# Generated by atri-wireless-init\n");
		fprintf(fp_conf, "WIFI_MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
		        wifi_mac[0], wifi_mac[1], wifi_mac[2], wifi_mac[3], wifi_mac[4], wifi_mac[5]);
		fprintf(fp_conf, "BT_MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
		        bt_mac[0], bt_mac[1], bt_mac[2], bt_mac[3], bt_mac[4], bt_mac[5]);
		fprintf(fp_conf, "HW_ID_SOURCE=%s\n", src_name);
		fclose(fp_conf);
	}

	/* Write systemd link file for persistent MAC on wlan0 */
	const char *link_path = "/etc/systemd/network/10-wlan0.link";
	ensure_dir(link_path);
	FILE *fp_link = fopen(link_path, "w");
	if (fp_link) {
		fprintf(fp_link, "[Match]\nOriginalName=wlan0\n\n[Link]\nMACAddress=%02X:%02X:%02X:%02X:%02X:%02X\n",
		        wifi_mac[0], wifi_mac[1], wifi_mac[2], wifi_mac[3], wifi_mac[4], wifi_mac[5]);
		fclose(fp_link);
	}

	/* Touch provision marker */
	int fd_m = open(PROVISION_MARKER, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd_m >= 0) close(fd_m);

	printf("atri-wireless-init: wireless provisioning complete.\n");
	return 0;
}
