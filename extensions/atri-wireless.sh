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
	cat <<- 'MODS' > "${destination}"/etc/modules-load.d/zigbee.conf
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
	cat <<- 'TMPF' > "${destination}"/etc/tmpfiles.d/rtl8822cs-vendor-config.conf
		C+ /lib/firmware/rtl_bt/rtl8822cs_config.bin 0644 root root - /usr/share/atri-fw-vendor/rtl8822cs_config.bin
		C+ /lib/firmware/rtl_bt/rtl8822cs_config     0644 root root - /usr/share/atri-fw-vendor/rtl8822cs_config.bin
		C+ /lib/firmware/rtw88/rtw8822c_fw.bin       0644 root root - /usr/share/atri-fw-vendor/wifi_vendor_fw.bin
		C+ /lib/firmware/rtw88/rtl8822cs_efuse.bin   0644 root root - /usr/share/atri-fw-vendor/rtl8822cs_efuse.bin
		C+ /lib/firmware/rtl_bt/rtl8822cs_fw.bin     0644 root root - /usr/share/atri-fw-vendor/vendor_bt_fw.bin
	TMPF

	# udev rule for Zigbee coordinator symlink /dev/ttyZigbee
	mkdir -pv "${destination}"/lib/udev/rules.d
	cat <<- 'UDEV' > "${destination}"/lib/udev/rules.d/99-atri-zigbee.rules
		# AtriStation Zigbee radio module (Tuya TZ9213 on UART_AO_B / ttyAML2)
		KERNEL=="ttyAML2", SYMLINK+="ttyZigbee", GROUP="dialout", MODE="0660"
	UDEV

	# First-boot wireless MAC & eFuse provisioning service
	mkdir -pv "${destination}"/lib/systemd/system
	cat <<- 'WIFI_SVC' > "${destination}"/lib/systemd/system/atri-wireless-init.service
		[Unit]
		Description=AtriStation Wireless MAC and eFuse Provisioning
		Before=network-pre.target bluetooth.target systemd-networkd.service NetworkManager.service
		Wants=network-pre.target
		DefaultDependencies=no
		After=local-fs.target systemd-modules-load.service

		[Service]
		Type=oneshot
		RemainAfterExit=yes
		ExecStart=/usr/bin/atri-wireless-init

		[Install]
		WantedBy=multi-user.target
	WIFI_SVC

	# Boot-time Zigbee hardware radio initialization service
	cat <<- 'ZB_SVC' > "${destination}"/lib/systemd/system/atri-zigbee-init.service
		[Unit]
		Description=AtriStation Zigbee Hardware Radio Initialization
		After=systemd-modules-load.service
		Before=zigbee2mqtt.service home-assistant.service
		DefaultDependencies=no

		[Service]
		Type=oneshot
		RemainAfterExit=yes
		ExecStart=/usr/bin/atri-zigbee reset

		[Install]
		WantedBy=multi-user.target
	ZB_SVC

	# Ensure configuration directory exists
	mkdir -pv "${destination}"/etc/atri-wireless

	display_alert "Extension: ${EXTENSION}: ${BOARD}" "staged RTL8822CS vendor firmware, Zigbee udev and services" "info"
	return 0
}

function post_family_tweaks__atri_wireless_enable_services() {
	display_alert "Extension: ${EXTENSION}: ${BOARD}" "enabling wireless & zigbee services" "info"
	chroot_sdcard systemctl --no-reload enable "atri-wireless-init.service" || true
	chroot_sdcard systemctl --no-reload enable "atri-zigbee-init.service" || true
	return 0
}
