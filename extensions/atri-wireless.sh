# shellcheck shell=bash disable=SC2154,SC2153
#
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2025-2026 leftymods
#

# Setup RTL8822CS WiFi, Bluetooth vendor firmware and Zigbee module loading

function extension_prepare_config__atri_wireless() {
	return 0
}

function post_family_tweaks_bsp__atri_wireless_add_config() {
	# Early module loading for Zigbee
	mkdir -pv "${destination}"/etc/modules-load.d
	cat <<- MODS > "${destination}"/etc/modules-load.d/zigbee.conf
		zigbee_control
	MODS

	# Stage vendor RTL8822CS WiFi and BT firmware files
	run_host_command_logged mkdir -pv "${destination}"/usr/share/atri-fw-vendor
	if [[ -f "${SRC}/packages/atri-fw/rtl8822cs_config.bin" ]]; then
		cp "${SRC}/packages/atri-fw/rtl8822cs_config.bin" "${destination}"/usr/share/atri-fw-vendor/
	fi
	if [[ -f "${SRC}/packages/atri-fw/wifi_vendor_fw.bin" ]]; then
		cp "${SRC}/packages/atri-fw/wifi_vendor_fw.bin"   "${destination}"/usr/share/atri-fw-vendor/
	fi
	if [[ -f "${SRC}/packages/atri-fw/vendor_bt_fw.bin" ]]; then
		cp "${SRC}/packages/atri-fw/vendor_bt_fw.bin"      "${destination}"/usr/share/atri-fw-vendor/
	fi
	if [[ -f "${SRC}/packages/atri-fw/rtl8822cs_efuse.bin" ]]; then
		cp "${SRC}/packages/atri-fw/rtl8822cs_efuse.bin"  "${destination}"/usr/share/atri-fw-vendor/
	fi

	# Replace at boot via tmpfiles.d (runs before bluetooth.service)
	# Type "C+" forces overwrite even if destination already exists.
	mkdir -pv "${destination}"/etc/tmpfiles.d
	cat <<- TMPF > "${destination}"/etc/tmpfiles.d/rtl8822cs-vendor-config.conf
		C+ /lib/firmware/rtl_bt/rtl8822cs_config.bin 0644 root root - /usr/share/atri-fw-vendor/rtl8822cs_config.bin
		C+ /lib/firmware/rtl_bt/rtl8822cs_config     0644 root root - /usr/share/atri-fw-vendor/rtl8822cs_config.bin
		C+ /lib/firmware/rtw88/rtw8822c_fw.bin       0644 root root - /usr/share/atri-fw-vendor/wifi_vendor_fw.bin
		C+ /lib/firmware/rtw88/rtl8822cs_efuse.bin   0644 root root - /usr/share/atri-fw-vendor/rtl8822cs_efuse.bin
		C+ /lib/firmware/rtl_bt/rtl8822cs_fw.bin     0644 root root - /usr/share/atri-fw-vendor/vendor_bt_fw.bin
	TMPF

	display_alert "Extension: ${EXTENSION}: ${BOARD}" "staged RTL8822CS vendor firmware and tmpfiles" "info"
	return 0
}
